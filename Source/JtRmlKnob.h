/*
 * JtRmlKnob.h — sprite-sheet rotary control for RmlUi
 *
 * A <knob> element whose appearance is a pre-rendered sprite strip and whose
 * only per-frame work is swapping which sprite its decorator points at.
 *
 * WHY A SPRITE STRIP RATHER THAN DRAWN GEOMETRY
 *   Drawing a knob costs gradient fills, path strokes and (for any glow) a
 *   blur, on every repaint, for every knob on a 110-control panel. Blitting a
 *   cached sprite is a single textured quad. The artwork cost moves to build
 *   time, where it is free. Sheets are produced by tools/gen_knob_tus.py.
 *
 * RCSS CONTRACT
 *   .knob
 *   {
 *       frames: 128;                  // sprites in the sheet
 *       spriteprefix: knob_silver_;   // concatenated with a 3-digit index
 *       drag: drag;                   // REQUIRED, see note below
 *       width: 150dp; height: 150dp;
 *   }
 *
 *   `drag: drag` is not optional. RmlUi only synthesises Drag events for
 *   elements that opt in via the drag property; without it the knob receives
 *   mousedown and mouseup but never moves.
 *
 * RML CONTRACT
 *   <knob class="knob" param="osc1.fine_tune" default="0.5"/>
 *
 *   `param` is an opaque key handed back to the host in the value callback.
 *   This element deliberately knows nothing about APVTS, NRPN or parameter
 *   ranges — it reports a normalised 0..1 position and nothing more. Range
 *   laws, units and display text belong to ParamMap, not to a widget.
 *
 * RMLUI VERSION
 *   Written against upstream RmlUi 6.x (MIT). Note that some forks add a
 *   Rml::CoreInstance& as the first constructor argument; upstream does not,
 *   and this class follows upstream.
 *
 * PERFORMANCE NOTES
 *   - The decorator property is only reassigned when the computed frame index
 *     actually changes. A drag of a few pixels usually lands on the same frame
 *     and must not trigger a style recalculation.
 *   - Sprite name construction avoids std::to_string and stream formatting on
 *     the hot path; the three-digit suffix is written directly into a small
 *     stack buffer.
 */

#pragma once

#include <RmlUi/Core.h>

#include <functional>

namespace jt
{

class JtRmlKnob : public Rml::Element
{
public:
    /* Drag distance, in pixels, for a full 0..1 sweep. Matches the feel of the
     * reference skin; exposed as a constant rather than a magic number so it
     * can be tuned once for the whole panel. */
    static constexpr float kPixelsPerSweep = 150.0f;

    /* Modifier scaling for fine adjustment. */
    static constexpr float kScaleShift = 0.10f;
    static constexpr float kScaleCtrl  = 0.20f;
    static constexpr float kScaleAlt   = 0.50f;

    /* Mouse wheel step, as a fraction of full travel. One detent is roughly
     * two sprite frames on a 128-frame sheet, which feels like a click-stop
     * without being coarse. */
    static constexpr float kWheelStep = 1.0f / 64.0f;

    explicit JtRmlKnob(const Rml::String& tag);
    ~JtRmlKnob() override = default;

    /* Normalised position, 0..1. `notify` is false when the host is pushing a
     * value in (parameter automation, patch load, incoming NRPN) so that the
     * change does not echo straight back out and cause a feedback loop. */
    void  setNormalised(float value, bool notify = true);
    float getNormalised() const { return m_value; }

    const Rml::String& getParam() const { return m_param; }

    /* Host hooks. Set once, after instancing.
     *   onValueChanged  user moved the control
     *   onHover         pointer entered or left; the host uses this to drive
     *                   the LCD readout, which is why no knob carries its own
     *                   numeric display.
     */
    std::function<void(JtRmlKnob&, float)> onValueChanged;
    std::function<void(JtRmlKnob&, bool)>  onHover;

    /* Registers the custom RCSS properties and the <knob> instancer. Must be
     * called once after Rml::Initialise() and before any document is loaded,
     * because a document referencing an unregistered element silently falls
     * back to a plain div. */
    static void registerElement();

protected:
    void OnAttributeChange(const Rml::ElementAttributes& changed) override;
    void OnPropertyChange(const Rml::PropertyIdSet& changed) override;
    void ProcessDefaultAction(Rml::Event& event) override;

private:
    void  readRcssProperties();
    void  updateSprite();
    float modifierScale(const Rml::Event& event) const;

    /* --- state pushed in from RCSS ------------------------------------ */
    Rml::String m_spritePrefix;
    int         m_frames = 128;

    /* --- state pushed in from RML attributes --------------------------- */
    Rml::String m_param;
    float       m_default = 0.5f;

    /* --- live state ---------------------------------------------------- */
    float m_value = 0.5f;

    /* Value at the moment the drag began. Deltas are applied to this rather
     * than accumulated onto m_value, so rounding cannot drift over a long
     * drag and the control returns exactly where it started if you drag back. */
    float m_dragStartValue = 0.5f;
    float m_dragStartY     = 0.0f;
    bool  m_dragging       = false;

    /* Last frame index actually written to the decorator. -1 forces the first
     * update. See performance notes. */
    int m_lastFrame = -1;
};

} // namespace jt
