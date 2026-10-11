// =============================================================================
// JtLcdMenu.h — the LCD becomes a two-level menu over every parameter
// =============================================================================
//
// WHY THIS EXISTS
//   The LCD used to show two things only: the patch name (top line) and, while
//   a knob was moving or hovered, that one control's name + value (bottom line,
//   driven by JtRmlBinding). This class keeps that behaviour for the idle case
//   and adds a menu on top: pick a section, step into its parameters, and edit
//   values from the LCD itself.
//
// NAVIGATION (three levels, driven by an encoder + push switch)
//   Idle      short press ->            Sections
//   Sections  rotate = choose section,  short press -> Params
//   Params    rotate = choose param,    short press -> Edit
//   Edit      rotate = change value,    short press -> back to Params
//   A hold (onBack) steps out one level: Edit -> Params -> Sections -> Idle.
//
//   Two levels beat one long scroll: 18 sections, then a handful of params in
//   the chosen one, instead of paging through all ~240 in a line.
//
// SINGLE SOURCE OF TRUTH
//   Sections and their parameters are built from JtParams::kParams
//   (gen/JtParamTable.h), generated from params.yaml — the same table the
//   panel, firmware and ESP32 consume. Section order and grouping are the
//   table's own `section` field (names in kSectionNames); within a section,
//   parameters keep table order. Values are read and formatted by the APVTS
//   parameter itself (getCurrentValueAsText), exactly as the hover readout is,
//   so the menu can never disagree with the panel or the engine.
//
//   The JUCE table has no visIf, so every parameter is always shown; if some
//   ever need hiding, that belongs in the generator, not a hand filter here.
//
// ONE WRITE PATH / ONE LCD WRITER
//   Edits go out through setValueNotifyingHost — the call a knob uses — so
//   automation, undo and the NRPN bridge all see them. This class is the sole
//   writer to the LCD: hover/knob readouts arrive via showTransient() and show
//   only while idle; once the user is in the menu, the menu owns the display.
//
// PORTABILITY
//   The state machine is thin and JUCE-light; the same navigate/edit logic is
//   what the firmware's hardware encoder menu will run, only the value accessor
//   differs (APVTS here, ParameterStore there).
// =============================================================================

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <cstdint>
#include <functional>
#include <vector>

namespace jt
{

class JtLcdMenu
{
public:
    explicit JtLcdMenu(juce::AudioProcessorValueTreeState& state);

    // ---- LCD output ---------------------------------------------------------
    // The only way anything reaches the LCD. line1/line2 map to the skin's
    // "lcd1"/"lcd2" elements. Wire this to JtRmlPanel::setLcd().
    std::function<void(const juce::String& line1,
                       const juce::String& line2)> onLcd;

    // ---- input primitives (encoder + push switch) ---------------------------
    // Rotate. Sections/Params: move the cursor at that level. Edit: change the
    // selected parameter by `delta` steps (`coarse` = larger step, continuous).
    void encoder(int delta, bool coarse = false);

    // Short press. Idle->Sections, Sections->Params, Params->Edit,
    // Edit->Params (confirm; the value was applied live).
    void enter();

    // Hold. Steps out one level: Edit->Params, Params->Sections, Sections->Idle.
    void back();

    // ---- idle-side updates (from JtRmlBinding, via the editor) ---------------
    // Hover/knob readout. Shown only while idle; ignored in the menu so the menu
    // keeps the display. An empty line2 is treated as a patch-name update and is
    // remembered for the idle top line (mirrors JtRmlBinding::setPatchName).
    void showTransient(const juce::String& line1, const juce::String& line2);

    // Explicit patch-name set, if the editor prefers to push it directly.
    void setPatchName(const juce::String& name);

    // Re-paint the current Params/Edit view from the live APVTS value. No-op in
    // Idle and Sections (no value shown there). Call it when the selected
    // parameter may have changed from OUTSIDE the menu — a panel knob, host
    // automation, an incoming NRPN — so the LCD tracks the real value. Cheap:
    // reads one value and rebuilds two strings; the panel skips the RML write
    // when the text is unchanged.
    void refresh();

    // ---- state --------------------------------------------------------------
    enum class Mode { Idle, Sections, Params, Edit };
    Mode mode()   const noexcept { return m_mode; }
    bool active() const noexcept { return m_mode != Mode::Idle; }

private:
    void render();                               // paint current state via onLcd
    int  currentTableIndex() const;              // selected row in JtParams::kParams
    juce::RangedAudioParameter* currentParam() const;
    void applyStep(int delta, bool coarse);      // edit-mode value change

    juce::AudioProcessorValueTreeState& m_state;

    // One entry per section that has at least one parameter, in table order;
    // each holds the table indices of its parameters, also in table order.
    struct Section
    {
        uint8_t          index;                  // into JtParams::kSectionNames
        std::vector<int> params;                 // indices into JtParams::kParams
    };
    std::vector<Section> m_sections;

    int  m_secCursor = 0;                         // index into m_sections
    int  m_parCursor = 0;                         // index into current section
    Mode m_mode      = Mode::Idle;

    juce::String m_patchName { "INIT" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JtLcdMenu)
};

} // namespace jt
