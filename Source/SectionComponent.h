/*
 * SectionComponent.h — JT-8000 Collapsible Section Panel
 *
 * Each section draws a dark rounded rectangle with a coloured header.
 * Click the header to toggle open/closed.
 *
 * Contains knobs, vertical sliders, combo boxes, and toggles in a
 * flow layout that wraps to the next row when the available width
 * is exceeded.
 *
 * Changes from previous version:
 *   - Added addSlider() for JT8000Slider (vertical fader) controls
 *   - Fixed initial height calculation (fallback width when getWidth()==0)
 *   - ControlEntry tracks a row height hint so slider rows are taller
 */

#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "CCDefinitions.h"
#include "KnobComponent.h"
#include "SliderComponent.h"

class SectionComponent : public juce::Component
{
public:
    SectionComponent(const juce::String& title, juce::Colour ledColour);

    // Add controls — call these during buildSections(), one per CCParam
    void addKnob(juce::AudioProcessorValueTreeState& apvts, const CCParam& param);
    void addSlider(juce::AudioProcessorValueTreeState& apvts, const CCParam& param);
    void addComboBox(juce::AudioProcessorValueTreeState& apvts, const CCParam& param);
    void addToggle(juce::AudioProcessorValueTreeState& apvts, const CCParam& param);

    // Collapse / expand
    void setOpen(bool open);
    bool isOpen() const { return _open; }

    // Fired when open/closed state changes so the editor can relayout
    std::function<void()> onLayoutChanged;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;

    // Calculate the height this section needs at a given width.
    // Pass the allocated width explicitly to avoid the getWidth()==0 bug
    // on first layout before resized() has been called.
    int getDesiredHeight(int allocatedWidth) const;

    static constexpr int kHeaderHeight = 32;

private:
    juce::String _title;
    juce::Colour _ledColour;
    bool         _open = true;

    // Owned widget storage — OwnedArray handles lifetime
    juce::OwnedArray<JT8000Knob>      _knobs;
    juce::OwnedArray<JT8000Slider>    _sliders;
    juce::OwnedArray<juce::ComboBox>  _combos;
    juce::OwnedArray<juce::ToggleButton> _toggles;
    juce::OwnedArray<juce::Label>     _labels;

    // APVTS attachment storage — must outlive the widgets
    juce::OwnedArray<juce::AudioProcessorValueTreeState::ComboBoxAttachment> _comboAtts;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::ButtonAttachment>   _btnAtts;

    // Flow layout entry — one per control added to this section
    struct ControlEntry
    {
        juce::Component* comp;
        int preferredWidth;
        int preferredHeight;
    };
    std::vector<ControlEntry> _controls;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SectionComponent)
};
