/*
 * JtRmlElements.h — the remaining custom controls: fader, toggle, stepgrid
 *
 * Grouped into one pair rather than four because they share the same contract
 * with the host - a `param` attribute, a normalised value, and the same two
 * callbacks - and each is small. The combo is deliberately absent: it draws its
 * selected value as text, which needs a font engine, and that decision is still
 * open.
 *
 * THE SHARED CONTRACT
 *   Every control reports a normalised 0..1 value and an opaque param string.
 *   None of them knows about APVTS, NRPN or parameter ranges: range laws, units
 *   and display text belong to ParamMap, and a widget that knew about them
 *   would be a widget you could not reuse.
 *
 * WHY A BASE CLASS
 *   Drag anchoring, modifier scaling and the notify/no-notify distinction are
 *   identical across all of them, and getting any of the three subtly different
 *   between controls is the kind of bug you feel rather than see.
 *
 * RCSS CONTRACT
 *   .fader, .toggle, .stepgrid must all set `drag: drag`. RmlUi only synthesises
 *   drag events for elements that opt in; without it a control receives
 *   mousedown and mouseup and never moves.
 */

#pragma once

#include <RmlUi/Core.h>

#include <functional>
#include <vector>

namespace jt
{

// =============================================================================
// Shared behaviour
// =============================================================================
class JtRmlControl : public Rml::Element
{
public:
    static constexpr float kPixelsPerSweep = 150.0f;
    static constexpr float kScaleShift = 0.10f;
    static constexpr float kScaleCtrl = 0.20f;
    static constexpr float kScaleAlt = 0.50f;

    explicit JtRmlControl(const Rml::String& tag);
    ~JtRmlControl() override = default;

    /* `notify` is false when the host pushes a value in - automation, a patch
     * load, incoming NRPN - so the change does not echo straight back out. */
    virtual void setNormalised(float value, bool notify = true);
    float getNormalised() const { return m_value; }

    const Rml::String& getParam() const { return m_param; }

    std::function<void(JtRmlControl&, float)> onValueChanged;
    std::function<void(JtRmlControl&, bool)> onHover;

protected:
    void OnAttributeChange(const Rml::ElementAttributes& changed) override;
    void ProcessDefaultAction(Rml::Event& event) override;

    /* Called after the value moves, for subclasses that need to restyle. */
    virtual void onValueUpdated() {}

    float modifierScale(const Rml::Event& event) const;

    Rml::String m_param;
    float m_value = 0.5f;
    float m_default = 0.5f;

    /* Deltas are measured from an anchor captured at drag start rather than
     * accumulated, so a long drag cannot drift through rounding and returns
     * exactly where it began. */
    float m_dragStartValue = 0.5f;
    float m_dragStartY = 0.0f;
    bool m_dragging = false;
};

// =============================================================================
// <fader> — vertical, thumb position is the value
// =============================================================================
class JtRmlFader final : public JtRmlControl
{
public:
    explicit JtRmlFader(const Rml::String& tag);
    static void registerElement();

protected:
    void ProcessDefaultAction(Rml::Event& event) override;
    void onValueUpdated() override;

    /* The thumb's position depends on the track's height, which is unknown
     * until layout has run. Without this the thumb sits at the top until the
     * value first changes, which reads as a fader stuck at zero. */
    void OnLayout() override;

private:
    /* The thumb is a child element positioned by top offset; RmlUi lays out the
     * track, and only this one property changes as the value moves. */
    Rml::Element* thumb();
    Rml::Element* m_thumb = nullptr;
};

// =============================================================================
// <toggle> — two states, driven by the :checked pseudo-class
// =============================================================================
class JtRmlToggle final : public JtRmlControl
{
public:
    explicit JtRmlToggle(const Rml::String& tag);
    static void registerElement();

    void setNormalised(float value, bool notify = true) override;

protected:
    void ProcessDefaultAction(Rml::Event& event) override;
};
// =============================================================================
// <stepgrid> — 16 steps, one SELECT/VALUE pair per lane
// =============================================================================
/*
 * THE PROTOCOL IS THE ESP32 CONTROLLER'S, NOT AN INVENTION
 *   The hardware controller edits a step by writing SELECT (the address) and
 *   then VALUE (the data), in that order. The JUCE panel must do exactly the
 *   same or the two front ends will disagree about a pattern. Six details carry
 *   over, each load-bearing:
 *
 *   1. ORDER. select first, value second. The engine moves its edit cursor on
 *      the select write; a value arriving first lands on the previous step.
 *
 *   2. LATTICE. step index -> normalised is step / 15.0f, not / 16: the
 *      parameter is an Int over 1..16, and sixteen values span fifteen
 *      intervals.
 *
 *   3. FORCED VALUE WRITE. Painting the same value onto a DIFFERENT step writes
 *      an equal float; a store that skips equal writes drops it, and the engine
 *      never hears a change the grid is already showing.
 *
 *   4. SELECTING IS A READ. Moving the cursor must NOT write that step's value
 *      back out - stamping the previous step's value onto the new one corrupts
 *      the pattern just by browsing it.
 *
 *   5. THE CACHE IS THE PATTERN. The parameter store only ever holds the LAST
 *      step written, so the sixteen values per lane live here.
 *
 *   6. LANE -> PARAM PAIR. gate = seq.step_select/seq.step_value,
 *      aux = seq.aux_step_select/seq.aux_step_value,
 *      arp = arp.step_select/arp.step_accent. The arp grid drives ACCENT only;
 *      on/off and ratchet are ordinary controls, as on the hardware.
 *
 * Pairs are declared in the document, so the element still knows nothing about
 * the parameter model:
 *
 *   <stepgrid lanes="2"
 *             select="seq.step_select,seq.aux_step_select"
 *             value="seq.step_value,seq.aux_step_value"/>
 */
class JtRmlStepGrid final : public JtRmlControl
{
public:
    explicit JtRmlStepGrid(const Rml::String& tag);
    static void registerElement();

    /* One parameter write from the grid.
     *   forced : bypass equal-value suppression (note 3)
     *   quiet  : apply locally, do not send (note 4) */
    struct Write
    {
        Rml::String param;
        float value = 0.0f;
        bool forced = false;
        bool quiet = false;
    };

    /* Raised once per write, in protocol order: select, then value. */
    std::function<void(const Write&)> onWrite;

    /* Raised when the cursor moves, so the LCD can follow. */
    std::function<void(int lane, int step, float value)> onStepSelected;

    void setSteps(int lane, const std::vector<float>& values);
    void setPlayhead(int step);

    int laneCount() const { return m_lanes; }
    int stepCount() const { return m_steps; }

    /* Normalised position of a step on the 1..16 Int lattice. */
    static float stepToNorm(int step) { return static_cast<float>(step) / 15.0f; }
    static int normToStep(float n) { return static_cast<int>(n * 15.0f + 0.5f); }

protected:
    void OnAttributeChange(const Rml::ElementAttributes& changed) override;
    void ProcessDefaultAction(Rml::Event& event) override;
    void OnRender() override;

private:
    /* Not const: GetClientWidth/Height are non-const in RmlUi, because querying
     * them may force a layout pass. */
    bool hitTest(float x, float y, int& lane, int& step);
    void writeStep(int lane, int step, float value);
    static std::vector<Rml::String> splitCsv(const Rml::String& csv);

    int m_lanes = 2;
    int m_steps = 16;
    int m_playhead = -1;
    int m_selLane = 0;
    int m_selStep = -1;                         // -1: nothing selected yet
    std::vector<Rml::String> m_selectParams;    // per lane
    std::vector<Rml::String> m_valueParams;     // per lane
    std::vector<std::vector<float>> m_values;   // [lane][step] — IS the pattern

    /* Bars build into one mesh, rebuilt only when a value, the playhead or the
     * element's size changes. A 16x3 grid is 48 quads; rebuilding every frame
     * would be waste on a panel that is static most of the time. */
    void rebuildGeometry();
    Rml::Geometry m_geometry;
    bool m_geometryDirty = true;
    Rml::Vector2f m_lastSize;
};


/* Registers every element in this file. Call once after Rml::Initialise() and
 * before any document loads: a document referencing an unregistered element
 * silently falls back to a plain div. */
void registerJtElements();

} // namespace jt
