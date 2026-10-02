/*
    AutoDelayMeasure / AutoDelayApply —— RVC 实时变声「延迟对齐」VST3 插件
    文件：Source/Measure/PluginProcessor.cpp

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

AutoDelayMeasureAudioProcessor::AutoDelayMeasureAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
                          .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)
                          .withOutput ("Output",    juce::AudioChannelSet::stereo(), true))
{
}

AutoDelayMeasureAudioProcessor::~AutoDelayMeasureAudioProcessor()
{
}

void AutoDelayMeasureAudioProcessor::prepareToPlay (double sampleRate, int /*samplesPerBlock*/)
{
    // 测量插件自己不引入任何延迟，也不许宿主替我们补偿
    setLatencySamples (0);
    analyzer.prepare (sampleRate);
}

void AutoDelayMeasureAudioProcessor::releaseResources()
{
}

bool AutoDelayMeasureAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in  = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();

    // 主输入必须是「进 = 出」，且只接受单声道 / 立体声
    if (in != out)
        return false;

    if (in != juce::AudioChannelSet::mono() && in != juce::AudioChannelSet::stereo())
        return false;

    // 侧链（第 2 条输入总线）：允许不接（disabled），也允许单声道 / 立体声
    if (layouts.inputBuses.size() > 1)
    {
        const auto& side = layouts.getChannelSet (true, 1);

        if (! side.isDisabled()
            && side != juce::AudioChannelSet::mono()
            && side != juce::AudioChannelSet::stereo())
            return false;
    }

    return true;
}

void AutoDelayMeasureAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                                   juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // 侧链 = 第 2 条输入总线。宿主没接的时候它的声道数就是 0，
    // 此时 getBusBuffer 返回的也是一块 0 声道的缓冲，读它是安全的。
    const auto* sideBus = getBus (true, 1);
    const int   sideCh  = (sideBus != nullptr && sideBus->isEnabled())
                              ? sideBus->getNumberOfChannels() : 0;
    const int   mainCh  = getChannelCountOfBus (true, 0);

    // 兜底：万一宿主给的声道数对不上我们声明的布局，就别去读越界的内存
    if (mainCh > 0 && mainCh + sideCh <= buffer.getNumChannels())
    {
        auto main = getBusBuffer (buffer, true, 0);
        auto side = getBusBuffer (buffer, true, 1);

        analyzer.pushBlock (side, main);
    }

    // 缓冲原样放行：本插件不做任何处理，只旁路观察
}

juce::AudioProcessorEditor* AutoDelayMeasureAudioProcessor::createEditor()
{
    return new AutoDelayMeasureAudioProcessorEditor (*this);
}

void AutoDelayMeasureAudioProcessor::getStateInformation (juce::MemoryBlock&)
{
    // 本插件没有可保存的参数
}

void AutoDelayMeasureAudioProcessor::setStateInformation (const void*, int)
{
}

// ============================================================================
// JUCE 插件入口
// ============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AutoDelayMeasureAudioProcessor();
}
