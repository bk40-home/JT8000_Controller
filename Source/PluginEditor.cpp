/*
 * PluginEditor.cpp — JT-8000 Controller Main Window
 *
 * Changes from previous version:
 *   - buildSections() now routes ControlStyle::Slider to addSlider()
 *   - layoutSections() implements multi-column row grouping:
 *     sections that share the same sectionRow[] value are placed
 *     side-by-side, each getting an equal share of the available width.
 *     Uses a two-pass approach: set widths first, then query heights.
 *   - Underscore prefix on all private members
 */

#include "PluginEditor.h"

// -----------------------------------------------------------------------
// Colour palette
// -----------------------------------------------------------------------
static const juce::Colour kBg       (0xff0b0e14);   // Window background
static const juce::Colour kHeaderBg (0xff141a28);   // Header bar background
static const juce::Colour kBorder   (0xff1e2a3e);   // Header separator line
static const juce::Colour kOrange   (0xfff08020);   // Accent colour
static const juce::Colour kTextHi   (0xffc8d8f4);   // Bright text
static const juce::Colour kText     (0xff8090b0);   // Subdued text

// -----------------------------------------------------------------------
// Section LED colours — one per section category
// -----------------------------------------------------------------------
static juce::Colour sectionLed(Section s)
{
    switch (s)
    {
        case Section::OSC1:
        case Section::OSC2:        return juce::Colour(0xfff08020u);   // Orange
        case Section::MIXER:       return juce::Colour(0xffc0c8d8u);   // Silver
        case Section::FILTER:      return juce::Colour(0xff40a0ffu);   // Blue
        case Section::AMP_ENV:     return juce::Colour(0xff28ff58u);   // Green
        case Section::FILTER_ENV:  return juce::Colour(0xff40a0ffu);   // Blue
        case Section::PITCH_ENV:
        case Section::VELOCITY:    return juce::Colour(0xff9040ffu);   // Purple
        case Section::LFO1:
        case Section::LFO2:        return juce::Colour(0xfff08020u);   // Orange
        case Section::FX_EQ:
        case Section::FX_MOD:
        case Section::FX_MOD_TUNE:
        case Section::FX_DELAY:
        case Section::FX_REVERB:
        case Section::FX_OUTPUT:   return juce::Colour(0xffff2828u);   // Red
        case Section::STEP_SEQ:    return juce::Colour(0xff96fc00u);   // Lime green
        default:                   return juce::Colour(0xffc0c8d8u);   // Silver
    }
}

// -----------------------------------------------------------------------
// Constructor — build header bar, viewport, and all sections
// -----------------------------------------------------------------------
JT8000ControllerEditor::JT8000ControllerEditor(JT8000ControllerProcessor& p)
    : AudioProcessorEditor(p), _proc(p)
{
    // --- Title ---
    _titleLabel.setText("JT-8000", juce::dontSendNotification);
    _titleLabel.setColour(juce::Label::textColourId, kOrange);
    _titleLabel.setFont(juce::FontOptions(18.0f, juce::Font::bold));
    addAndMakeVisible(_titleLabel);

    // --- MIDI channel selector (1-16) ---
    for (int i = 1; i <= 16; ++i)
        _midiChSelector.addItem("Ch " + juce::String(i), i);
    _midiChSelector.setSelectedId(_proc.midiChannel.load());
    _midiChSelector.onChange = [this] {
        _proc.midiChannel.store(_midiChSelector.getSelectedId());
    };
    _midiChSelector.setColour(juce::ComboBox::backgroundColourId, kBg);
    _midiChSelector.setColour(juce::ComboBox::textColourId, kTextHi);
    _midiChSelector.setColour(juce::ComboBox::outlineColourId, kBorder);
    addAndMakeVisible(_midiChSelector);

    // --- MIDI output device selector ---
    _midiOutSelector.setColour(juce::ComboBox::backgroundColourId, kBg);
    _midiOutSelector.setColour(juce::ComboBox::textColourId, kTextHi);
    _midiOutSelector.setColour(juce::ComboBox::outlineColourId, kBorder);
    _midiOutSelector.onChange = [this] {
        const int sel = _midiOutSelector.getSelectedId();
        if (sel <= 0)
        {
            _proc.setMidiOutput(nullptr);
            return;
        }
        auto devices = juce::MidiOutput::getAvailableDevices();
        const int idx = sel - 1;
        if (idx >= 0 && idx < devices.size())
        {
            auto out = juce::MidiOutput::openDevice(devices[idx].identifier);
            _proc.setMidiOutput(std::move(out));
        }
    };
    addAndMakeVisible(_midiOutSelector);
    refreshMidiOutputList();

    // --- Toolbar buttons ---
    auto styleButton = [](juce::TextButton& b, juce::Colour c) {
        b.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff161c28));
        b.setColour(juce::TextButton::textColourOffId, c);
    };
    styleButton(_sendAllBtn,  kOrange);
    styleButton(_expandBtn,   kText);
    styleButton(_collapseBtn, kText);

    _sendAllBtn.onClick  = [this] { _proc.sendAllCCs(); };
    _expandBtn.onClick   = [this] { for (auto* s : _sections) s->setOpen(true); };
    _collapseBtn.onClick = [this] { for (auto* s : _sections) s->setOpen(false); };

    addAndMakeVisible(_sendAllBtn);
    addAndMakeVisible(_expandBtn);
    addAndMakeVisible(_collapseBtn);

    // --- Scrollable viewport ---
    _viewport.setViewedComponent(&_contentPanel, false);
    _viewport.setScrollBarsShown(true, false);
    _viewport.getVerticalScrollBar().setColour(
        juce::ScrollBar::thumbColourId, juce::Colour(0xff283850));
    addAndMakeVisible(_viewport);

    // --- Build and populate all sections ---
    buildSections();

    setSize(1080, 820);
    setResizable(true, true);
    setResizeLimits(400, 400, 1600, 2400);
}

// -----------------------------------------------------------------------
// refreshMidiOutputList — populate the MIDI output dropdown
// -----------------------------------------------------------------------
void JT8000ControllerEditor::refreshMidiOutputList()
{
    _midiOutSelector.clear(juce::dontSendNotification);
    _midiOutSelector.addItem(juce::String::fromUTF8("— No MIDI Output —"), -1);

    auto devices = juce::MidiOutput::getAvailableDevices();
    for (int i = 0; i < devices.size(); ++i)
        _midiOutSelector.addItem(devices[i].name, i + 1);

    _midiOutSelector.setSelectedId(-1, juce::dontSendNotification);
}

// -----------------------------------------------------------------------
// buildSections — create one SectionComponent per Section enum value,
// then populate each with the appropriate controls from kAllParams[]
// -----------------------------------------------------------------------
void JT8000ControllerEditor::buildSections()
{
    // Create empty sections
    for (int s = 0; s < static_cast<int>(Section::NUM_SECTIONS); ++s)
    {
        auto* sec = _sections.add(
            new SectionComponent(sectionNames[s], sectionLed(static_cast<Section>(s))));

        // Wire relayout callback so collapse/expand triggers a full relayout
        sec->onLayoutChanged = [this] { layoutSections(); };

        _contentPanel.addAndMakeVisible(sec);
    }

    // Populate sections from the master parameter table
    for (int i = 0; i < kNumParams; ++i)
    {
        const auto& p = kAllParams[i];
        auto* sec = _sections[static_cast<int>(p.section)];

        switch (p.style)
        {
            case ControlStyle::Knob:
                sec->addKnob(_proc.apvts, p);
                break;

            case ControlStyle::Slider:
                sec->addSlider(_proc.apvts, p);
                break;

            case ControlStyle::Combo:
                sec->addComboBox(_proc.apvts, p);
                break;

            case ControlStyle::Toggle:
                sec->addToggle(_proc.apvts, p);
                break;
        }
    }
}

// -----------------------------------------------------------------------
// layoutSections — arrange sections in the content panel
//
// Multi-column row grouping:
//   Sections that share the same sectionRow[] value (>= 0) are placed
//   side-by-side on the same row, dividing the available width equally.
//   Sections with sectionRow == -1 get their own full-width row.
//
// Two-pass approach:
//   Pass 1: Set each section's width (needed before getDesiredHeight)
//   Pass 2: Query heights and set final bounds
// -----------------------------------------------------------------------
void JT8000ControllerEditor::layoutSections()
{
    const int scrollBarW = _viewport.getScrollBarThickness();
    const int totalW     = _viewport.getWidth() - scrollBarW - 8;
    const int gap        = 4;

    if (totalW <= 0) return;

    int y = gap;
    int idx = 0;
    const int numSections = _sections.size();

    while (idx < numSections)
    {
        const int row = sectionRow[idx];

        if (row < 0)
        {
            // --- Single section on its own row ---
            auto* sec = _sections[idx];
            const int h = sec->getDesiredHeight(totalW);
            sec->setBounds(gap, y, totalW, h);
            y += h + gap;
            ++idx;
        }
        else
        {
            // --- Collect all consecutive sections sharing this row number ---
            int groupStart = idx;
            while (idx < numSections && sectionRow[idx] == row)
                ++idx;
            int groupCount = idx - groupStart;

            // Calculate width per section (equal share minus gaps)
            int sectionW = (totalW - (groupCount - 1) * gap) / groupCount;

            // Find the tallest section in this row
            int maxH = 0;
            for (int g = 0; g < groupCount; ++g)
            {
                auto* sec = _sections[groupStart + g];
                maxH = juce::jmax(maxH, sec->getDesiredHeight(sectionW));
            }

            // Place all sections in this row side-by-side at the max height
            int x = gap;
            for (int g = 0; g < groupCount; ++g)
            {
                auto* sec = _sections[groupStart + g];
                sec->setBounds(x, y, sectionW, maxH);
                x += sectionW + gap;
            }

            y += maxH + gap;
        }
    }

    _contentPanel.setSize(totalW + 2 * gap, y + gap);
}

// -----------------------------------------------------------------------
// paint — window background and header bar
// -----------------------------------------------------------------------
void JT8000ControllerEditor::paint(juce::Graphics& g)
{
    g.fillAll(kBg);
    g.setColour(kHeaderBg);
    g.fillRect(0, 0, getWidth(), kHeaderHeight);
    g.setColour(kBorder);
    g.drawHorizontalLine(kHeaderHeight - 1, 0.0f, static_cast<float>(getWidth()));
}

// -----------------------------------------------------------------------
// resized — position header widgets and viewport
// -----------------------------------------------------------------------
void JT8000ControllerEditor::resized()
{
    auto area = getLocalBounds();
    auto hdr  = area.removeFromTop(kHeaderHeight).reduced(6, 6);

    _titleLabel.setBounds(hdr.removeFromLeft(90));
    _collapseBtn.setBounds(hdr.removeFromRight(66));
    _expandBtn.setBounds(hdr.removeFromRight(60));
    _sendAllBtn.setBounds(hdr.removeFromRight(66));
    _midiChSelector.setBounds(hdr.removeFromRight(60));
    _midiOutSelector.setBounds(hdr.removeFromRight(juce::jmin(180, hdr.getWidth())));

    _viewport.setBounds(area);
    layoutSections();
}
