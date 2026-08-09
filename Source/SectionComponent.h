/*
 * SectionComponent.h — JT-8000 Collapsible Section Panel
 *
 * Each section draws a dark rounded rectangle with a coloured header.
 * Click the header to toggle open/closed.
 *
 * Controls are plain JUCE widgets (Slider, ComboBox, ToggleButton) styled
 * via JT8000LookAndFeel.  No custom widget subclasses needed:
 *   - Knobs:   juce::Slider with RotaryHorizontalVerticalDrag + setRotaryParameters
 *   - Faders:  juce::Slider with LinearVertical
 *   - Labels:  managed as paired components (below knobs, above combos)
 */

#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "CCDefinitions.h"
#include "JT8000LookAndFeel.h"
#include "StepSequencerGrid.h"
#include "ArpPatternGrid.h"
#include "EnvelopeDisplay.h"

class SectionComponent : public juce::Component
{
public:
    SectionComponent(const juce::String& title, juce::Colour ledColour,
                     JT8000LookAndFeel& lnf);

    // Add controls — call during buildSections(), one per CCParam
    void addKnob(juce::AudioProcessorValueTreeState& apvts, const CCParam& param);
    void addSlider(juce::AudioProcessorValueTreeState& apvts, const CCParam& param);
    void addComboBox(juce::AudioProcessorValueTreeState& apvts, const CCParam& param);
    void addToggle(juce::AudioProcessorValueTreeState& apvts, const CCParam& param);
    void addStepGrid(std::function<void(int,int)> sendCCPair);
    void addAuxStepGrid(std::function<void(int,int)> sendAuxPair);   // Stage B/C/D aux lane
    // Phase 9: bespoke tri-lane arp pattern grid.  The callback carries the
    // lane index (0=OnOff,1=Accent,2=Ratchet) alongside step + wire value.
    void addArpGrid(std::function<void(int, int, int)> sendLaneEdit);

    // Raw controls for v2-only params with no CC/APVTS binding (the seq aux
    // dest/depth — Option B).  Value goes out via the callback, not an
    // attachment.  addRawCombo: enum select, callback gets the 0-based index.
    // addRawKnob: 0..127 rotary, callback gets the raw 7-bit value.
    void addRawCombo(const juce::String& name,
                     const char* const* enumNames, int enumCount,
                     int initialIndex,
                     std::function<void(int)> onChange);
    void addRawKnob(const juce::String& name, int initialValue,
                    std::function<void(int)> onChange);
    void addEnvelope(juce::AudioProcessorValueTreeState& apvts,
                     JuceEnvFlavour flavour);

    /* Access step grids for Send All */
    const juce::OwnedArray<StepSequencerGrid>& getStepGrids() const { return _stepGrids; }
    const juce::OwnedArray<ArpPatternGrid>&    getArpGrids()  const { return _arpGrids; }

    void setOpen(bool open);
    bool isOpen() const { return _open; }

    // Fired when open/closed state changes so the editor can relayout
    std::function<void()> onLayoutChanged;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;

    // Height this section needs at a given width (explicit to avoid getWidth()==0 on first layout)
    int getDesiredHeight(int allocatedWidth) const;

    static constexpr int kHeaderHeight = 32;

private:
    juce::String _title;
    juce::Colour _ledColour;
    bool         _open = true;
    JT8000LookAndFeel& _lnf;

    // Owned widgets and attachments
    juce::OwnedArray<juce::Slider>        _sliders;
    juce::OwnedArray<juce::ComboBox>      _combos;
    juce::OwnedArray<juce::ToggleButton>  _toggles;
    juce::OwnedArray<juce::Label>         _labels;
    juce::OwnedArray<StepSequencerGrid>   _stepGrids;
    juce::OwnedArray<ArpPatternGrid>      _arpGrids;
    juce::OwnedArray<EnvelopeDisplay>      _envelopes;

    // Attachments — must outlive the widgets they bind
    juce::OwnedArray<juce::AudioProcessorValueTreeState::SliderAttachment>   _sliderAtts;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::ComboBoxAttachment> _comboAtts;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::ButtonAttachment>   _btnAtts;

    // Flow layout entry
    struct ControlEntry
    {
        juce::Component* comp;
        int preferredWidth;
        int preferredHeight;
        juce::Label* label;    // Associated label (below knobs, above combos), or nullptr
        bool labelAbove;       // true = label above (combos), false = label below (knobs/sliders)
    };
    std::vector<ControlEntry> _controls;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SectionComponent)
};
