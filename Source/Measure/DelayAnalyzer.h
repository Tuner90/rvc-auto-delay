/*
    AutoDelayMeasure / AutoDelayApply —— RVC 实时变声「延迟对齐」VST3 插件
    文件：Source/Measure/DelayAnalyzer.h

    Copyright (C) 2026 晨锋

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU Affero General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Affero General Public License for more details.

    You should have received a copy of the GNU Affero General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <vector>

/**
    延迟测算核心 —— 用「幅度包络互相关」求两路信号之间的时间差。

    为什么不用波形互相关：
      RVC 变声只保留了发音的节奏与音节包络，谐波结构和相位全变了。
      直接对波形做互相关，峰值会很弱甚至错位。幅度包络基本保留，
      所以这里先整流 + 低通取出包络，再对包络做归一化互相关（Pearson）。

    精度：
      包络带宽只有几十 Hz，互相关峰是「圆顶」而非尖峰，靠抛物线插值取顶点，
      实际精度约 ±1~3 ms（下限受 RVC 输出自身的分块拼接平滑限制）。

    线程模型（对音频线程完全无阻塞）：
      音频线程   → 逐块取包络 → 推入 juce::AbstractFifo
      后台线程   → 排空 FIFO 到自己的环形窗口 → 互相关 → 发布结果
      界面线程   → 只读发布出来的结果

    除了 FIFO 之外没有跨线程共享的可变状态。
*/
class DelayAnalyzer : private juce::Thread
{
public:
    enum class Status
    {
        ok = 0,       // 正常出数
        noSidechain,  // 宿主没提供侧链总线（aux 输入是 0 声道）
        noSignal,     // 两路里至少有一路没声音（静音 / 没人说话）
        sameSignal,   // 侧链与主输入是同一份信号，侧链源选错了
        weak          // 相关性太弱，读数不可信
    };

    struct Result
    {
        Status status     = Status::noSidechain;
        double lagMs      = 0.0;   // 主输入(RVC) 相对侧链(本音) 的延迟；正数 = 主输入更晚
        double medianMs   = 0.0;   // 近期读数的中位数（建议填入值）
        double confidence = 0.0;   // 互相关峰值高度 0~1
        double jitterMs   = 0.0;   // 近期读数极差
        int    sampleCount = 0;    // 已积累的读数个数
    };

    DelayAnalyzer();
    ~DelayAnalyzer() override;

    /** 采样率变化时调用（非实时线程）。 */
    void prepare (double newSampleRate) noexcept;

    /** 清空历史与窗口（界面「重置」按钮）。 */
    void reset() noexcept;

    /** 音频线程调用。

        refBus  = 侧链输入总线（参考 = 本音），宿主没接侧链时是 0 声道；
        testBus = 主输入总线（待测 = RVC 输出）。

        两条总线各自按「各声道平均」折成单声道再取包络 —— 这样即便侧链源
        只挂在某一侧声道上，也不会读成静音。 */
    void pushBlock (const juce::AudioBuffer<float>& refBus,
                    const juce::AudioBuffer<float>& testBus) noexcept;

    /** 界面线程调用。 */
    Result getResult() const;

    /** 界面线程调用，取近期读数历史（毫秒）；不足的部分为 NaN。 */
    void copyHistory (std::vector<double>& dest) const;

    /** 界面线程调用，取最近一次的相关曲线（已降采样到 kCurvePoints 个点，
        覆盖整个 ±maxLag 范围）。用来判断是不是有好几个并排的假峰。 */
    void copyCurve (std::vector<double>& dest) const;

    /** 界面线程调用，取最终选中的那个峰在曲线上的位置，0~1（0.5 = 零延迟）。 */
    double getCurvePeakRatio() const;

    // ---- 分析参数 ----
    static constexpr double kEnvelopeRate  = 1000.0;  // 包络采样率目标 1kHz
    static constexpr double kWindowSeconds = 6.0;     // 每次互相关使用的时长
    static constexpr double kMaxLagSeconds = 1.0;     // 搜索范围 ±1000ms
    static constexpr int    kHistoryLength = 60;      // 历史条长度
    static constexpr int    kCurvePoints   = 400;     // 相关曲线上报给界面的点数

private:
    void run() override;
    void appendToRing (const float* r, const float* t, int n) noexcept;
    void drainFifo();
    void analyse();
    void publishStatus (Status s) noexcept;

    static constexpr int kWindowSize = 16384;  // 环形窗口，必须是 2 的幂
    static constexpr int kWindowMask = kWindowSize - 1;
    static constexpr int kFifoSize   = 16384;
    static constexpr int kMaxEnvPerBlock = 1024;

    // 相关曲线平滑核（5 抽头二项式），抹掉逐样点噪声造成的峰位乱蹦
    static constexpr double kSmoothKernel[5] = { 1.0, 4.0, 6.0, 4.0, 1.0 };

    // 防跳峰：与最高峰差距在此以内的局部极大都视为「并列」，
    // 从中挑离上一轮读数最近的那个，避免在旁瓣之间来回跳。
    // 定得偏大是有意的：相关峰只有 0.6 左右时，旁瓣很容易落在 0.1 以内，
    // 门槛太低就锁不住，读数会在几个假峰之间来回蹦。
    static constexpr double kPeakTieTolerance = 0.12;

    juce::AbstractFifo fifo { kFifoSize };
    std::vector<float> fifoRef, fifoTest;

    // ---- 只有音频线程访问 ----
    float accumRef = 0.0f, accumTest = 0.0f;
    int   accumCount = 0;
    int   decimCount = 48;          // 抽取比，由采样率算出（48 kHz 下 = 48 → 包络 1 kHz）
    float lpCoeff    = 0.27f;       // 包络一阶低通系数（约 50 Hz）
    float envLpRef = 0.0f, envLpTest = 0.0f;   // 低通状态
    std::vector<float> blockEnvRef, blockEnvTest;
    std::atomic<bool>  referencePresent { false };   // 本块侧链是否有声道

    // ---- 分析侧参数（prepare 写、后台线程读）----
    std::atomic<double> envelopeRate   { kEnvelopeRate };
    std::atomic<int>    windowSamples  { 3000 };
    std::atomic<int>    maxLagSamples  { 600 };
    std::atomic<bool>   resetRequested { false };
    std::atomic<int>    publishedCount { 0 };

    // ---- 只有后台线程访问 ----
    std::vector<float>  ringRef, ringTest;
    int                 ringPos = 0;
    int                 ringFill = 0;   // 环形窗口里已有的有效样本数（上限 kWindowSize）
    std::vector<double> history;
    std::vector<double> workRef, workTest;
    std::vector<double> centredRef;      // 已去均值的参考包络
    std::vector<double> prefixTest, prefixTest2;
    std::vector<double> rhos;            // 各延迟位置的相关值
    std::vector<double> smoothed;        // 抹平后的相关值（取峰用这个）
    std::vector<double> curve;           // 降采样后、准备上报给界面画图的相关曲线
    double              peakRatio = 0.5; // 选中峰的位置，0~1（0.5 = 零延迟）

    // ---- 发布给界面（由 lock 保护）----
    mutable juce::SpinLock lock;
    Result               sharedResult;
    std::vector<double>  sharedHistory;
    std::vector<double>  sharedCurve;
    double               sharedPeakRatio = 0.5;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DelayAnalyzer)
};
