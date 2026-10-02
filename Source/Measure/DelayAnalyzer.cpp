/*
    AutoDelayMeasure / AutoDelayApply —— RVC 实时变声「延迟对齐」VST3 插件
    文件：Source/Measure/DelayAnalyzer.cpp

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

#include "DelayAnalyzer.h"

// ============================================================================
// 内部的包络抽取（音频线程调用，全部是无分支的定点量级运算，不会阻塞）
// ============================================================================

DelayAnalyzer::DelayAnalyzer()
    : juce::Thread ("AutoDelayMeasure Analyzer")
{
    fifoRef.resize ((size_t) kFifoSize);
    fifoTest.resize ((size_t) kFifoSize);

    ringRef.assign ((size_t) kWindowSize, 0.0f);
    ringTest.assign ((size_t) kWindowSize, 0.0f);

    blockEnvRef.reserve ((size_t) kMaxEnvPerBlock);
    blockEnvTest.reserve ((size_t) kMaxEnvPerBlock);
    history.reserve ((size_t) kHistoryLength);

    sharedHistory.assign ((size_t) kHistoryLength,
                          std::numeric_limits<double>::quiet_NaN());

    startThread (juce::Thread::Priority::low);
}

DelayAnalyzer::~DelayAnalyzer()
{
    stopThread (500);
}

void DelayAnalyzer::prepare (double newSampleRate) noexcept
{
    if (newSampleRate <= 0.0)
        newSampleRate = 48000.0;

    // 抽取比：把采样率降到约 1 kHz 的包络率
    const int decim = juce::jmax (1, (int) std::round (newSampleRate / kEnvelopeRate));
    const double actualEnvRate = newSampleRate / (double) decim;

    decimCount = decim;

    // 一阶低通：截止约 50 Hz（语音音节包络的有效带宽在这里面）
    lpCoeff = (float) (1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi
                                        * 50.0 / actualEnvRate));

    envelopeRate .store (actualEnvRate);
    windowSamples.store ((int) std::round (kWindowSeconds  * actualEnvRate));
    maxLagSamples.store ((int) std::round (kMaxLagSeconds  * actualEnvRate));

    // 采样率变了，历史读数不再可比，顺手清空
    resetRequested.store (true);
}

void DelayAnalyzer::reset() noexcept
{
    resetRequested.store (true);
}

// ============================================================================
// 音频线程
// ============================================================================

void DelayAnalyzer::pushBlock (const juce::AudioBuffer<float>& refBus,
                               const juce::AudioBuffer<float>& testBus) noexcept
{
    const int numSamples = testBus.getNumSamples();
    const int refCh      = refBus.getNumChannels();
    const int testCh     = testBus.getNumChannels();

    const bool haveReference = (refCh > 0 && numSamples > 0);
    referencePresent.store (haveReference);

    if (! haveReference || testCh <= 0)
        return;

    const auto* const* refPtrs  = refBus .getArrayOfReadPointers();
    const auto* const* testPtrs = testBus.getArrayOfReadPointers();

    const float refScale  = 1.0f / (float) refCh;
    const float testScale = 1.0f / (float) testCh;

    blockEnvRef.clear();
    blockEnvTest.clear();

    const int decim = juce::jmax (1, decimCount);

    for (int i = 0; i < numSamples; ++i)
    {
        float r = 0.0f;
        for (int c = 0; c < refCh; ++c)
            r += refPtrs[c][i];

        float t = 0.0f;
        for (int c = 0; c < testCh; ++c)
            t += testPtrs[c][i];

        accumRef  += std::abs (r * refScale);
        accumTest += std::abs (t * testScale);

        if (++accumCount >= decim)
        {
            const float inv = 1.0f / (float) accumCount;
            const float eR  = accumRef  * inv;
            const float eT  = accumTest * inv;

            envLpRef  += lpCoeff * (eR - envLpRef);
            envLpTest += lpCoeff * (eT - envLpTest);

            if ((int) blockEnvRef.size() < kMaxEnvPerBlock)
            {
                blockEnvRef .push_back (envLpRef);
                blockEnvTest.push_back (envLpTest);
            }

            accumRef  = 0.0f;
            accumTest = 0.0f;
            accumCount = 0;
        }
    }

    const int num = (int) blockEnvRef.size();
    if (num <= 0)
        return;

    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    fifo.prepareToWrite (num, start1, size1, start2, size2);

    if (size1 > 0)
    {
        std::copy (blockEnvRef .begin(), blockEnvRef .begin() + size1, fifoRef .begin() + start1);
        std::copy (blockEnvTest.begin(), blockEnvTest.begin() + size1, fifoTest.begin() + start1);
    }
    if (size2 > 0)
    {
        std::copy (blockEnvRef .begin() + size1, blockEnvRef .end(), fifoRef .begin() + start2);
        std::copy (blockEnvTest.begin() + size1, blockEnvTest.end(), fifoTest.begin() + start2);
    }

    fifo.finishedWrite (size1 + size2);
}

// ============================================================================
// 后台分析线程
// ============================================================================

void DelayAnalyzer::run()
{
    juce::uint32 lastAnalyse = 0;

    while (! threadShouldExit())
    {
        if (resetRequested.exchange (false))
        {
            ringPos  = 0;
            ringFill = 0;
            std::fill (ringRef .begin(), ringRef .end(), 0.0f);
            std::fill (ringTest.begin(), ringTest.end(), 0.0f);
            history.clear();

            const juce::SpinLock::ScopedLockType sl (lock);
            sharedResult = Result();
            sharedHistory.assign ((size_t) kHistoryLength,
                                  std::numeric_limits<double>::quiet_NaN());
        }

        drainFifo();

        const juce::uint32 now = juce::Time::getMillisecondCounter();
        if (now - lastAnalyse >= 500)
        {
            lastAnalyse = now;
            analyse();
        }

        wait (20);
    }
}

void DelayAnalyzer::drainFifo()
{
    for (;;)
    {
        const int ready = fifo.getNumReady();
        if (ready <= 0)
            break;

        int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
        fifo.prepareToRead (ready, start1, size1, start2, size2);

        if (size1 > 0) appendToRing (fifoRef.data() + start1, fifoTest.data() + start1, size1);
        if (size2 > 0) appendToRing (fifoRef.data() + start2, fifoTest.data() + start2, size2);

        fifo.finishedRead (size1 + size2);
    }
}

void DelayAnalyzer::appendToRing (const float* r, const float* t, int n) noexcept
{
    for (int i = 0; i < n; ++i)
    {
        ringRef [(size_t) ringPos] = r[i];
        ringTest[(size_t) ringPos] = t[i];

        ringPos = (ringPos + 1) & kWindowMask;
        if (ringFill < kWindowSize)
            ++ringFill;
    }
}

void DelayAnalyzer::publishStatus (Status s) noexcept
{
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        sharedResult.status = s;
    }
    publishedCount.fetch_add (1);
}

void DelayAnalyzer::analyse()
{
    if (! referencePresent.load())
    {
        publishStatus (Status::noSidechain);
        return;
    }

    const double envRate = juce::jmax (1.0, envelopeRate.load());
    const int    win     = windowSamples.load();
    const int    maxLag  = maxLagSamples.load();

    if (win <= 0 || maxLag <= 0 || win + 2 * maxLag > kWindowSize)
        return;

    if (ringFill < win + 2 * maxLag)
    {
        publishStatus (Status::noSignal);
        return;
    }

    // ---- 从环形窗口里取两段 ----
    //
    // 关键：参考段必须整体往前退一个 maxLag，不能取「最新的 win 个样点」。
    //
    // 我们测的是「test 比 ref 晚多少」。test 晚 L，意味着那段内容出现在更靠后的时刻；
    // 而环形窗口里最新只写到「现在」。若 ref 取最末尾（最新 3 秒），那么所有正延迟的
    // 匹配位置都落在还没写入的未来区 —— 那里读到的是缓冲绕回来的旧数据（约 8 秒前），
    // 于是真峰永远找不到，只能抓到某个假峰，读数就会乱跳、而且钉在搜索区边上。
    //
    // ref 退后 maxLag 之后，±maxLag 的整个搜索区就都落在已写入的数据里了：
    //   ref  = [ringPos-win-maxLag, ringPos-maxLag)
    //   test = [ringPos-win-2*maxLag, ringPos)
    // 延迟 L 处对齐的是 ref 时刻 + L，与 L 的符号定义一致。
    const int testLen   = win + 2 * maxLag;
    const int testStart = (ringPos - win - 2 * maxLag) & kWindowMask;
    const int refStart  = (ringPos - win - maxLag) & kWindowMask;

    workRef.resize ((size_t) win);
    workTest.resize ((size_t) testLen);

    for (int i = 0; i < win; ++i)
        workRef[i] = (double) ringRef[(size_t) ((refStart + i) & kWindowMask)];

    for (int i = 0; i < testLen; ++i)
        workTest[i] = (double) ringTest[(size_t) ((testStart + i) & kWindowMask)];

    // ---- 电平检查：两路里只要有一路是静音，就还测不了 ----
    // 注意：这里不能判成 noSidechain。停嘴之后的静音和「侧链没接对」
    // 在信号上完全一样，只能如实报告「没声音」。
    double sumRef = 0.0, sumTest = 0.0;
    for (int i = 0; i < win; ++i)
    {
        sumRef  += std::abs (workRef[i]);
        sumTest += std::abs (workTest[maxLag + i]);
    }

    const double levelFloor = 1.0e-4;        // 约 -76 dBFS 以下，当作没有信号

    if (sumRef / (double) win < levelFloor || sumTest / (double) win < levelFloor)
    {
        publishStatus (Status::noSignal);
        return;
    }

    // ---- 两路雷同检查（侧链源误选成了本插件所在的这一轨）----
    double absDiff = 0.0;
    for (int i = 0; i < win; ++i)
        absDiff += std::abs (workRef[i] - workTest[maxLag + i]);

    if (absDiff / sumRef < 0.02)
    {
        publishStatus (Status::sameSignal);
        return;
    }

    // ---- 参考段去均值并算范数 ----
    double meanRef = 0.0;
    for (int i = 0; i < win; ++i)
        meanRef += workRef[i];
    meanRef /= (double) win;

    centredRef.resize ((size_t) win);
    double ssRef = 0.0;
    for (int i = 0; i < win; ++i)
    {
        const double c = workRef[i] - meanRef;
        centredRef[i] = c;
        ssRef += c * c;
    }

    const double normRef = std::sqrt (ssRef);
    if (normRef < 1.0e-9)
    {
        publishStatus (Status::noSignal);
        return;
    }

    // ---- test 前缀和：让每个延迟位置的均值/方差 O(1) 算出 ----
    prefixTest .resize ((size_t) testLen + 1);
    prefixTest2.resize ((size_t) testLen + 1);
    prefixTest [0] = 0.0;
    prefixTest2[0] = 0.0;
    for (int i = 0; i < testLen; ++i)
    {
        const double v = workTest[i];
        prefixTest [i + 1] = prefixTest [i] + v;
        prefixTest2[i + 1] = prefixTest2[i] + v * v;
    }

    // ---- 归一化互相关，扫描 ±maxLag ----
    const int numLags = 2 * maxLag + 1;
    rhos.resize ((size_t) numLags);

    for (int L = -maxLag; L <= maxLag; ++L)
    {
        const int s = L + maxLag;                       // test 段起点 = s，长度 win
        const double sumT  = prefixTest [s + win] - prefixTest [s];
        const double sumT2 = prefixTest2[s + win] - prefixTest2[s];
        const double meanT = sumT / (double) win;
        const double varT  = sumT2 - (double) win * meanT * meanT;

        // 参考段已去均值，所以 dot(refC, test) 就等于 dot(refC, test - meanT)
        double dot = 0.0;
        for (int i = 0; i < win; ++i)
            dot += centredRef[i] * workTest[s + i];

        const double denom = normRef * std::sqrt (juce::jmax (0.0, varT));
        rhos[(size_t) (L + maxLag)] = (denom > 1.0e-12) ? (dot / denom) : 0.0;
    }

    // ---- 先把相关曲线抹平 ----
    // 包络互相关的峰是个圆顶，逐样点的噪声会让峰位在相邻样点间乱蹦；先平滑再取峰。
    smoothed.resize ((size_t) numLags);

    for (int i = 0; i < numLags; ++i)
    {
        double acc = 0.0, wsum = 0.0;

        for (int d = -2; d <= 2; ++d)
        {
            const int j = i + d;
            if (j < 0 || j >= numLags)
                continue;

            const double w = kSmoothKernel[d + 2];
            acc  += w * rhos[(size_t) j];
            wsum += w;
        }

        smoothed[(size_t) i] = acc / wsum;
    }

    // ---- 取全局最高峰 ----
    int globalIdx  = 0;
    double globalRho = smoothed[0];

    for (int i = 1; i < numLags; ++i)
    {
        if (smoothed[(size_t) i] > globalRho)
        {
            globalRho = smoothed[(size_t) i];
            globalIdx = i;
        }
    }

    // ---- 防「跳峰」----
    // 语音包络的自相关在主峰两侧还有旁瓣，旁瓣高度接近主峰时，两轮之间会来回跳，
    // 读数就成了乱蹦。这里在所有「与最高峰差距在 kPeakTieTolerance 以内」的局部
    // 极大里，挑离上一轮中位数最近的那个，让读数在真实的几毫秒抖动上保持连续。
    int bestIdx = globalIdx;

    if (! history.empty())
    {
        std::vector<double> sortedPrev (history);
        std::sort (sortedPrev.begin(), sortedPrev.end());

        const double prevMs = sortedPrev[sortedPrev.size() / 2];
        const int    prevIdx = juce::jlimit (0, numLags - 1,
                                             (int) std::lround (prevMs / 1000.0 * envRate) + maxLag);

        const double threshold = globalRho - kPeakTieTolerance;

        int    nearestIdx  = globalIdx;
        int    nearestDist = std::abs (globalIdx - prevIdx);

        for (int i = 1; i < numLags - 1; ++i)
        {
            if (smoothed[(size_t) i] < threshold)
                continue;

            // 只认局部极大，别被斜坡上的点骗走
            if (smoothed[(size_t) i] < smoothed[(size_t) (i - 1)]
                || smoothed[(size_t) i] < smoothed[(size_t) (i + 1)])
                continue;

            const int dist = std::abs (i - prevIdx);

            if (dist < nearestDist)
            {
                nearestDist = dist;
                nearestIdx  = i;
            }
        }

        bestIdx = nearestIdx;
    }

    const double bestRho = rhos[(size_t) bestIdx];

    // ---- 抛物线插值，把整数样本的峰细化到小数 ----
    double refined = (double) (bestIdx - maxLag);
    if (bestIdx > 0 && bestIdx < numLags - 1)
    {
        const double y0 = smoothed[(size_t) (bestIdx - 1)];
        const double y1 = smoothed[(size_t) bestIdx];
        const double y2 = smoothed[(size_t) (bestIdx + 1)];
        const double den = y0 - 2.0 * y1 + y2;

        if (std::abs (den) > 1.0e-12)
        {
            const double delta = 0.5 * (y0 - y2) / den;
            if (std::abs (delta) <= 1.0)
                refined += delta;
        }
    }

    const double lagMs = refined / envRate * 1000.0;
    const double conf  = juce::jlimit (0.0, 1.0, bestRho);

    if (conf < 0.25)
    {
        publishStatus (Status::weak);
        return;
    }

    // ---- 记入历史，算中位数与抖动 ----
    history.push_back (lagMs);
    while ((int) history.size() > kHistoryLength)
        history.erase (history.begin());

    std::vector<double> sorted (history);
    std::sort (sorted.begin(), sorted.end());

    const double median = sorted[sorted.size() / 2];
    const double jitter = sorted.back() - sorted.front();

    // ---- 降采样一条曲线给界面画图（400 点足够看出有没有并排的假峰）----
    curve.resize ((size_t) kCurvePoints);
    for (int i = 0; i < kCurvePoints; ++i)
    {
        const int idx = (kCurvePoints > 1)
                          ? (int) std::lround ((double) i / (double) (kCurvePoints - 1)
                                               * (double) (numLags - 1))
                          : 0;
        curve[(size_t) i] = smoothed[(size_t) juce::jlimit (0, numLags - 1, idx)];
    }

    // 选中的峰在曲线上的相对位置（0.5 = 零延迟）
    peakRatio = (numLags > 1) ? (double) bestIdx / (double) (numLags - 1) : 0.5;

    {
        const juce::SpinLock::ScopedLockType sl (lock);

        sharedResult.status      = Status::ok;
        sharedResult.lagMs       = lagMs;
        sharedResult.medianMs    = median;
        sharedResult.confidence  = conf;
        sharedResult.jitterMs    = jitter;
        sharedResult.sampleCount = (int) history.size();

        sharedHistory.assign ((size_t) kHistoryLength,
                              std::numeric_limits<double>::quiet_NaN());
        const int off = kHistoryLength - (int) history.size();
        for (int i = 0; i < (int) history.size(); ++i)
            sharedHistory[(size_t) (off + i)] = history[(size_t) i];

        sharedCurve = curve;
        sharedPeakRatio = peakRatio;
    }

    publishedCount.fetch_add (1);
}

// ============================================================================
// 界面线程
// ============================================================================

DelayAnalyzer::Result DelayAnalyzer::getResult() const
{
    const juce::SpinLock::ScopedLockType sl (lock);
    return sharedResult;
}

void DelayAnalyzer::copyHistory (std::vector<double>& dest) const
{
    const juce::SpinLock::ScopedLockType sl (lock);
    dest = sharedHistory;
}

void DelayAnalyzer::copyCurve (std::vector<double>& dest) const
{
    const juce::SpinLock::ScopedLockType sl (lock);
    dest = sharedCurve;
}

double DelayAnalyzer::getCurvePeakRatio() const
{
    const juce::SpinLock::ScopedLockType sl (lock);
    return sharedPeakRatio;
}
