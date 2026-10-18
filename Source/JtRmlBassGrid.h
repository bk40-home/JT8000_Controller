/*
 * JtRmlBassGrid.h — <bassgrid>: the bass-sequencer pattern, drawn and edited
 *
 * WHAT IT SHOWS
 *   One column per step up to the pattern length (bass.length), as a small
 *   piano roll whose vertical axis is the SCALE DEGREE (decision 5a: always in
 *   key, follows bass.scale without knowing it).
 *     note       a bar on its degree row; brighter/red when ACCENTED
 *     slide      a tie bar joining the note to the next step
 *     octave     a thin band above (+1) or below (-1) the note
 *     rest       a faint ghost at the step's stored degree
 *     hand edit  a white outline (the step carries the USER bit)
 *     playhead   a column wash (Sound mode)
 *
 * WHAT IT DOES NOT KNOW
 *   Nothing about seeds, generation, parameters or the APVTS.  The panel hands
 *   it 32 RESOLVED packed steps (generated or edited) via setPattern(), and it
 *   reports every edit as (step, packed) through onEdit.  Same separation as
 *   the other controls: a widget that knew the parameter model could not be
 *   reused, and could disagree with it.
 *
 * GESTURES (decision 4a)
 *   left-click empty cell     place a note at that degree
 *   left-click a note         remove it (on release, so a drag can move it)
 *   left-drag a note          move it up/down (degree)
 *   right-click a note        cycle  none -> accent -> slide -> both -> none
 *   shift-click a note        cycle octave  0 -> +1 -> -1 -> 0
 *   double-click any step     hand the step back to the generator
 *
 * PACKED STEP FORMAT — mirrors JT::BassSequencer (firmware BassSequencer.h).
 * Duplicated as constants rather than included so this file builds in a
 * controller-only plugin (no engine sources).  If the firmware format ever
 * changes, change it here too: the bit layout is frozen in saved patches.
 *
 * RCSS CONTRACT: .bassgrid must set `drag: drag` (no Drag events otherwise).
 */

#pragma once

#include <RmlUi/Core.h>

#include <array>
#include <cstdint>
#include <functional>

namespace jt
{

class JtRmlBassGrid final : public Rml::Element
{
public:
    // ---- Shape -----------------------------------------------------------
    static constexpr int kMaxSteps = 32;    // == BassSequencer::kMaxSteps
    static constexpr int kRows     = 16;    // degrees 0..15 (bass.range max 15)

    // ---- Packed step bits (== BassSequencer.h "Packed step format") -------
    static constexpr uint16_t kDegreeMask = 0x001Fu;   // bits 0-4
    static constexpr uint16_t kGateBit    = 1u << 5;
    static constexpr uint16_t kAccentBit  = 1u << 6;
    static constexpr uint16_t kSlideBit   = 1u << 7;
    static constexpr int      kOctShift   = 8;          // bits 8-9: 0=-1 1=0 2=+1
    static constexpr uint16_t kOctMask    = 0x0300u;
    static constexpr uint16_t kUserBit    = 1u << 10;   // hand edit

    explicit JtRmlBassGrid(const Rml::String& tag);
    static void registerElement();

    /* The resolved pattern: `count` packed steps (generated or edited) and the
     * playing length.  Redraws only if something actually changed. */
    void setPattern(const uint16_t* packed, int count, int length);

    /* -1 clears it.  Change-driven. */
    void setPlayhead(int step);

    /* One hand edit: the step's NEW packed value with the USER bit set, or 0
     * to hand the step back to the generator (double-click). */
    std::function<void(int step, uint16_t packed)> onEdit;

    /* The touched step's value after an edit, for the LCD readout. */
    std::function<void(int step, uint16_t packed)> onStepInfo;

protected:
    void ProcessDefaultAction(Rml::Event& event) override;
    void OnRender() override;

private:
    /* Not const: GetClientWidth/Height may force a layout pass in RmlUi. */
    bool hitTest(float x, float y, int& step, int& row);
    int  rowAt(float y);
    void commit(int step, uint16_t packed);       // local update + onEdit
    void rebuildGeometry();

    std::array<uint16_t, kMaxSteps> m_steps {};   // resolved pattern (display)
    int m_length   = 16;
    int m_playhead = -1;

    // ---- Gesture state (one left-button gesture at a time) ---------------
    int  m_dragStep      = -1;      // step being pressed/dragged, -1 = none
    bool m_pendingRemove = false;   // pressed ON a note: remove on release...
    bool m_moved         = false;   // ...unless it was dragged to a new row

    /* All quads build into one mesh, rebuilt only when the pattern, the
     * playhead or the size changes — a static panel costs nothing per frame. */
    Rml::Geometry m_geometry;
    bool          m_dirty = true;
    Rml::Vector2f m_lastSize;
};

} // namespace jt
