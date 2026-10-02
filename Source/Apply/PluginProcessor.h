/*
    AutoDelayMeasure / AutoDelayApply —— RVC 实时变声「延迟对齐」VST3 插件
    文件：Source/Apply/PluginProcessor.h

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

#include <juce_audio_processors/juce_audio_processors.h>

#include "DelayLink.h"

/**
    AutoDelayApply —— 接收 AutoDelayMeasure 测出的延迟，把本轨音频整体延后。

    用途：
      挂在【环境音轨】上，替代 Studio One 的 Inspector → Manual Track Delay。
      在 AutoDelayMeasure 上点一次 Send，本插件就把值写进自己的 Delay 参数
      （写进参数 → 随工程存盘，跟 Auto-Tune 记调性一个道理）。

    ⚠️ 绝不上报延迟：
      宿主会对「插件正确上报的延迟」做自动补偿（PDC），一补偿就等于什么都没做。
      所以 setLatencySamples 恒为 0，让这段延迟对宿主不可见。
      同理，环境音轨自己的 Manual Delay 必须清 0，否则会双重补偿。

    参数：
      delay   0 ~ 2000 ms   基准延迟，链接收到的值写进这里
      offset -100 ~ +100 ms 微调，实际延迟 = delay + offset
      link    开关          关掉就纯手动，不再接受链接来的值

    换延迟时的处理（避免爆音）：
      交叉淡化 60 ms —— 旧读指针照原速走并淡出，新读指针从新位置照原速走并淡入，
      60 ms 后只留新的。两个指针都是 1 倍速，所以【不变调】。
      （不用「读指针平滑滑动」：600 ms 的大跳变那样会像磁带变速一样「呜」一声。）
*/
class AutoDelayApplyAudioProcessor : public juce::AudioProcessor,
                                     private juce::Timer
{
public:
    AutoDelayApplyAudioProcessor();
    ~AutoDelayApplyAudioProcessor() override;

    // ---- AudioProcessor 接口 ----
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }

    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }

    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    /** 界面要显示的链接状态。只由消息线程读写（定时器 + 重绘）。 */
    struct LinkStatus
    {
        bool   valueReceived = false;  // 本次运行期间收到过值
        double valueMs       = 0.0;    // 最近一次收到的值
        double ageSeconds    = 0.0;    // 距发送时刻过去了多久
    };

    LinkStatus getLinkStatus() const;

    /** Link 开关当前是否打开（直接读参数，界面用）。 */
    bool isLinkEnabled() const noexcept
    {
        return linkParam != nullptr && linkParam->load() > 0.5f;
    }

    // ---- 分析参数 ----
    static constexpr double kMaxDelayMs  = 2100.0;  // 2000 + 100，参数上限之和
    static constexpr double kFadeSeconds = 0.06;    // 换延迟时的交叉淡化长度

private:
    void timerCallback() override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    /** 按采样率把延迟线缓冲区开出来（长度取 2 的幂，方便用位与做环形取模）。 */
    void allocateDelayBuffer (double newSampleRate);

    // ---- 参数（音频线程只读这两个 atomic）----
    std::atomic<float>* delayParam  = nullptr;
    std::atomic<float>* offsetParam = nullptr;
    std::atomic<float>* linkParam   = nullptr;

    // ---- 链接（只在消息线程用）----
    DelayLink link;
    double    sessionStartMs = 0.0;   // 本实例启动时刻（Unix 毫秒）
    bool      hasValue       = false;
    double    lastValueMs    = 0.0;
    double    lastSentAtMs   = 0.0;

    // ---- 延迟线（只在 prepareToPlay / processBlock 用）----
    juce::AudioBuffer<float> delayBuffer;
    int    writePos         = 0;      // 环形写指针（所有声道共用一个）
    int    bufferSize       = 1;      // 2 的幂
    int    bufferMask       = 0;
    double currentSampleRate = 48000.0;
    double maxDelaySamples   = 0.0;
    double fadeLengthSamples = 1.0;

    double activeDelaySamples = 0.0;  // 当前实际使用的延迟
    double targetDelaySamples = 0.0;  // 参数算出来的目标延迟
    double fadeFromSamples    = 0.0;  // 淡化起点（旧延迟）
    double fadeToSamples      = 0.0;  // 淡化终点（新延迟）
    double fadePos            = 1.0;  // 0~1 淡化进度，>=1 表示没在淡化
    bool   fading             = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AutoDelayApplyAudioProcessor)
};
