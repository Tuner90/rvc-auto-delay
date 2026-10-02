/*
    AutoDelayMeasure / AutoDelayApply —— RVC 实时变声「延迟对齐」VST3 插件
    文件：Source/Apply/PluginProcessor.cpp

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

#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>

namespace
{
    constexpr int kMaxProcessChannels = 8;

    /** 从延迟线里取一个样点，延迟可以是小数（线性插值）。 */
    inline float readDelayed (const float* data, int writePos, double delaySamples, int mask)
    {
        const double pos  = (double) writePos - delaySamples;
        const double flo  = std::floor (pos);
        const int    i0   = (int) flo;
        const float  frac = (float) (pos - flo);

        const float a = data[i0 & mask];
        const float b = data[(i0 + 1) & mask];

        return a + frac * (b - a);
    }
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout
AutoDelayApplyAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // 基准延迟：链接收到的值就写进这里（会随工程存盘）
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "delay", 1 },
        "Delay",
        juce::NormalisableRange<float> (0.0f, 2000.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("ms")));

    // 微调：实际延迟 = delay + offset
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "offset", 1 },
        "Offset",
        juce::NormalisableRange<float> (-100.0f, 100.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("ms")));

    // 链接开关：关掉就纯手动，不再接受链接来的值
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "link", 1 }, "Link", true));

    return { params.begin(), params.end() };
}

//==============================================================================
AutoDelayApplyAudioProcessor::AutoDelayApplyAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    delayParam  = apvts.getRawParameterValue ("delay");
    offsetParam = apvts.getRawParameterValue ("offset");
    linkParam   = apvts.getRawParameterValue ("link");

    // 只接受「本次实例启动之后」发出的值，见 timerCallback
    sessionStartMs = (double) juce::Time::currentTimeMillis();

    startTimer (200);   // 链接轮询：200 ms 一次，够用且不占 CPU
}

AutoDelayApplyAudioProcessor::~AutoDelayApplyAudioProcessor()
{
    stopTimer();
}

//==============================================================================
void AutoDelayApplyAudioProcessor::prepareToPlay (double sampleRate, int /*samplesPerBlock*/)
{
    // ⚠️ 绝不上报延迟。宿主会对已上报的延迟做自动补偿，一补偿就等于没做。
    setLatencySamples (0);

    allocateDelayBuffer (sampleRate);

    // 起播时不要从「0 延迟」淡化到目标值，直接按当前参数就位
    const double initialMs = (double) juce::jlimit (-100.0f, 2100.0f,
                                       delayParam->load() + offsetParam->load());
    const double initial   = juce::jlimit (0.0, maxDelaySamples,
                                           initialMs * sampleRate * 0.001);

    activeDelaySamples = initial;
    targetDelaySamples = initial;
    fadeFromSamples    = initial;
    fadeToSamples      = initial;
    fadePos            = 1.0;
    fading             = false;
}

void AutoDelayApplyAudioProcessor::allocateDelayBuffer (double newSampleRate)
{
    currentSampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;

    // 缓冲区要容得下最大延迟；取 2 的幂，环形取模就能用位与
    maxDelaySamples = std::ceil (currentSampleRate * kMaxDelayMs * 0.001) + 4.0;

    int size = 2;
    while ((double) size < maxDelaySamples + 4.0)
        size <<= 1;

    bufferSize = size;
    bufferMask = size - 1;

    const int chans = juce::jmax (2, getTotalNumInputChannels());
    delayBuffer.setSize (chans, bufferSize, false, true, true);
    delayBuffer.clear();

    writePos         = 0;
    fadeLengthSamples = juce::jmax (1.0, currentSampleRate * kFadeSeconds);
}

void AutoDelayApplyAudioProcessor::releaseResources()
{
    delayBuffer.setSize (0, 0);
}

bool AutoDelayApplyAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in  = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();

    if (in != out)
        return false;

    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void AutoDelayApplyAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                                juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numChans   = juce::jmin (buffer.getNumChannels(),
                                       delayBuffer.getNumChannels(),
                                       kMaxProcessChannels);

    if (numSamples <= 0 || numChans <= 0 || bufferSize <= 1)
        return;

    // ---- 目标延迟 = delay + offset ----
    const float totalMs = delayParam->load() + offsetParam->load();
    targetDelaySamples  = juce::jlimit (0.0, maxDelaySamples,
                                        (double) totalMs * currentSampleRate * 0.001);

    float* io[kMaxProcessChannels] = {};
    float* dl[kMaxProcessChannels] = {};

    for (int ch = 0; ch < numChans; ++ch)
    {
        io[ch] = buffer.getWritePointer (ch);
        dl[ch] = delayBuffer.getWritePointer (ch);
    }

    int wp = writePos;

    for (int i = 0; i < numSamples; ++i)
    {
        // ---- 目标变了：开始淡化，或在淡化途中改终点 ----
        // 淡化途中改终点时 fadePos 不动，混合比例保持连续，不会突然「咔」一下。
        if (std::abs (targetDelaySamples - fadeToSamples) > 1.0e-6)
        {
            if (! fading)
            {
                fadeFromSamples = activeDelaySamples;
                fading          = true;
                fadePos         = 0.0;
            }

            fadeToSamples = targetDelaySamples;
        }

        float g = 0.0f;

        if (fading)
        {
            // 等功率曲线（raised cosine）：中点两路各 0.5，总功率不变，不会掉音量
            g = 0.5f - 0.5f * (float) std::cos (juce::MathConstants<double>::pi * fadePos);

            fadePos += 1.0 / fadeLengthSamples;

            if (fadePos >= 1.0)
            {
                fading             = false;
                fadePos            = 1.0;
                activeDelaySamples = fadeToSamples;
            }
        }

        for (int ch = 0; ch < numChans; ++ch)
        {
            auto* d = dl[ch];

            d[wp] = io[ch][i];                      // 先写进延迟线

            float out;

            if (fading)
            {
                // 两个读指针都按 1 倍速走 —— 所以【不变调】
                const float oldOut = readDelayed (d, wp, fadeFromSamples, bufferMask);
                const float newOut = readDelayed (d, wp, fadeToSamples,   bufferMask);
                out = (1.0f - g) * oldOut + g * newOut;
            }
            else
            {
                out = readDelayed (d, wp, activeDelaySamples, bufferMask);
            }

            io[ch][i] = out;
        }

        if (++wp >= bufferSize)
            wp = 0;
    }

    writePos = wp;
}

//==============================================================================
juce::AudioProcessorEditor* AutoDelayApplyAudioProcessor::createEditor()
{
    return new AutoDelayApplyAudioProcessorEditor (*this);
}

//==============================================================================
AutoDelayApplyAudioProcessor::LinkStatus AutoDelayApplyAudioProcessor::getLinkStatus() const
{
    LinkStatus s;

    s.valueReceived = hasValue;
    s.valueMs       = lastValueMs;

    if (hasValue)
        s.ageSeconds = juce::jmax (0.0,
                        ((double) juce::Time::currentTimeMillis() - lastSentAtMs) * 0.001);

    return s;
}

void AutoDelayApplyAudioProcessor::timerCallback()
{
    const auto snap = link.read();

    if (! snap.valid)
        return;

    // 只认「本次运行之后」发出的值。
    // 否则每次打开工程，都会被上一次留下的旧值把手动改过的 Delay 冲掉。
    if (snap.sentAtMs < sessionStartMs - 1000.0)
        return;

    hasValue     = true;
    lastValueMs  = snap.valueMs;
    lastSentAtMs = snap.sentAtMs;

    const bool linkOn = isLinkEnabled();

    if (! snap.isNew || ! linkOn)
        return;

    // 写进 Delay 参数（写参数 = 会被宿主记录进工程）
    if (auto* p = apvts.getParameter ("delay"))
    {
        const float v = juce::jlimit (0.0f, 2000.0f, (float) snap.valueMs);
        p->setValueNotifyingHost (p->convertTo0to1 (v));
    }
}

//==============================================================================
void AutoDelayApplyAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void AutoDelayApplyAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

// ============================================================================
// JUCE 插件入口
// ============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AutoDelayApplyAudioProcessor();
}
