/*
    AutoDelayMeasure / AutoDelayApply —— RVC 实时变声「延迟对齐」VST3 插件
    文件：Source/Apply/PluginEditor.h

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

#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"

/**
    AutoDelayApply 界面。

    上：实际生效的延迟（delay + offset）+ 链接状态
    中：Link 开关、Offset 微调、Delay 主滑块
    下：状态提示 + Reset
*/
class AutoDelayApplyAudioProcessorEditor : public juce::AudioProcessorEditor,
                                           private juce::Timer
{
public:
    explicit AutoDelayApplyAudioProcessorEditor (AutoDelayApplyAudioProcessor&);
    ~AutoDelayApplyAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    static juce::Font uiFont (float height, int style = juce::Font::plain);
    static juce::Font numberFont (float height, int style = juce::Font::plain);

    void paintReadout (juce::Graphics&);

    /** 实际生效的延迟 = delay + offset（毫秒）。 */
    double appliedDelayMs() const;

    AutoDelayApplyAudioProcessor& processor;

    juce::TextButton   resetButton { "Reset" };
    juce::ToggleButton linkButton  { "Link" };
    juce::Slider       offsetSlider, delaySlider;
    juce::Label        offsetLabel, delayLabel;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    SliderAttachment offsetAttachment { processor.apvts, "offset", offsetSlider };
    SliderAttachment delayAttachment  { processor.apvts, "delay",  delaySlider };
    ButtonAttachment linkAttachment   { processor.apvts, "link",   linkButton };

    juce::Rectangle<int> readoutArea, linkRow, delayRow, bottomRow, footerArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AutoDelayApplyAudioProcessorEditor)
};
