/*
    AutoDelayMeasure / AutoDelayApply —— RVC 实时变声「延迟对齐」VST3 插件
    文件：Source/Measure/PluginEditor.h

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
#include "DelayLink.h"

/**
    AutoDelayMeasure 界面。

    一块大读数 + 一条历史走势 + 状态提示。
    读数取的是近期读数的中位数（比瞬时值稳），历史条能看出抖动有多大。
*/
class AutoDelayMeasureAudioProcessorEditor : public juce::AudioProcessorEditor,
                                             private juce::Timer
{
public:
    explicit AutoDelayMeasureAudioProcessorEditor (AutoDelayMeasureAudioProcessor&);
    ~AutoDelayMeasureAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    /** 把当前保留的中位数值通过共享通道发给 AutoDelayApply。 */
    void sendReading();

    /** 界面统一字体（系统默认无衬线字体，界面全英文）。 */
    static juce::Font uiFont (float height, int style = juce::Font::plain);

    /** 等宽数字用，读数跳动时宽度不乱。 */
    static juce::Font numberFont (float height, int style = juce::Font::plain);

    static juce::String statusText (DelayAnalyzer::Status s);
    static juce::Colour statusColour (DelayAnalyzer::Status s);

    void paintReadout (juce::Graphics&);
    void paintHistory (juce::Graphics&);
    void paintCorrelation (juce::Graphics&);

    AutoDelayMeasureAudioProcessor& processor;

    juce::TextButton resetButton { "Reset" };

    // 「发送」把大读数（中位数）通过共享内存文件推给 AutoDelayApply。
    // 按钮右下角那块地方是读数面板，按钮是子组件，会盖在绘制内容之上。
    juce::TextButton sendButton { "Send" };
    DelayLink        link;
    int              sendFeedbackFrames = 0;   // 点完显示 1.5 秒「已发送」（20Hz 计时器）

    juce::Rectangle<int> readoutArea, historyArea, curveArea, statusArea;

    DelayAnalyzer::Result result;
    std::vector<double>   history;
    std::vector<double>   curve;              // 相关曲线（看有没有并排的假峰）
    double                curvePeakRatio = 0.5;

    // 停嘴之后分析窗会滑进静音区、状态变回「等待声音」。
    // 这里把最近一次有效的读数留住，免得数字刚看到就没了。
    double lastGoodMedianMs = 0.0;
    bool   hasGoodReading    = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AutoDelayMeasureAudioProcessorEditor)
};
