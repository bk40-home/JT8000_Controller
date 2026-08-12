/*
 * JtRmlKnob.cpp — implementation
 *
 * See JtRmlKnob.h for the RCSS/RML contract and the reasoning behind the
 * sprite-strip approach.
 */

#include "JtRmlKnob.h"

#include <algorithm>
#include <cstdio>

namespace jt
{

namespace
{
    /* Custom RCSS property names. Registered once in registerElement(). */
    constexpr const char* kPropFrames = "frames";
    constexpr const char* kPropPrefix = "spriteprefix";

    /* Attribute names. */
    constexpr const char* kAttrParam   = "param";
    constexpr const char* kAttrDefault = "default";

    /* Cached property ids. Looking these up by string on every property change
     * would mean a hash lookup per knob per style pass; resolving once at
     * registration is free. */
    Rml::PropertyId g_idFrames = Rml::PropertyId::Invalid;
    Rml::PropertyId g_idPrefix = Rml::PropertyId::Invalid;

    inline float clamp01(const float v)
    {
        return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
    }
}

// =============================================================================
// Construction and registration
// =============================================================================
JtRmlKnob::JtRmlKnob(const Rml::String& tag)
    : Rml::Element(tag)
{
}

void JtRmlKnob::registerElement()
{
    /* Custom properties must exist before any stylesheet that uses them is
     * parsed, otherwise RmlUi discards the declarations with a warning and the
     * knob silently renders as an empty box.
     *
     * Neither property is inherited: a knob's sheet is a property of that
     * knob, and inheriting it would mean a container accidentally restyling
     * every control beneath it. */
    g_idFrames = Rml::StyleSheetSpecification::RegisterProperty(
                     kPropFrames, "128", false /*inherited*/, false /*forces layout*/)
                     .AddParser("number")
                     .GetId();

    g_idPrefix = Rml::StyleSheetSpecification::RegisterProperty(
                     kPropPrefix, "", false, false)
                     .AddParser("string")
                     .GetId();

    /* The instancer must outlive every document, so it is function-local
     * static rather than owned by a caller who might go out of scope. */
    static Rml::ElementInstancerGeneric<JtRmlKnob> instancer;
    Rml::Factory::RegisterElementInstancer("knob", &instancer);
}

// =============================================================================
// RCSS and attributes
// =============================================================================
void JtRmlKnob::readRcssProperties()
{
    if (const Rml::Property* p = GetProperty(g_idFrames))
    {
        /* A sheet with fewer than two frames cannot express a range; clamp
         * rather than divide by zero later. */
        m_frames = std::max(2, static_cast<int>(p->Get<float>()));
    }

    if (const Rml::Property* p = GetProperty(g_idPrefix))
        m_spritePrefix = p->Get<Rml::String>();

    /* Force a sprite write on the next update: the prefix or frame count may
     * have changed even though the value did not. */
    m_lastFrame = -1;
}

void JtRmlKnob::OnPropertyChange(const Rml::PropertyIdSet& changed)
{
    Rml::Element::OnPropertyChange(changed);

    if (changed.Contains(g_idFrames) || changed.Contains(g_idPrefix))
    {
        readRcssProperties();
        updateSprite();
    }
}

void JtRmlKnob::OnAttributeChange(const Rml::ElementAttributes& changed)
{
    Rml::Element::OnAttributeChange(changed);

    if (changed.count(kAttrParam))
        m_param = GetAttribute<Rml::String>(kAttrParam, "");

    if (changed.count(kAttrDefault))
    {
        m_default = clamp01(GetAttribute<float>(kAttrDefault, 0.5f));

        /* Adopt the default as the initial position only while the control is
         * still untouched. Doing it unconditionally would stamp on a value the
         * host had already pushed in from a loaded patch. */
        if (m_lastFrame < 0)
            setNormalised(m_default, false);
    }
}

// =============================================================================
// Value
// =============================================================================
void JtRmlKnob::setNormalised(const float value, const bool notify)
{
    const float v = clamp01(value);
    if (v == m_value && m_lastFrame >= 0)
        return;                       // nothing to do; skip the style write

    m_value = v;
    updateSprite();

    if (notify && onValueChanged)
        onValueChanged(*this, m_value);
}

void JtRmlKnob::updateSprite()
{
    if (m_spritePrefix.empty())
        return;

    const int frame = std::clamp(
        static_cast<int>(m_value * static_cast<float>(m_frames - 1) + 0.5f),
        0, m_frames - 1);

    /* The whole point of the caching: a few pixels of drag usually lands on
     * the same frame, and reassigning the decorator would trigger a full style
     * recalculation for no visible change. */
    if (frame == m_lastFrame)
        return;
    m_lastFrame = frame;

    /* Sprite names are prefix + three digits, e.g. knob_silver_042.
     * Built into a stack buffer rather than via streams or std::to_string,
     * because this runs on every frame change of every knob being dragged. */
    char suffix[4];
    std::snprintf(suffix, sizeof(suffix), "%03d", frame);

    Rml::String decorator;
    decorator.reserve(m_spritePrefix.size() + 16);
    decorator += "image(";
    decorator += m_spritePrefix;
    decorator += suffix;
    decorator += ")";

    SetProperty("decorator", decorator);
}

// =============================================================================
// Input
// =============================================================================
float JtRmlKnob::modifierScale(const Rml::Event& event) const
{
    /* Modifiers stack multiplicatively, so shift+ctrl gives a finer result
     * than either alone. That is what a user reaching for both expects. */
    float scale = 1.0f;
    if (event.GetParameter<int>("shift_key", 0)) scale *= kScaleShift;
    if (event.GetParameter<int>("ctrl_key",  0)) scale *= kScaleCtrl;
    if (event.GetParameter<int>("alt_key",   0)) scale *= kScaleAlt;
    return scale;
}

void JtRmlKnob::ProcessDefaultAction(Rml::Event& event)
{
    Rml::Element::ProcessDefaultAction(event);

    switch (event.GetId())
    {
    case Rml::EventId::Dragstart:
        /* Anchor the drag. Deltas are measured from here rather than
         * accumulated, so a long drag cannot drift through rounding. */
        m_dragging       = true;
        m_dragStartValue = m_value;
        m_dragStartY     = event.GetParameter<float>("mouse_y", 0.0f);
        event.StopPropagation();
        break;

    case Rml::EventId::Drag:
    {
        if (!m_dragging)
            break;

        /* Screen y grows downwards; a knob turns up when the mouse moves up,
         * hence the negated delta. */
        const float y     = event.GetParameter<float>("mouse_y", 0.0f);
        const float delta = (m_dragStartY - y) / kPixelsPerSweep;

        setNormalised(m_dragStartValue + delta * modifierScale(event));
        event.StopPropagation();
        break;
    }

    case Rml::EventId::Dragend:
        m_dragging = false;
        event.StopPropagation();
        break;

    case Rml::EventId::Mousescroll:
    {
        /* RmlUi reports positive wheel_delta as scrolling down. */
        const float wheel = event.GetParameter<float>("wheel_delta", 0.0f);
        if (wheel != 0.0f)
        {
            setNormalised(m_value - wheel * kWheelStep * modifierScale(event));
            event.StopPropagation();
        }
        break;
    }

    case Rml::EventId::Dblclick:
        setNormalised(m_default);
        event.StopPropagation();
        break;

    case Rml::EventId::Mouseover:
        if (onHover)
            onHover(*this, true);
        break;

    case Rml::EventId::Mouseout:
        if (onHover)
            onHover(*this, false);
        break;

    default:
        break;
    }
}

} // namespace jt
