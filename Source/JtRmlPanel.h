/*
 * JtRmlPanel.h — drop-in replacement for the accordion viewport
 *
 * WHAT IT REPLACES
 *   In JT8000ControllerEditor, the parameter UI is `_viewport` + `_contentPanel`
 *   + `_sections` (an OwnedArray<SectionComponent>). This class replaces those
 *   three members and `buildSections()` / `layoutSections()`.
 *
 * WHAT IT DELIBERATELY DOES NOT REPLACE
 *   The existing 108px header stays exactly as it is. It carries MIDI device
 *   selection, SYX import, save/load/init and the perf-global controls, none of
 *   which the skin implements — and all of which already work. Replacing
 *   working things is how a UI port turns into a rewrite.
 *
 *   The skin's own header strip (brand, LCD, layer buttons) is drawn INSIDE the
 *   panel, beneath yours. Two headers is one too many; see "Known rough edges".
 *
 * SIZE
 *   The panel is authored at 3840x2160 dp and looks right at 1920x1080. Given a
 *   smaller box it scales down uniformly - at the editor's current 1080px width
 *   a knob would be 40px, which is too small to use. The editor should ask for
 *   1920 x (1080 + kHeaderHeight). See setUp() for the exact call.
 *
 * WIRING
 *   Everything the panel needs - embedded assets, parameter binding, stepgrid
 *   protocol, LCD text - is set up in the constructor. The editor only has to
 *   addAndMakeVisible() it and give it bounds.
 */

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdint>
#include <functional>
#include <memory>

namespace jt
{

class JtRmlComponent;
class JtRmlBinding;

class JtRmlPanel : public juce::Component
{
public:
    /* Authoring size. The editor should size itself from these. */
    static constexpr int kDesignW = 3840;
    static constexpr int kDesignH = 2400;            // matches jt8000.rcss body
                                                     // (was 2160 — the mismatch
                                                     // letterboxed the panel and
                                                     // wasted vertical space)
    static constexpr int kNativeW = kDesignW / 2;    // 1920
    static constexpr int kNativeH = kDesignH / 2;    // 1200

    explicit JtRmlPanel(juce::AudioProcessorValueTreeState& state);
    ~JtRmlPanel() override;

    /* Shown on the LCD's top line. Call from wherever the patch name changes. */
    void setPatchName(const juce::String& name);

    /* Writes the LCD's two lines directly (skin elements "lcd1"/"lcd2"). This
     * is the single-writer entry the menu drives; the internal hover/knob
     * readout is diverted through onLcdText so it can no longer write the LCD
     * behind the menu's back. */
    void setLcd(const juce::String& line1, const juce::String& line2);

    /* When set, the binding's hover/knob readout is delivered HERE instead of
     * being written to the LCD. The editor points this at the menu's
     * showTransient(), making the menu the only thing that touches the LCD.
     * Left null, the readout writes the LCD directly (original behaviour). */
    std::function<void(const juce::String& line1,
                       const juce::String& line2)> onLcdText;

    /* The LCD's on-screen rectangle in this panel's coordinates (the union of
     * the two line elements). Empty until the skin has laid out. Used to float
     * the encoder strip beside it. */
    juce::Rectangle<int> lcdBounds() const;

    /* A neutral (mid-arc) frame of the knob sprite strip, decoded as an image,
     * so the encoder can rotate the SAME artwork the panel knobs use. The frame
     * is discovered by pattern from the embedded assets (no hard-coded name),
     * and an invalid image is returned if no knob sprite is found — the caller
     * then keeps its procedural fallback. */
    juce::Image knobImage() const;

    /* The top LCD line's rectangle in this panel's coordinates. The editor
     * floats a transparent, double-click-to-edit label here so the patch name
     * is renamed on the LCD itself instead of a duplicate header field. */
    juce::Rectangle<int> lcdLine1Bounds() const;

    /* "main", "reverb", "seq", "arp", "setup". Also driven by the skin's own
     * layer buttons once those are emitted as elements. */
    void setView(const juce::String& view);

    /* Latching SHIFT. The skin swaps 13 cells in place; nothing moves. */
    void setShift(bool on);
    bool shift() const { return m_shift; }

    /* Sequencer playhead, from the engine status feed. Lane 0 is the gate lane.
     * Pass -1 to clear. */
    void setPlayhead(int step);

    /* Voice activity, from the engine status word. Bit n set = voice n is
     * sounding; `alive` false greys them all, meaning the link is not
     * responding. Eight dots, matching MAX_VOICES. */
    void setVoiceActivity(uint8_t mask, bool alive);

    /* Routed from the skin's action buttons; the editor supplies the handlers
     * it already has. */
    std::function<void(const juce::String&)> onAction;

    /* Development only: a directory whose files win over the embedded copies,
     * so the skin can be edited and reloaded without a rebuild. */
    void setSkinDiskRoot(const juce::File& dir);
    void reloadSkin();

    /* Loads a specific skin document by file (any name). Sets the disk-root to
     * the file's parent folder so its sibling assets resolve there (missing
     * ones fall back to embedded), points the document at the file's name, and
     * reloads. This is the one call a "load skin" UI needs. */
    void loadSkinFile(const juce::File& rmlFile);

    /* Returns to the embedded factory skin: clears the disk-root, restores the
     * default document name, and reloads. */
    void resetSkin();

    /* ---- Bass sequencer pattern grid (bass-seq grid delivery) -----------
     * Playhead for every <bassgrid>: -1 clears it.  Fed from the embedded
     * engine in Sound mode (decision 7b - no hardware telemetry yet). */
    void setBassPlayhead(int step);

    /* Pattern actions from the skin's buttons, routed by the editor:
     *   "bass_clear"  every step becomes a hand-edited REST (blank canvas)
     *   "bass_freeze" copy the generated pattern into hand edits, so later
     *                 seed / amount changes no longer move it
     *   "bass_revert" hand every step back to the generator
     * All three only write bass.step_1..32, so the result is saved with the
     * patch and reaches the engine by the normal parameter path. */
    void bassPatternAction(const juce::String& action);

    void resized() override;

    /* Draws a diagnostic report if the inner component is missing or has no
     * size. This is the OUTER component: if even this does not appear, the
     * problem is in the editor's wiring, not in anything RmlUi does. */
    void paint(juce::Graphics&) override;

    /* Where the log is being written. Shown in the diagnostic so there is no
     * guessing about the path. */
    static juce::File logFile();

private:
    void registerEmbeddedAssets();
    void wireStepGrids();

    /* Bass pattern model: the hand edits, the length and (with the engine
     * compiled in) a SHADOW copy of the firmware generator fed with the same
     * parameters, so the grid can show the generated pattern with no wire
     * traffic (grid decision 1a).  Defined in the .cpp so this header stays
     * free of engine includes. */
    struct BassModel;
    std::unique_ptr<BassModel> m_bass;
    void wireBassGrids();                                  // after any reload
    void refreshBassGrids();                               // model -> every grid
    void onBassValue(const juce::String& id, float norm);  // APVTS -> model
    void writeBassStep(int step, uint16_t packed);         // grid -> APVTS
    void showBassStep(int step, uint16_t packed);          // LCD readout

    juce::AudioProcessorValueTreeState& m_state;
    std::unique_ptr<JtRmlComponent> m_panel;
    std::unique_ptr<JtRmlBinding> m_binding;
    std::unique_ptr<juce::FileLogger> m_logger;
    juce::String m_view { "main" };
    bool m_shift = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JtRmlPanel)
};

} // namespace jt
