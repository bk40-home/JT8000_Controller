/*
 * JtRmlBinding.h — connects the RmlUi panel to the APVTS
 *
 * The panel deliberately knows nothing about parameters: every control reports
 * a normalised 0..1 value and an opaque key. This class is the only place that
 * knows both sides, which is what keeps the widgets reusable and the parameter
 * model free of layout concerns.
 *
 * WHAT IT DOES
 *   - control moved      -> APVTS, via the parameter's normalised range
 *   - APVTS changed      -> control, WITHOUT echoing back out
 *   - pointer over a control -> LCD shows its name and formatted value
 *   - layer buttons and SHIFT -> view switch on the component
 *
 * THE ECHO PROBLEM
 *   A naive binding writes the APVTS on every control move and pushes every
 *   APVTS change back to the control. With automation running, the two fight:
 *   the host writes a value, the UI writes it back a frame later, and knob
 *   movements stutter or snap back. Two guards prevent it here - the component's
 *   setParameterValue() takes notify=false, and m_pushing suppresses the
 *   outbound path while an inbound update is being applied.
 *
 * THREADING
 *   parameterChanged() arrives on whatever thread the host uses - often the
 *   audio thread. It must not touch the UI. Values are therefore parked in a
 *   lock-free slot and applied on the message thread by the timer, which is the
 *   same discipline the firmware uses for its NRPN handoff.
 */

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace jt
{

class JtRmlComponent;

class JtRmlBinding : private juce::AudioProcessorValueTreeState::Listener,
                     private juce::Timer
{
public:
    JtRmlBinding(juce::AudioProcessorValueTreeState& state,
                 JtRmlComponent& panel);
    ~JtRmlBinding() override;

    /* Pushes every parameter's current value into the panel. Call once after
     * the document has loaded, and after a patch change. */
    void syncAll();

    /* What the LCD should show: patch name on the top line, the hovered
     * control's name and value beneath. */
    std::function<void(const juce::String& line1,
                       const juce::String& line2)> onLcdChanged;

    void setPatchName(juce::String name);
    const juce::String& patchName() const { return m_patchName; }

    /* Raised on the MESSAGE thread every time a parameter's value has been
     * applied to the panel (the timer drain and syncAll): (param id, 0..1).
     * Lets the owner derive panel STATE from a parameter — e.g. the bass/arp
     * page flip from bass.enable — without a second APVTS listener. */
    std::function<void(const juce::String& id, float normalised)> onValueApplied;

private:
    void parameterChanged(const juce::String& id, float newValue) override;
    void timerCallback() override;

    void controlMoved(const juce::String& key, float normalised);
    void hoverChanged(const juce::String& key);

    static juce::String displayValue(const juce::RangedAudioParameter& p);

    juce::AudioProcessorValueTreeState& m_state;
    JtRmlComponent& m_panel;

    /* Parameters whose value arrived from the host and have yet to be applied
     * to the UI. Written from any thread, drained on the message thread. */
    struct Pending
    {
        std::atomic<float> value{0.0f};
        std::atomic<bool> dirty{false};
    };
    /* Keyed by std::string, not juce::String: JUCE does not specialise
     * std::hash, so a juce::String key does not compile here. */
    std::unordered_map<std::string, std::unique_ptr<Pending>> m_pending;
    std::vector<juce::String> m_ids;

    juce::String m_patchName { "INIT" };
    juce::String m_hovered;

    /* Set while an inbound value is being written to a control, so the
     * control's own change notification is ignored rather than looping back to
     * the APVTS. */
    bool m_pushing = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JtRmlBinding)
};

} // namespace jt
