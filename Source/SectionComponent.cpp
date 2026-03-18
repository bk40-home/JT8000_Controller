/*
 * SectionComponent.cpp — JT-8000 Collapsible Section Panel
 *
 * Changes from previous version:
 *   - Added addSlider() for vertical fader controls (ADSR)
 *   - getDesiredHeight() now takes an explicit width parameter to avoid
 *     the getWidth()==0 bug on first layout pass
 *   - Row height adapts to the tallest control in each row instead of
 *     using a fixed 76px, so slider rows can be taller than knob rows
 *   - Combo box label positioning integrated into the flow layout
 */

#include "SectionComponent.h"

// -----------------------------------------------------------------------
// Colour palette
// -----------------------------------------------------------------------
static const juce::Colour kSecBg     (0xff111620);   // Section body background
static const juce::Colour kSecHdrBg  (0xff161c28);   // Header bar background
static const juce::Colour kSecBorder (0xff1e2a3e);   // Section outline
static const juce::Colour kTitleCol  (0xffc8d8f4);   // Header title text
static const juce::Colour kChevronCol(0xff3e5070);   // Expand/collapse chevron

// -----------------------------------------------------------------------
// Constructor
// -----------------------------------------------------------------------
SectionComponent::SectionComponent(const juce::String& title, juce::Colour ledColour)
    : _title(title), _ledColour(ledColour) {}

// -----------------------------------------------------------------------
// addKnob — rotary arc knob for continuous parameters
// -----------------------------------------------------------------------
void SectionComponent::addKnob(juce::AudioProcessorValueTreeState& apvts,
                                const CCParam& param)
{
    auto* knob = _knobs.add(new JT8000Knob(param.name));
    knob->setAttachment(apvts, param.id);
    addAndMakeVisible(knob);
    _controls.push_back({ knob, 58, 64 });
}

// -----------------------------------------------------------------------
// addSlider — vertical fader for ADSR and depth parameters
// -----------------------------------------------------------------------
void SectionComponent::addSlider(juce::AudioProcessorValueTreeState& apvts,
                                  const CCParam& param)
{
    auto* slider = _sliders.add(new JT8000Slider(param.name));
    slider->setAttachment(apvts, param.id);
    addAndMakeVisible(slider);

    // Narrower than knobs (36px) but taller (110px) for fader travel
    _controls.push_back({ slider, 36, 110 });
}

// -----------------------------------------------------------------------
// addComboBox — dropdown list for enum parameters (>2 options)
// -----------------------------------------------------------------------
void SectionComponent::addComboBox(juce::AudioProcessorValueTreeState& apvts,
                                    const CCParam& param)
{
    // Label above the combo box
    auto* label = _labels.add(new juce::Label({}, param.name));
    label->setColour(juce::Label::textColourId, juce::Colour(0xff8090b0));
    label->setFont(juce::FontOptions(9.0f));
    addAndMakeVisible(label);

    auto* combo = _combos.add(new juce::ComboBox());

    // Populate with named options from CCParam
    if (param.enumNames != nullptr && param.enumCount > 0)
    {
        for (int i = 0; i < param.enumCount; ++i)
            combo->addItem(param.enumNames[i], i + 1);   // JUCE IDs are 1-based
    }
    else
    {
        // Fallback: raw 0-127 (should not happen with ControlStyle::Combo)
        for (int v = 0; v <= 127; ++v)
            combo->addItem(juce::String(v), v + 1);
    }

    combo->setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff0b0e14));
    combo->setColour(juce::ComboBox::textColourId,       juce::Colour(0xffc8d8f4));
    combo->setColour(juce::ComboBox::outlineColourId,    juce::Colour(0xff1e2a3e));
    addAndMakeVisible(combo);

    _comboAtts.add(new juce::AudioProcessorValueTreeState::ComboBoxAttachment(
        apvts, param.id, *combo));

    // Combo box gets 120px wide, 40px tall (includes space for label above)
    _controls.push_back({ combo, 120, 40 });
}

// -----------------------------------------------------------------------
// addToggle — on/off checkbox for 2-option enum parameters
// -----------------------------------------------------------------------
void SectionComponent::addToggle(juce::AudioProcessorValueTreeState& apvts,
                                  const CCParam& param)
{
    auto* toggle = _toggles.add(new juce::ToggleButton(param.name));
    toggle->setColour(juce::ToggleButton::textColourId, juce::Colour(0xff8090b0));
    toggle->setColour(juce::ToggleButton::tickColourId, juce::Colour(0xfff08020));
    addAndMakeVisible(toggle);

    _btnAtts.add(new juce::AudioProcessorValueTreeState::ButtonAttachment(
        apvts, param.id, *toggle));

    _controls.push_back({ toggle, 100, 28 });
}

// -----------------------------------------------------------------------
// setOpen — collapse or expand the section body
// -----------------------------------------------------------------------
void SectionComponent::setOpen(bool open)
{
    if (_open == open) return;
    _open = open;

    // Show/hide all controls and their labels
    for (auto& c : _controls)
        c.comp->setVisible(_open);
    for (auto* l : _labels)
        l->setVisible(_open);

    repaint();

    // Notify the editor to relayout all sections
    if (onLayoutChanged)
        onLayoutChanged();
}

// -----------------------------------------------------------------------
// getDesiredHeight — calculate total height for this section
//
// Takes explicit width to avoid the getWidth()==0 bug on first layout.
// Uses a two-pass approach: lay out controls into rows, track the max
// height per row, then sum all row heights.
// -----------------------------------------------------------------------
int SectionComponent::getDesiredHeight(int allocatedWidth) const
{
    if (!_open) return kHeaderHeight;

    const int pad    = 12;
    const int gap    = 6;
    const int availW = juce::jmax(100, allocatedWidth - 2 * pad);

    int x = 0;
    int rowMaxH = 0;
    int totalH  = 0;

    for (const auto& c : _controls)
    {
        // Wrap to next row if this control doesn't fit
        if (x + c.preferredWidth > availW && x > 0)
        {
            totalH += rowMaxH + gap;
            x = 0;
            rowMaxH = 0;
        }

        rowMaxH = juce::jmax(rowMaxH, c.preferredHeight);
        x += c.preferredWidth + gap;
    }

    // Add the last row
    if (!_controls.empty())
        totalH += rowMaxH;

    // Header + top padding + content + bottom padding
    return kHeaderHeight + 10 + totalH + 10;
}

// -----------------------------------------------------------------------
// paint — section background, header bar, LED dot, title, chevron
// -----------------------------------------------------------------------
void SectionComponent::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds();

    // Section body background
    g.setColour(kSecBg);
    g.fillRoundedRectangle(bounds.toFloat(), 4.0f);
    g.setColour(kSecBorder);
    g.drawRoundedRectangle(bounds.toFloat().reduced(0.5f), 4.0f, 1.0f);

    // Header bar
    auto hdr = bounds.removeFromTop(kHeaderHeight);
    g.setColour(kSecHdrBg);
    g.fillRoundedRectangle(hdr.toFloat(), 4.0f);

    // LED dot (coloured indicator in header)
    g.setColour(_ledColour);
    g.fillEllipse(static_cast<float>(hdr.getX()) + 12.0f,
                  static_cast<float>(hdr.getCentreY()) - 4.0f, 8.0f, 8.0f);

    // Title text
    g.setColour(kTitleCol);
    g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
    g.drawText(_title, hdr.getX() + 28, hdr.getY(),
               hdr.getWidth() - 50, hdr.getHeight(),
               juce::Justification::centredLeft);

    // Chevron (▼ when open, ▶ when collapsed)
    g.setColour(kChevronCol);
    g.setFont(juce::FontOptions(14.0f));
    g.drawText(_open ? juce::String(juce::CharPointer_UTF8("\xe2\x96\xbc"))
                     : juce::String(juce::CharPointer_UTF8("\xe2\x96\xb6")),
               hdr.getRight() - 28, hdr.getY(), 20, hdr.getHeight(),
               juce::Justification::centred);
}

// -----------------------------------------------------------------------
// resized — flow layout for all controls
//
// Controls are placed left to right, wrapping to the next row when
// the available width is exceeded.  Each row's Y position advances
// by the maximum control height in that row (so slider rows are
// taller than knob rows).
// -----------------------------------------------------------------------
void SectionComponent::resized()
{
    if (!_open) return;

    const int startY = kHeaderHeight + 10;
    const int pad    = 12;
    const int gap    = 6;
    const int availW = getWidth() - 2 * pad;

    int x       = pad;
    int y       = startY;
    int rowMaxH = 0;

    for (auto& c : _controls)
    {
        // Wrap to next row if this control doesn't fit
        if (x + c.preferredWidth > pad + availW && x > pad)
        {
            x = pad;
            y += rowMaxH + gap;
            rowMaxH = 0;
        }

        c.comp->setBounds(x, y, c.preferredWidth, c.preferredHeight);
        rowMaxH = juce::jmax(rowMaxH, c.preferredHeight);
        x += c.preferredWidth + gap;
    }

    // Position labels above their corresponding combo boxes
    for (int i = 0; i < _combos.size() && i < _labels.size(); ++i)
    {
        auto cb = _combos[i]->getBounds();
        _labels[i]->setBounds(cb.getX(), cb.getY() - 14, cb.getWidth(), 14);
    }
}

// -----------------------------------------------------------------------
// mouseDown — click header to toggle open/closed
// -----------------------------------------------------------------------
void SectionComponent::mouseDown(const juce::MouseEvent& e)
{
    if (e.y < kHeaderHeight)
        setOpen(!_open);
}
