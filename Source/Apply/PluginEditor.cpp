/*
    AutoDelayMeasure / AutoDelayApply —— RVC 实时变声「延迟对齐」VST3 插件
    文件：Source/Apply/PluginEditor.cpp

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

namespace
{
    const juce::Colour kBackground { 0xff1c1e22 };
    const juce::Colour kPanel      { 0xff24272d };
    const juce::Colour kTextDim    { 0xff8b919c };
    const juce::Colour kTextBright { 0xffe8ebf0 };
    const juce::Colour kAccent     { 0xff4fd0a0 };
    const juce::Colour kWarn       { 0xffe0b050 };

    /** Formats "how many seconds ago" into a short, readable phrase. */
    juce::String ageText (double seconds)
    {
        if (seconds < 0.0)
            return "just now";

        if (seconds < 60.0)
            return juce::String (juce::roundToInt (seconds)) + " s ago";

        if (seconds < 3600.0)
            return juce::String (juce::roundToInt (seconds / 60.0)) + " min ago";

        return juce::String (juce::roundToInt (seconds / 3600.0)) + " h ago";
    }
}

AutoDelayApplyAudioProcessorEditor::AutoDelayApplyAudioProcessorEditor (
        AutoDelayApplyAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    // ---- Link toggle ----
    addAndMakeVisible (linkButton);
    linkButton.setColour (juce::ToggleButton::textColourId, kTextBright);
    linkButton.setColour (juce::ToggleButton::tickColourId, kAccent);
    linkButton.setColour (juce::ToggleButton::tickDisabledColourId, kTextDim);

    // ---- Offset trim ----
    addAndMakeVisible (offsetLabel);
    offsetLabel.setText ("Offset", juce::dontSendNotification);
    offsetLabel.setJustificationType (juce::Justification::centredLeft);
    offsetLabel.setColour (juce::Label::textColourId, kTextDim);
    offsetLabel.setFont (uiFont (12.5f));

    addAndMakeVisible (offsetSlider);
    offsetSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    offsetSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 66, 22);
    offsetSlider.setTextValueSuffix (" ms");
    offsetSlider.setColour (juce::Slider::thumbColourId, kTextBright);
    offsetSlider.setColour (juce::Slider::trackColourId, kAccent);
    offsetSlider.setColour (juce::Slider::backgroundColourId, juce::Colour (0xff33383f));
    offsetSlider.setColour (juce::Slider::textBoxTextColourId, kTextBright);
    offsetSlider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    offsetSlider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colour (0xff2c3036));

    // ---- Delay main slider ----
    addAndMakeVisible (delayLabel);
    delayLabel.setText ("Delay", juce::dontSendNotification);
    delayLabel.setJustificationType (juce::Justification::centredLeft);
    delayLabel.setColour (juce::Label::textColourId, kTextDim);
    delayLabel.setFont (uiFont (12.5f));

    addAndMakeVisible (delaySlider);
    delaySlider.setSliderStyle (juce::Slider::LinearHorizontal);
    delaySlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 76, 22);
    delaySlider.setTextValueSuffix (" ms");
    delaySlider.setColour (juce::Slider::thumbColourId, kTextBright);
    delaySlider.setColour (juce::Slider::trackColourId, kAccent);
    delaySlider.setColour (juce::Slider::backgroundColourId, juce::Colour (0xff33383f));
    delaySlider.setColour (juce::Slider::textBoxTextColourId, kTextBright);
    delaySlider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    delaySlider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colour (0xff2c3036));

    // ---- Reset: zero both values ----
    addAndMakeVisible (resetButton);
    resetButton.onClick = [this]
    {
        for (auto* id : { "delay", "offset" })
            if (auto* param = processor.apvts.getParameter (id))
                param->setValueNotifyingHost (param->convertTo0to1 (0.0f));
    };

    setSize (520, 350);
    startTimerHz (10);   // only refreshes the "N s ago" text; 10Hz is plenty
}

AutoDelayApplyAudioProcessorEditor::~AutoDelayApplyAudioProcessorEditor()
{
    stopTimer();
}

//==============================================================================
juce::Font AutoDelayApplyAudioProcessorEditor::uiFont (float height, int style)
{
    // UI is all English; the system default sans-serif is enough.
    return juce::Font (juce::FontOptions (height, style));
}

juce::Font AutoDelayApplyAudioProcessorEditor::numberFont (float height, int style)
{
    return juce::Font (juce::FontOptions ("Consolas", height, style));
}

double AutoDelayApplyAudioProcessorEditor::appliedDelayMs() const
{
    const auto* d = processor.apvts.getRawParameterValue ("delay");
    const auto* o = processor.apvts.getRawParameterValue ("offset");

    const double dm = d != nullptr ? (double) d->load() : 0.0;
    const double om = o != nullptr ? (double) o->load() : 0.0;

    return juce::jlimit (0.0, AutoDelayApplyAudioProcessor::kMaxDelayMs, dm + om);
}

void AutoDelayApplyAudioProcessorEditor::timerCallback()
{
    repaint();
}

//==============================================================================
void AutoDelayApplyAudioProcessorEditor::resized()
{
    auto r = getLocalBounds().reduced (16);

    r.removeFromTop (24);                       // title

    readoutArea = r.removeFromTop (104);
    r.removeFromTop (10);

    // ---- Link toggle + Offset ----
    linkRow = r.removeFromTop (28);
    linkButton.setBounds (linkRow.removeFromLeft (84).withSizeKeepingCentre (84, 24));

    auto offsetArea = linkRow.removeFromRight (300);
    offsetLabel.setBounds (offsetArea.removeFromLeft (48));
    offsetSlider.setBounds (offsetArea);

    r.removeFromTop (10);

    // ---- Delay main slider ----
    delayRow = r.removeFromTop (56);
    delayLabel.setBounds (delayRow.removeFromLeft (48).withSizeKeepingCentre (48, 22));
    delaySlider.setBounds (delayRow.withSizeKeepingCentre (delayRow.getWidth(), 36));

    r.removeFromTop (10);

    bottomRow = r.removeFromTop (38);
    resetButton.setBounds (bottomRow.removeFromRight (96).withSizeKeepingCentre (96, 28));

    r.removeFromTop (6);
    footerArea = r.removeFromTop (22);
}

void AutoDelayApplyAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBackground);

    // ---- Title ----
    g.setColour (kTextDim);
    g.setFont (uiFont (13.0f));
    g.drawText ("AutoDelayApply",
                getLocalBounds().reduced (16).removeFromTop (24),
                juce::Justification::centredLeft);

    // ---- 版权/许可声明（标题行右侧；纯 ASCII，不会乱码）----
    g.setColour (juce::Colour (0xff5c6169));
    g.setFont (uiFont (11.5f));
    g.drawText ("AGPL-3.0 (C) 2026 ChenFeng",
                getLocalBounds().reduced (16).removeFromTop (24),
                juce::Justification::centredRight);

    paintReadout (g);

    // ---- Status line ----
    const auto link   = processor.getLinkStatus();
    const bool linkOn = processor.isLinkEnabled();

    juce::String status;
    juce::Colour colour;

    if (! linkOn)
    {
        status = "Link off - manual";
        colour = kTextDim;
    }
    else if (link.valueReceived)
    {
        status = "Linked - receiving the value from AutoDelayMeasure";
        colour = kAccent;
    }
    else
    {
        status = "No value yet - click Send in AutoDelayMeasure";
        colour = kWarn;
    }

    g.setColour (colour);
    g.setFont (uiFont (13.0f));
    g.drawFittedText (status, bottomRow, juce::Justification::centredLeft, 2);

    // ---- Footer note (avoid double compensation) ----
    g.setColour (kTextDim);
    g.setFont (uiFont (11.5f));
    g.drawText ("Does not report delay - keep this track's Manual Delay at 0.",
                footerArea, juce::Justification::centredLeft);
}

void AutoDelayApplyAudioProcessorEditor::paintReadout (juce::Graphics& g)
{
    g.setColour (kPanel);
    g.fillRoundedRectangle (readoutArea.toFloat(), 6.0f);

    auto inner = readoutArea.reduced (18, 12);

    g.setColour (kTextDim);
    g.setFont (uiFont (12.5f));
    g.drawText ("Applied delay", inner.removeFromTop (16), juce::Justification::topLeft);

    // ---- Big readout ----
    auto bigArea = inner.removeFromTop (60);
    const auto bigFont = numberFont (46.0f, juce::Font::bold);

    g.setColour (kTextBright);
    g.setFont (bigFont);

    const juce::String bigText = juce::String (appliedDelayMs(), 1);
    g.drawText (bigText, bigArea, juce::Justification::centredLeft);

    const int textWidth = juce::roundToInt (
        juce::GlyphArrangement::getStringWidth (bigFont, bigText));

    g.setColour (kTextDim);
    g.setFont (uiFont (16.0f));
    g.drawText ("ms", bigArea.withTrimmedLeft (textWidth + 8),
                juce::Justification::centredLeft);

    // ---- Detail line: where the link came from and how long ago ----
    const auto link   = processor.getLinkStatus();
    const bool linkOn = processor.isLinkEnabled();

    juce::String detail;

    if (! linkOn)
        detail << "Link off - manual value";
    else if (link.valueReceived)
        detail << "Linked - received " << ageText (link.ageSeconds)
               << "    " << juce::String (link.valueMs, 1) << " ms";
    else
        detail << "Waiting for a value";

    g.setColour (kTextDim);
    g.setFont (numberFont (13.0f));
    g.drawText (detail, inner, juce::Justification::topLeft);
}
