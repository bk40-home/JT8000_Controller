/*
 * SectionComponent.cpp — JT-8000 Collapsible Section Panel
 *
 * All controls are standard JUCE widgets styled by JT8000LookAndFeel.
 * Knobs: juce::Slider(RotaryHorizontalVerticalDrag) + setRotaryParameters()
 * Faders: juce::Slider(LinearVertical)
 * Combos: juce::ComboBox + AudioParameterChoice via ComboBoxAttachment
 * Toggles: juce::ToggleButton + AudioParameterChoice via ComboBoxAttachment
 */

#include "SectionComponent.h"

// -----------------------------------------------------------------------
// Colour palette
// -----------------------------------------------------------------------
static const juce::Colour kSecBg     (0xff111620);
static const juce::Colour kSecHdrBg  (0xff161c28);
static const juce::Colour kSecBorder (0xff1e2a3e);
static const juce::Colour kTitleCol  (0xffc8d8f4);
static const juce::Colour kChevronCol(0xff3e5070);
static const juce::Colour kLabelCol  (0xff8090b0);

// -----------------------------------------------------------------------
// Constructor
// -----------------------------------------------------------------------
SectionComponent::SectionComponent(const juce::String& title, juce::Colour ledColour,
                                   JT8000LookAndFeel& lnf)
    : _title(title), _ledColour(ledColour), _lnf(lnf) {}

// -----------------------------------------------------------------------
// addKnob — rotary arc knob using standard juce::Slider
// -----------------------------------------------------------------------
void SectionComponent::addKnob(juce::AudioProcessorValueTreeState& apvts,
                                const CCParam& param)
{
    auto* slider = _sliders.add(new juce::Slider());
    slider->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider->setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    slider->setRotaryParameters(JT8000LookAndFeel::kRotaryStart,
                                JT8000LookAndFeel::kRotaryEnd, true);
    slider->setRange(0, 127, 1);
    slider->setDoubleClickReturnValue(true, 64.0);
    slider->setLookAndFeel(&_lnf);
    addAndMakeVisible(slider);

    _sliderAtts.add(new juce::AudioProcessorValueTreeState::SliderAttachment(
        apvts, param.id, *slider));

    // Label below knob
    auto* label = _labels.add(new juce::Label({}, param.name));
    label->setFont(juce::FontOptions(9.0f));
    label->setColour(juce::Label::textColourId, kLabelCol);
    label->setJustificationType(juce::Justification::centred);
    addAndMakeVisible(label);

    _controls.push_back({ slider, 58, 52, label, false });
}

// -----------------------------------------------------------------------
// addSlider — vertical fader using standard juce::Slider
// -----------------------------------------------------------------------
void SectionComponent::addSlider(juce::AudioProcessorValueTreeState& apvts,
                                  const CCParam& param)
{
    auto* slider = _sliders.add(new juce::Slider());
    slider->setSliderStyle(juce::Slider::LinearVertical);
    slider->setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    slider->setRange(0, 127, 1);
    slider->setDoubleClickReturnValue(true, 0.0);
    slider->setLookAndFeel(&_lnf);
    addAndMakeVisible(slider);

    _sliderAtts.add(new juce::AudioProcessorValueTreeState::SliderAttachment(
        apvts, param.id, *slider));

    // Label below fader
    auto* label = _labels.add(new juce::Label({}, param.name));
    label->setFont(juce::FontOptions(9.0f));
    label->setColour(juce::Label::textColourId, kLabelCol);
    label->setJustificationType(juce::Justification::centred);
    addAndMakeVisible(label);

    _controls.push_back({ slider, 36, 96, label, false });
}

// -----------------------------------------------------------------------
// addComboBox — dropdown for AudioParameterChoice params
// -----------------------------------------------------------------------
void SectionComponent::addComboBox(juce::AudioProcessorValueTreeState& apvts,
                                    const CCParam& param)
{
    // Label above combo
    auto* label = _labels.add(new juce::Label({}, param.name));
    label->setFont(juce::FontOptions(9.0f));
    label->setColour(juce::Label::textColourId, kLabelCol);
    addAndMakeVisible(label);

    auto* combo = _combos.add(new juce::ComboBox());

    // Populate items from CCParam enum names
    if (param.enumNames != nullptr && param.enumCount > 0)
        for (int i = 0; i < param.enumCount; ++i)
            combo->addItem(param.enumNames[i], i + 1);

    combo->setLookAndFeel(&_lnf);
    addAndMakeVisible(combo);

    // ComboBoxAttachment on an AudioParameterChoice gives 1:1 index mapping
    _comboAtts.add(new juce::AudioProcessorValueTreeState::ComboBoxAttachment(
        apvts, param.id, *combo));

    _controls.push_back({ combo, 120, 40, label, true });
}

// -----------------------------------------------------------------------
// addToggle — on/off using ToggleButton bound to AudioParameterChoice
//
// AudioParameterChoice("Off","On") with index 0=Off, 1=On.
// ToggleButton doesn't have a direct ChoiceAttachment, so we use
// a ButtonAttachment which maps bool ↔ the parameter's normalised value.
// For a 2-choice parameter: normalised < 0.5 = off (index 0), >= 0.5 = on (index 1).
// -----------------------------------------------------------------------
void SectionComponent::addToggle(juce::AudioProcessorValueTreeState& apvts,
                                  const CCParam& param)
{
    auto* toggle = _toggles.add(new juce::ToggleButton(param.name));
    toggle->setLookAndFeel(&_lnf);
    addAndMakeVisible(toggle);

    _btnAtts.add(new juce::AudioProcessorValueTreeState::ButtonAttachment(
        apvts, param.id, *toggle));

    _controls.push_back({ toggle, 100, 28, nullptr, false });
}

// -----------------------------------------------------------------------
// addStepGrid — 16-bar step sequencer grid
//
// The grid sends CC pairs (SEQ_STEP_SELECT + SEQ_STEP_VALUE) directly
// via the provided callback, bypassing APVTS because the firmware
// uses a two-CC select-then-value protocol that does not map to a
// single automatable parameter.
// -----------------------------------------------------------------------
void SectionComponent::addStepGrid(std::function<void(int,int)> sendCCPair)
{
    auto* grid = _stepGrids.add(new StepSequencerGrid());
    grid->onStepChanged = std::move(sendCCPair);
    addAndMakeVisible(grid);

    // Full width row, tall enough for 16 bars + step number labels
    _controls.push_back({ grid, 580, 100, nullptr, false });
}

// -----------------------------------------------------------------------
// addEnvelope — visual ADSR editor with draggable dots
// -----------------------------------------------------------------------
void SectionComponent::addEnvelope(juce::AudioProcessorValueTreeState& apvts,
                                   JuceEnvFlavour flavour)
{
    auto* env = _envelopes.add(new EnvelopeDisplay(apvts, flavour));
    addAndMakeVisible(env);

    // Full width, 140px tall (room for value labels + shape + param labels)
    _controls.push_back({ env, 580, 140, nullptr, false });
}

// -----------------------------------------------------------------------
// setOpen
// -----------------------------------------------------------------------
void SectionComponent::setOpen(bool open)
{
    if (_open == open) return;
    _open = open;

    for (auto& c : _controls)
    {
        c.comp->setVisible(_open);
        if (c.label) c.label->setVisible(_open);
    }

    repaint();
    if (onLayoutChanged) onLayoutChanged();
}

// -----------------------------------------------------------------------
// getDesiredHeight
// -----------------------------------------------------------------------
int SectionComponent::getDesiredHeight(int allocatedWidth) const
{
    if (!_open) return kHeaderHeight;

    const int pad = 12, gap = 6;
    const int availW = juce::jmax(100, allocatedWidth - 2 * pad);
    const int labelH = 14;

    int xPos = 0, rowMaxH = 0, totalH = 0;

    for (const auto& c : _controls)
    {
        int itemH = c.preferredHeight + (c.label ? labelH : 0);

        if (xPos + c.preferredWidth > availW && xPos > 0)
        {
            totalH += rowMaxH + gap;
            xPos = 0;
            rowMaxH = 0;
        }
        rowMaxH = juce::jmax(rowMaxH, itemH);
        xPos += c.preferredWidth + gap;
    }
    if (!_controls.empty()) totalH += rowMaxH;

    return kHeaderHeight + 10 + totalH + 10;
}

// -----------------------------------------------------------------------
// paint — section background, header bar, LED dot, title, chevron
// -----------------------------------------------------------------------
void SectionComponent::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds();

    g.setColour(kSecBg);
    g.fillRoundedRectangle(bounds.toFloat(), 4.0f);
    g.setColour(kSecBorder);
    g.drawRoundedRectangle(bounds.toFloat().reduced(0.5f), 4.0f, 1.0f);

    auto hdr = bounds.removeFromTop(kHeaderHeight);
    g.setColour(kSecHdrBg);
    g.fillRoundedRectangle(hdr.toFloat(), 4.0f);

    g.setColour(_ledColour);
    g.fillEllipse(static_cast<float>(hdr.getX()) + 12.0f,
                  static_cast<float>(hdr.getCentreY()) - 4.0f, 8.0f, 8.0f);

    g.setColour(kTitleCol);
    g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
    g.drawText(_title, hdr.getX() + 28, hdr.getY(),
               hdr.getWidth() - 50, hdr.getHeight(),
               juce::Justification::centredLeft);

    g.setColour(kChevronCol);
    g.setFont(juce::FontOptions(14.0f));
    g.drawText(_open ? juce::String(juce::CharPointer_UTF8("\xe2\x96\xbc"))
                     : juce::String(juce::CharPointer_UTF8("\xe2\x96\xb6")),
               hdr.getRight() - 28, hdr.getY(), 20, hdr.getHeight(),
               juce::Justification::centred);
}

// -----------------------------------------------------------------------
// resized — flow layout with labels
// -----------------------------------------------------------------------
void SectionComponent::resized()
{
    if (!_open) return;

    const int startY = kHeaderHeight + 10;
    const int pad = 12, gap = 6;
    const int availW = getWidth() - 2 * pad;
    const int labelH = 14;

    int xPos = pad, yPos = startY, rowMaxH = 0;

    for (auto& c : _controls)
    {
        int itemH = c.preferredHeight + (c.label ? labelH : 0);

        if (xPos + c.preferredWidth > pad + availW && xPos > pad)
        {
            xPos = pad;
            yPos += rowMaxH + gap;
            rowMaxH = 0;
        }

        if (c.label && c.labelAbove)
        {
            // Label above (combos): label first, then widget
            c.label->setBounds(xPos, yPos, c.preferredWidth, labelH);
            c.comp->setBounds(xPos, yPos + labelH, c.preferredWidth, c.preferredHeight);
        }
        else
        {
            // Widget first, label below (knobs/sliders)
            c.comp->setBounds(xPos, yPos, c.preferredWidth, c.preferredHeight);
            if (c.label)
                c.label->setBounds(xPos, yPos + c.preferredHeight, c.preferredWidth, labelH);
        }

        rowMaxH = juce::jmax(rowMaxH, itemH);
        xPos += c.preferredWidth + gap;
    }
}

// -----------------------------------------------------------------------
// mouseDown — click header to toggle
// -----------------------------------------------------------------------
void SectionComponent::mouseDown(const juce::MouseEvent& e)
{
    if (e.y < kHeaderHeight)
        setOpen(!_open);
}
