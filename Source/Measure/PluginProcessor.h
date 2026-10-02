/*
    AutoDelayMeasure / AutoDelayApply —— RVC 实时变声「延迟对齐」VST3 插件
    文件：Source/Measure/PluginProcessor.h

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

#include "DelayAnalyzer.h"

/**
    AutoDelayMeasure —— 纯测量插件。

    职责：
      - 把主输入原样放行（本插件挂在 RVC 轨上，必须透传，否则声音断了）
      - 把「侧链输入」当参考、把「主输入」当待测，送进 DelayAnalyzer
      - 把结果交给界面显示

    总线：
      主输入  Input     = RVC 轨的输出（待测）
      侧链输入 Sidechain = 本音轨（参考）—— 由宿主在插件上指定侧链源
      主输出  Output    = 与主输入一致，原样透传

    刻意不做的事：
      - 不上报任何延迟（setLatencySamples 恒为 0）。
        宿主会对「已上报的延迟」做自动补偿，反而把我们想测的相对偏移藏起来。
      - 不做补偿。补偿由 Studio One 的「轨道 Inspector → Delay」完成。

    用法：
      把本插件插在 RVC 轨的插入链末端，侧链源选【本音轨】。
      连续说话几秒，等读数稳定，把中位数填进环境音轨 Inspector → Delay。
*/
class AutoDelayMeasureAudioProcessor : public juce::AudioProcessor
{
public:
    AutoDelayMeasureAudioProcessor();
    ~AutoDelayMeasureAudioProcessor() override;

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

    DelayAnalyzer& getAnalyzer() noexcept { return analyzer; }

private:
    DelayAnalyzer analyzer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AutoDelayMeasureAudioProcessor)
};
