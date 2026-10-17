/*
 * JtRmlElements.cpp — implementation
 * See JtRmlElements.h for the shared contract and why these are grouped.
 */

#include "JtRmlElements.h"

#include "JtRmlCombo.h"
#include "JtRmlBassGrid.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace jt
{

namespace
{
    constexpr const char* kAttrParam = "param";
    constexpr const char* kAttrDefault = "default";
    constexpr const char* kAttrLanes = "lanes";
    constexpr const char* kAttrSteps = "steps";

    inline float clamp01(const float v)
    {
        return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
    }
}

// =============================================================================
// JtRmlControl
// =============================================================================
JtRmlControl::JtRmlControl(const Rml::String& tag)
    : Rml::Element(tag)
{
}

void JtRmlControl::OnAttributeChange(const Rml::ElementAttributes& changed)
{
    Rml::Element::OnAttributeChange(changed);

    if (changed.count(kAttrParam))
        m_param = GetAttribute<Rml::String>(kAttrParam, "");

    if (changed.count(kAttrDefault))
    {
        m_default = clamp01(GetAttribute<float>(kAttrDefault, 0.5f));
        setNormalised(m_default, false);
    }
}

void JtRmlControl::setNormalised(const float value, const bool notify)
{
    const float v = clamp01(value);
    if (v == m_value)
        return;

    m_value = v;
    onValueUpdated();

    if (notify && onValueChanged)
        onValueChanged(*this, m_value);
}

float JtRmlControl::modifierScale(const Rml::Event& event) const
{
    /* Modifiers multiply, so shift+ctrl is finer than either alone - which is
     * what someone reaching for both expects. */
    float s = 1.0f;
    if (event.GetParameter<int>("shift_key", 0)) s *= kScaleShift;
    if (event.GetParameter<int>("ctrl_key", 0))  s *= kScaleCtrl;
    if (event.GetParameter<int>("alt_key", 0))   s *= kScaleAlt;
    return s;
}

void JtRmlControl::ProcessDefaultAction(Rml::Event& event)
{
    Rml::Element::ProcessDefaultAction(event);

    switch (event.GetId())
    {
    case Rml::EventId::Mouseover:
        if (onHover) onHover(*this, true);
        break;
    case Rml::EventId::Mouseout:
        if (onHover) onHover(*this, false);
        break;
    default:
        break;
    }
}

// =============================================================================
// JtRmlFader
// =============================================================================
JtRmlFader::JtRmlFader(const Rml::String& tag)
    : JtRmlControl(tag)
{
}

Rml::Element* JtRmlFader::thumb()
{
    if (m_thumb == nullptr && GetNumChildren() > 0)
        m_thumb = GetChild(0);
    return m_thumb;
}

void JtRmlFader::onValueUpdated()
{
    Rml::Element* t = thumb();
    if (t == nullptr)
        return;

    /* Travel is the track's height less the thumb's, so the thumb stays inside
     * the track at both ends rather than hanging off it. */
    const float track = GetClientHeight();
    const float th = t->GetOffsetHeight();
    const float travel = std::max(0.0f, track - th);

    /* Value grows upwards; top offset grows downwards. */
    const float top = travel * (1.0f - m_value);
    t->SetProperty(Rml::PropertyId::Top,
                   Rml::Property(top, Rml::Unit::PX));
}

void JtRmlFader::OnLayout()
{
    JtRmlControl::OnLayout();
    onValueUpdated();
}

void JtRmlFader::ProcessDefaultAction(Rml::Event& event)
{
    JtRmlControl::ProcessDefaultAction(event);

    switch (event.GetId())
    {
    case Rml::EventId::Dragstart:
        m_dragging = true;
        m_dragStartValue = m_value;
        m_dragStartY = event.GetParameter<float>("mouse_y", 0.0f);
        event.StopPropagation();
        break;

    case Rml::EventId::Drag:
    {
        if (!m_dragging)
            break;
        const float y = event.GetParameter<float>("mouse_y", 0.0f);
        /* Screen y grows downwards, the fader upwards, hence the negation.
         * Travel rather than a fixed pixel sweep: a fader's throw IS its
         * length, unlike a knob's. */
        const float travel = std::max(1.0f, GetClientHeight());
        const float d = (m_dragStartY - y) / travel;
        setNormalised(m_dragStartValue + d * modifierScale(event));
        event.StopPropagation();
        break;
    }

    case Rml::EventId::Dragend:
        m_dragging = false;
        event.StopPropagation();
        break;

    case Rml::EventId::Mousescroll:
    {
        const float w = event.GetParameter<float>("wheel_delta", 0.0f);
        if (w != 0.0f)
        {
            setNormalised(m_value - w * (1.0f / 64.0f) * modifierScale(event));
            event.StopPropagation();
        }
        break;
    }

    case Rml::EventId::Dblclick:
        setNormalised(m_default);
        event.StopPropagation();
        break;

    default:
        break;
    }
}

void JtRmlFader::registerElement()
{
    static Rml::ElementInstancerGeneric<JtRmlFader> instancer;
    Rml::Factory::RegisterElementInstancer("fader", &instancer);
}

// =============================================================================
// JtRmlToggle
// =============================================================================
JtRmlToggle::JtRmlToggle(const Rml::String& tag)
    : JtRmlControl(tag)
{
}

void JtRmlToggle::setNormalised(const float value, const bool notify)
{
    /* A toggle is 0 or 1 exactly. Anything in between would leave the artwork
     * showing a state the parameter is not in. */
    JtRmlControl::setNormalised(value >= 0.5f ? 1.0f : 0.0f, notify);
    SetPseudoClass("checked", m_value >= 0.5f);
}

void JtRmlToggle::ProcessDefaultAction(Rml::Event& event)
{
    JtRmlControl::ProcessDefaultAction(event);

    if (event.GetId() == Rml::EventId::Click)
    {
        setNormalised(m_value >= 0.5f ? 0.0f : 1.0f);
        event.StopPropagation();
    }
}

void JtRmlToggle::registerElement()
{
    static Rml::ElementInstancerGeneric<JtRmlToggle> instancer;
    Rml::Factory::RegisterElementInstancer("toggle", &instancer);
}

// =============================================================================
// JtRmlStepGrid
// =============================================================================
JtRmlStepGrid::JtRmlStepGrid(const Rml::String& tag)
    : JtRmlControl(tag)
{
    m_values.assign(static_cast<std::size_t>(m_lanes),
                    std::vector<float>(static_cast<std::size_t>(m_steps), 0.0f));
}

std::vector<Rml::String> JtRmlStepGrid::splitCsv(const Rml::String& csv)
{
    std::vector<Rml::String> out;
    std::string cur;
    for (const char c : std::string(csv.c_str()))
    {
        if (c == ',')
        {
            if (!cur.empty()) out.push_back(Rml::String(cur));
            cur.clear();
        }
        else if (c != ' ' && c != '\t')
        {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) out.push_back(Rml::String(cur));
    return out;
}

void JtRmlStepGrid::OnAttributeChange(const Rml::ElementAttributes& changed)
{
    JtRmlControl::OnAttributeChange(changed);

    bool shape = false;
    if (changed.count(kAttrLanes))
    {
        m_lanes = std::max(1, GetAttribute<int>(kAttrLanes, 2));
        shape = true;
    }
    if (changed.count(kAttrSteps))
    {
        m_steps = std::max(1, GetAttribute<int>(kAttrSteps, 16));
        shape = true;
    }
    if (changed.count("select"))
        m_selectParams = splitCsv(GetAttribute<Rml::String>("select", ""));
    if (changed.count("value"))
        m_valueParams = splitCsv(GetAttribute<Rml::String>("value", ""));
    if (changed.count("stepprefix"))
        m_stepPrefix = splitCsv(GetAttribute<Rml::String>("stepprefix", ""));

    if (shape)
    {
        m_values.assign(static_cast<std::size_t>(m_lanes),
                        std::vector<float>(static_cast<std::size_t>(m_steps), 0.0f));
        m_geometryDirty = true;
    }
}

void JtRmlStepGrid::setSteps(const int lane, const std::vector<float>& values)
{
    if (lane < 0 || lane >= m_lanes)
        return;
    auto& row = m_values[static_cast<std::size_t>(lane)];
    const std::size_t n = std::min(row.size(), values.size());
    std::copy(values.begin(), values.begin() + static_cast<long>(n), row.begin());
    m_geometryDirty = true;
}

void JtRmlStepGrid::setPlayhead(const int step)
{
    if (step == m_playhead)
        return;
    m_playhead = step;
    m_geometryDirty = true;
}

bool JtRmlStepGrid::hitTest(const float x, const float y, int& lane, int& step)
{
    const float w = GetClientWidth();
    const float h = GetClientHeight();
    if (w <= 0.0f || h <= 0.0f || x < 0.0f || y < 0.0f || x >= w || y >= h)
        return false;

    step = std::min(m_steps - 1,
                    static_cast<int>(x / (w / static_cast<float>(m_steps))));
    lane = std::min(m_lanes - 1,
                    static_cast<int>(y / (h / static_cast<float>(m_lanes))));
    return true;
}

void JtRmlStepGrid::writeStep(const int lane, const int step, const float value)
{
    const auto li = static_cast<std::size_t>(lane);

    /* The cache IS the pattern: the parameter store only ever holds the last
     * step written, so the grid's own copy is what the sixteen bars are drawn
     * from and what a lane switch reads back. */
    m_values[li][static_cast<std::size_t>(step)] = value;
    m_geometryDirty = true;

    if (!onWrite)
        return;

    /* Per-step model (matches the firmware): each step is its own param, so a
     * tap writes prefix+(step+1) directly - one write, no cursor. This is the
     * default when the lane declares a stepprefix. */
    if (li < m_stepPrefix.size() && !m_stepPrefix[li].empty())
    {
        onWrite(Write{ m_stepPrefix[li] + std::to_string(step + 1),
                       value, true, false });
        return;
    }

    /* Fallback (old skins): SELECT is the address, VALUE is the data. Order
     * matters - a value written first lands on the previously selected step. */
    if (li < m_selectParams.size() && !m_selectParams[li].empty())
        onWrite(Write{ m_selectParams[li], stepToNorm(step), false, false });

    /* Forced: painting the same value onto a DIFFERENT step writes an equal
     * float, and a store that suppresses equal writes would drop it - the
     * engine would never hear a change the grid is already showing. */
    if (li < m_valueParams.size() && !m_valueParams[li].empty())
        onWrite(Write{ m_valueParams[li], value, true, false });
}

void JtRmlStepGrid::ProcessDefaultAction(Rml::Event& event)
{
    JtRmlControl::ProcessDefaultAction(event);

    const auto id = event.GetId();
    if (id != Rml::EventId::Mousedown && id != Rml::EventId::Drag)
        return;

    const float x = event.GetParameter<float>("mouse_x", 0.0f)
                    - GetAbsoluteOffset().x;
    const float y = event.GetParameter<float>("mouse_y", 0.0f)
                    - GetAbsoluteOffset().y;

    int lane = 0, step = 0;
    if (!hitTest(x, y, lane, step))
        return;

    /* Height within the lane sets the value, so one drag draws a pattern -
     * which is the point of a grid over a pair of knobs. */
    const float laneH = GetClientHeight() / static_cast<float>(m_lanes);
    const float local = y - static_cast<float>(lane) * laneH;
    const float v = clamp01(1.0f - local / std::max(1.0f, laneH));

    const bool moved = (lane != m_selLane || step != m_selStep);
    if (moved)
    {
        m_selLane = lane;
        m_selStep = step;

        /* Selecting is a READ. The cursor's new step must be loaded QUIETLY -
         * writing it back out would stamp the previous step's value onto this
         * one and corrupt the pattern just by browsing it. */
        const auto li = static_cast<std::size_t>(lane);
        if (onWrite && li < m_valueParams.size() && !m_valueParams[li].empty())
            onWrite(Write{ m_valueParams[li],
                           m_values[li][static_cast<std::size_t>(step)],
                           false, true });

        if (onStepSelected)
            onStepSelected(lane, step,
                           m_values[li][static_cast<std::size_t>(step)]);
    }

    writeStep(lane, step, v);
    event.StopPropagation();
}

void JtRmlStepGrid::rebuildGeometry()
{
    Rml::RenderManager* rm = GetRenderManager();
    if (rm == nullptr)
        return;

    const float w = GetClientWidth();
    const float h = GetClientHeight();
    if (w <= 1.0f || h <= 1.0f)
        return;

    const float laneH = h / static_cast<float>(m_lanes);
    const float stepW = w / static_cast<float>(m_steps);

    /* Lane colours: the gate lane reads as the primary value, the aux lane as
     * modulation, and a third lane (the arpeggiator's ratchet) as neither. Red
     * stays reserved for "on", consistent with the rest of the panel. */
    static const Rml::Colourb kLane[3] = {
        Rml::Colourb(255, 168, 58, 255),
        Rml::Colourb(226, 24, 24, 255),
        Rml::Colourb(110, 190, 220, 255),
    };

    Rml::Mesh mesh;
    int quad = 0;

    const auto addQuad = [&mesh, &quad](float x0, float y0, float x1, float y1,
                                        Rml::Colourb c)
    {
        const Rml::ColourbPremultiplied p = c.ToPremultiplied();
        mesh.vertices.push_back({ Rml::Vector2f(x0, y0), p, Rml::Vector2f() });
        mesh.vertices.push_back({ Rml::Vector2f(x1, y0), p, Rml::Vector2f() });
        mesh.vertices.push_back({ Rml::Vector2f(x1, y1), p, Rml::Vector2f() });
        mesh.vertices.push_back({ Rml::Vector2f(x0, y1), p, Rml::Vector2f() });
        const int b = quad * 4;
        mesh.indices.insert(mesh.indices.end(),
                            { b, b + 1, b + 2, b, b + 2, b + 3 });
        ++quad;
    };

    for (int lane = 0; lane < m_lanes; ++lane)
    {
        const float top = static_cast<float>(lane) * laneH;
        const float base = top + laneH - 4.0f;
        const Rml::Colourb col = kLane[lane % 3];

        for (int step = 0; step < m_steps; ++step)
        {
            const float v = m_values[static_cast<std::size_t>(lane)]
                                    [static_cast<std::size_t>(step)];
            const float x0 = static_cast<float>(step) * stepW + 2.0f;
            const float x1 = x0 + stepW - 4.0f;

            /* The playhead is drawn as a full-height wash behind its column,
             * not as a border: a border on a two-pixel bar is invisible. */
            if (step == m_playhead)
                addQuad(x0, top, x1, top + laneH,
                        Rml::Colourb(255, 255, 255, 28));

            const float barH = (laneH - 8.0f) * v;
            if (barH > 0.5f)
                addQuad(x0, base - barH, x1, base, col);
        }
    }

    /* Selection marker, so the LCD readout and the grid agree about which step
     * is being edited. */
    if (m_selStep >= 0 && m_selStep < m_steps)
    {
        const float x0 = static_cast<float>(m_selStep) * stepW + 2.0f;
        const float top = static_cast<float>(m_selLane) * laneH;
        addQuad(x0, top, x0 + stepW - 4.0f, top + 3.0f,
                Rml::Colourb(255, 255, 255, 190));
    }

    m_geometry = rm->MakeGeometry(std::move(mesh));
    m_geometryDirty = false;
    m_lastSize = Rml::Vector2f(w, h);
}

void JtRmlStepGrid::OnRender()
{
    Rml::Element::OnRender();

    /* Rebuild when the value set changed or the element was resized; the size
     * check catches a window resize, which does not otherwise notify. */
    if (m_geometryDirty
        || std::fabs(m_lastSize.x - GetClientWidth()) > 0.5f
        || std::fabs(m_lastSize.y - GetClientHeight()) > 0.5f)
        rebuildGeometry();

    if (m_geometry)
        m_geometry.Render(GetAbsoluteOffset());
}

void JtRmlStepGrid::registerElement()
{
    static Rml::ElementInstancerGeneric<JtRmlStepGrid> instancer;
    Rml::Factory::RegisterElementInstancer("stepgrid", &instancer);
}

// =============================================================================
void registerJtElements()
{
    JtRmlFader::registerElement();
    JtRmlToggle::registerElement();
    JtRmlStepGrid::registerElement();
    JtRmlCombo::registerElement();
    JtRmlBassGrid::registerElement();    // <bassgrid> (bass-seq pattern)
}

} // namespace jt
