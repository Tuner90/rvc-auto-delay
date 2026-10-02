/*
    AutoDelayMeasure / AutoDelayApply —— RVC 实时变声「延迟对齐」VST3 插件
    文件：Source/Measure/PluginEditor.cpp

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

#include "PluginEditor.h"

#include <cmath>

namespace
{
    const juce::Colour kBackground   { 0xff1c1e22 };
    const juce::Colour kPanel        { 0xff24272d };
    const juce::Colour kTextDim      { 0xff8b919c };
    const juce::Colour kTextBright   { 0xffe8ebf0 };
}

AutoDelayMeasureAudioProcessorEditor::AutoDelayMeasureAudioProcessorEditor (
        AutoDelayMeasureAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    addAndMakeVisible (resetButton);
    resetButton.onClick = [this]
    {
        processor.getAnalyzer().reset();
        hasGoodReading   = false;
        lastGoodMedianMs = 0.0;
    };

    // 送值按钮：没出数时禁用，免得把 0 或旧值推过去
    addAndMakeVisible (sendButton);
    sendButton.setEnabled (false);
    sendButton.onClick = [this] { sendReading(); };

    setSize (520, 470);
    startTimerHz (20);
}

AutoDelayMeasureAudioProcessorEditor::~AutoDelayMeasureAudioProcessorEditor()
{
    stopTimer();
}

void AutoDelayMeasureAudioProcessorEditor::sendReading()
{
    if (! hasGoodReading)
        return;

    // 写进共享通道（%APPDATA%\ChenFeng\delay_link.dat）
    link.write (lastGoodMedianMs);

    sendFeedbackFrames = 30;                 // 20Hz × 1.5s
    sendButton.setButtonText ("Sent");
}

juce::Font AutoDelayMeasureAudioProcessorEditor::uiFont (float height, int style)
{
    // 界面已全英文，用系统默认无衬线字体即可，不再依赖中文字体
    return juce::Font (juce::FontOptions (height, style));
}

juce::Font AutoDelayMeasureAudioProcessorEditor::numberFont (float height, int style)
{
    return juce::Font (juce::FontOptions ("Consolas", height, style));
}

juce::String AutoDelayMeasureAudioProcessorEditor::statusText (DelayAnalyzer::Status s)
{
    switch (s)
    {
        case DelayAnalyzer::Status::ok:
            return "Stable - use the median for the Ambience track Delay";

        case DelayAnalyzer::Status::noSidechain:
            return "No sidechain input - check the plugin's sidechain routing";

        case DelayAnalyzer::Status::noSignal:
            return "Waiting for audio - keep talking for 5+ seconds";

        case DelayAnalyzer::Status::sameSignal:
            return "Sidechain and input are identical - wrong sidechain source";

        case DelayAnalyzer::Status::weak:
            return "Weak correlation - sidechain = Dry voice, input = RVC";

        default:
            return {};
    }
}

juce::Colour AutoDelayMeasureAudioProcessorEditor::statusColour (DelayAnalyzer::Status s)
{
    switch (s)
    {
        case DelayAnalyzer::Status::ok:  return juce::Colour (0xff4fd0a0);
        case DelayAnalyzer::Status::weak: return juce::Colour (0xffe0b050);
        default:                          return juce::Colour (0xffe06a6a);
    }
}

void AutoDelayMeasureAudioProcessorEditor::timerCallback()
{
    result = processor.getAnalyzer().getResult();
    processor.getAnalyzer().copyHistory (history);
    processor.getAnalyzer().copyCurve (curve);
    curvePeakRatio = processor.getAnalyzer().getCurvePeakRatio();

    // 只在「这次真的出数」时更新保留值；停嘴后不再覆盖，读数就留在界面上
    if (result.status == DelayAnalyzer::Status::ok && result.sampleCount > 0)
    {
        lastGoodMedianMs = result.medianMs;
        hasGoodReading   = true;
    }

    // 「发送」按钮跟着读数有无来开关；「已发送」提示 1.5 秒后复原
    if (sendButton.isEnabled() != hasGoodReading)
        sendButton.setEnabled (hasGoodReading);

    if (sendFeedbackFrames > 0 && --sendFeedbackFrames == 0)
        sendButton.setButtonText ("Send");

    repaint();
}

void AutoDelayMeasureAudioProcessorEditor::resized()
{
    auto r = getLocalBounds().reduced (16);

    r.removeFromTop (24);                  // 标题
    r.removeFromBottom (24);               // 底部说明

    readoutArea = r.removeFromTop (112);
    r.removeFromTop (8);
    historyArea = r.removeFromTop (96);
    r.removeFromTop (10);
    curveArea = r.removeFromTop (96);
    r.removeFromTop (10);

    // Send 放在读数面板右上角：那一行左边是标签，右侧本来空着，
    // 不会压到大数字和下面的细项行。
    sendButton.setBounds (readoutArea.getRight() - 18 - 84,
                          readoutArea.getY() + 12,
                          84, 26);

    auto bottomRow = r.removeFromTop (46);
    resetButton.setBounds (bottomRow.removeFromRight (96).withSizeKeepingCentre (96, 28));
    statusArea = bottomRow;
}

void AutoDelayMeasureAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBackground);

    // ---- 标题 ----
    g.setColour (kTextDim);
    g.setFont (uiFont (13.0f));
    g.drawText ("AutoDelayMeasure",
                getLocalBounds().reduced (16).removeFromTop (24),
                juce::Justification::centredLeft);

    // ---- 版权/许可声明（标题行右侧；纯 ASCII，不会乱码）----
    g.setColour (juce::Colour (0xff5c6169));
    g.setFont (uiFont (11.5f));
    g.drawText ("AGPL-3.0 (C) 2026 ChenFeng",
                getLocalBounds().reduced (16).removeFromTop (24),
                juce::Justification::centredRight);

    paintReadout (g);
    paintHistory (g);
    paintCorrelation (g);

    // ---- 状态提示 ----
    g.setColour (statusColour (result.status));
    g.setFont (uiFont (13.5f));
    g.drawFittedText (statusText (result.status), statusArea, juce::Justification::centredLeft, 2);

    // ---- 底部说明 ----
    g.setColour (kTextDim);
    g.setFont (uiFont (11.5f));
    g.drawText ("Measure only, no compensation. Click Send to push the median to AutoDelayApply.",
                juce::Rectangle<int> (16, getHeight() - 38, getWidth() - 32, 20),
                juce::Justification::centredLeft);
}

void AutoDelayMeasureAudioProcessorEditor::paintReadout (juce::Graphics& g)
{
    g.setColour (kPanel);
    g.fillRoundedRectangle (readoutArea.toFloat(), 6.0f);

    auto inner = readoutArea.reduced (18, 12);

    // 左侧标签
    g.setColour (kTextDim);
    g.setFont (uiFont (12.5f));

    const bool live = (result.status == DelayAnalyzer::Status::ok
                       && result.sampleCount > 0);

    g.drawText (hasGoodReading && ! live ? "Suggested delay (held, last stable)"
                                         : "Suggested delay",
                inner.removeFromTop (16), juce::Justification::topLeft);

    // 大读数（中位数）：出数时亮，停嘴后保留显示但变暗
    auto bigArea = inner.removeFromTop (60);
    const auto bigFont = numberFont (46.0f, juce::Font::bold);

    g.setColour (live ? kTextBright
                      : (hasGoodReading ? kTextDim : juce::Colour (0xff5c6169)));
    g.setFont (bigFont);

    const juce::String bigText = hasGoodReading ? juce::String (lastGoodMedianMs, 1)
                                                : juce::String ("--.-");
    g.drawText (bigText, bigArea, juce::Justification::centredLeft);

    // ms 单位贴在数字右边
    const int textWidth = juce::roundToInt (
        juce::GlyphArrangement::getStringWidth (bigFont, bigText));
    g.setColour (kTextDim);
    g.setFont (uiFont (16.0f));
    g.drawText ("ms", bigArea.withTrimmedLeft (textWidth + 8),
                juce::Justification::centredLeft);

    // 下面一行细项：停嘴后不再隐藏，保留最后一次的数值，只把「现在」换成「上次」
    juce::String detail;

    if (hasGoodReading)
    {
        detail << (live ? "now " : "last ") << juce::String (result.lagMs, 1) << " ms"
               << "    conf " << juce::String (juce::roundToInt (result.confidence * 100.0)) << "%"
               << "    jitter " << juce::String (result.jitterMs, 1) << " ms"
               << "    samples " << result.sampleCount;
    }
    else
    {
        detail << "no reading yet";
    }

    g.setColour (kTextDim);
    g.setFont (numberFont (13.0f));
    g.drawText (detail, inner, juce::Justification::topLeft);
}

void AutoDelayMeasureAudioProcessorEditor::paintHistory (juce::Graphics& g)
{
    g.setColour (kPanel);
    g.fillRoundedRectangle (historyArea.toFloat(), 6.0f);

    auto inner = historyArea.reduced (12, 10);

    if (history.empty())
        return;

    // ---- 统计有效读数 ----
    double vmin =  std::numeric_limits<double>::max();
    double vmax = -std::numeric_limits<double>::max();

    for (double v : history)
    {
        if (std::isnan (v))
            continue;

        vmin = juce::jmin (vmin, v);
        vmax = juce::jmax (vmax, v);
    }

    if (vmin > vmax)
        return;

    if (vmax - vmin < 1.0)
    {
        const double mid = 0.5 * (vmin + vmax);
        vmin = mid - 0.5;
        vmax = mid + 0.5;
    }
    else
    {
        const double pad = (vmax - vmin) * 0.15;
        vmin -= pad;
        vmax += pad;
    }

    const int n = (int) history.size();
    const float step = (float) inner.getWidth() / (float) juce::jmax (1, n - 1);

    auto valueToY = [&] (double v) -> float
    {
        const double t = (v - vmin) / (vmax - vmin);
        return (float) (inner.getBottom() - t * inner.getHeight());
    };

    // ---- 折线 + 点 ----
    juce::Path path;
    bool started = false;

    for (int i = 0; i < n; ++i)
    {
        const double v = history[(size_t) i];
        if (std::isnan (v))
            continue;

        const float x = inner.getX() + step * (float) i;
        const float y = valueToY (v);

        if (! started) { path.startNewSubPath (x, y); started = true; }
        else            path.lineTo (x, y);
    }

    g.setColour (juce::Colour (0xff4fd0a0));
    g.strokePath (path, juce::PathStrokeType (1.8f));

    // ---- 中位数参考线（用保留值，停嘴后也还在）----
    if (hasGoodReading)
    {
        const float y = valueToY (lastGoodMedianMs);

        juce::Path solid;
        solid.startNewSubPath ((float) inner.getX(), y);
        solid.lineTo ((float) inner.getRight(), y);

        const float dashes[] = { 4.0f, 4.0f };
        juce::Path dashed;
        juce::PathStrokeType (1.0f).createDashedStroke (dashed, solid, dashes, 2);

        g.setColour (juce::Colour (0x66e8ebf0));
        g.fillPath (dashed);
    }

    // ---- 上下界标注 ----
    g.setColour (kTextDim);
    g.setFont (numberFont (10.5f));
    g.drawText (juce::String (vmax, 1), inner.removeFromTop (12),
                juce::Justification::topLeft);
    g.drawText (juce::String (vmin, 1), inner.removeFromBottom (12),
                juce::Justification::bottomLeft);
}

// 相关曲线：横轴是延迟（-1000ms ~ +1000ms），纵轴是相关值。
// 用途是判断峰好不好找 —— 只有一个尖峰说明测得住；一排几乎等高的峰
// 就说明有两个以上的假峰在抢，读数当然会跳。
void AutoDelayMeasureAudioProcessorEditor::paintCorrelation (juce::Graphics& g)
{
    g.setColour (kPanel);
    g.fillRoundedRectangle (curveArea.toFloat(), 6.0f);

    auto inner = curveArea.reduced (12, 8);

    g.setColour (kTextDim);
    g.setFont (uiFont (11.5f));
    g.drawText ("correlation vs lag", inner.removeFromTop (14), juce::Justification::topLeft);

    const int n = (int) curve.size();

    if (n < 2)
    {
        g.setColour (juce::Colour (0xff5c6169));
        g.setFont (uiFont (12.5f));
        g.drawText ("no correlation data yet", inner, juce::Justification::centred);
        return;
    }

    auto axisArea = inner.removeFromBottom (11);
    auto plot     = inner.reduced (0, 4);

    // ---- 纵轴范围（留一点上下空白，别贴着边框）----
    double trueMin = curve[0], trueMax = curve[0];

    for (double v : curve)
    {
        trueMin = juce::jmin (trueMin, v);
        trueMax = juce::jmax (trueMax, v);
    }

    const double span    = juce::jmax (1.0e-6, trueMax - trueMin);
    const double drawMin = trueMin - span * 0.12;
    const double drawMax = trueMax + span * 0.12;

    // ---- 零延迟刻度线（曲线正中间）----
    g.setColour (juce::Colour (0xff3a3f47));
    g.drawVerticalLine (plot.getCentreX(), (float) plot.getY(), (float) plot.getBottom());

    // ---- 曲线本体 ----
    juce::Path path;
    bool started = false;

    for (int i = 0; i < n; ++i)
    {
        const float x = (float) plot.getX()
                        + (float) i / (float) (n - 1) * (float) plot.getWidth();
        const float y = (float) plot.getBottom()
                        - (float) ((curve[(size_t) i] - drawMin) / (drawMax - drawMin))
                              * (float) plot.getHeight();

        if (! started) { path.startNewSubPath (x, y); started = true; }
        else            path.lineTo (x, y);
    }

    g.setColour (juce::Colour (0xff6fa8ff));
    g.strokePath (path, juce::PathStrokeType (1.4f));

    // ---- 选中的峰（绿线）----
    const float px = (float) plot.getX()
                     + (float) curvePeakRatio * (float) plot.getWidth();

    g.setColour (juce::Colour (0xff4fd0a0));
    g.drawVerticalLine ((int) px, (float) plot.getY(), (float) plot.getBottom());

    // ---- 顶部标注：峰值相关值 + 并列峰的个数 ----
    // 「并列峰」= 高度在最高峰的 0.12 以内的局部极大（和防跳峰用的门槛一致）。
    // 这个数大于 1，就说明峰不唯一，读数必然会跳。
    int tiedPeaks = 0;

    for (int i = 1; i < n - 1; ++i)
    {
        const double v = curve[(size_t) i];

        if (v >= curve[(size_t) (i - 1)] && v >= curve[(size_t) (i + 1)]
            && v >= trueMax - 0.12)
            ++tiedPeaks;
    }

    g.setColour (kTextDim);
    g.setFont (numberFont (10.5f));
    g.drawText ("peak " + juce::String (trueMax, 2)
                    + "    tied peaks " + juce::String (tiedPeaks),
                plot.removeFromTop (11), juce::Justification::topRight);

    // ---- 横轴刻度 ----
    g.setColour (kTextDim);
    g.setFont (numberFont (10.5f));
    g.drawText ("-1000 ms", axisArea, juce::Justification::bottomLeft);
    g.drawText ("0", axisArea, juce::Justification::centredBottom);
    g.drawText ("+1000 ms", axisArea, juce::Justification::bottomRight);
}
