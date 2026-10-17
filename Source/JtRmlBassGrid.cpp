/*
 * JtRmlBassGrid.cpp — implementation (contract and gestures in the header)
 */

#include "JtRmlBassGrid.h"

#include <algorithm>
#include <cmath>

namespace jt
{

namespace
{
    // Panel palette: orange = a value, red = "on/emphasis" (accent), as the
    // step grids and knobs already use.
    const Rml::Colourb kNote   (255, 168,  58, 255);
    const Rml::Colourb kAccent (255,  72,  40, 255);
    const Rml::Colourb kGhost  (255, 168,  58,  46);   // rest: stored degree
    const Rml::Colourb kOctave (240, 244, 250, 230);
    const Rml::Colourb kOutline(240, 244, 250, 200);   // hand-edited step
    const Rml::Colourb kBeat   (255, 255, 255,  12);   // every other beat
    const Rml::Colourb kRowLine(255, 255, 255,   9);   // degree guide rows
    const Rml::Colourb kPlay   (255, 255, 255,  40);   // playhead column

    inline int degreeOf(uint16_t s) { return static_cast<int>(s & JtRmlBassGrid::kDegreeMask); }
    inline bool gated(uint16_t s)   { return (s & JtRmlBassGrid::kGateBit) != 0; }
    inline int octField(uint16_t s)
    {
        const int o = static_cast<int>((s & JtRmlBassGrid::kOctMask) >> JtRmlBassGrid::kOctShift);
        return (o == 3) ? 1 : o;                      // 3 is treated as 0 (engine rule)
    }
    inline uint16_t withOct(uint16_t s, int field)
    {
        return static_cast<uint16_t>((s & ~JtRmlBassGrid::kOctMask)
                                     | (static_cast<uint16_t>(field) << JtRmlBassGrid::kOctShift));
    }
    inline uint16_t withDegree(uint16_t s, int deg)
    {
        return static_cast<uint16_t>((s & ~JtRmlBassGrid::kDegreeMask)
                                     | static_cast<uint16_t>(deg & 0x1F));
    }
}

JtRmlBassGrid::JtRmlBassGrid(const Rml::String& tag)
    : Rml::Element(tag)
{
}

void JtRmlBassGrid::registerElement()
{
    static Rml::ElementInstancerGeneric<JtRmlBassGrid> instancer;
    Rml::Factory::RegisterElementInstancer("bassgrid", &instancer);
}

// =============================================================================
// Host -> grid
// =============================================================================
void JtRmlBassGrid::setPattern(const uint16_t* packed, int count, int length)
{
    const int n   = std::min(count, kMaxSteps);
    const int len = std::max(1, std::min(length, kMaxSteps));
    bool changed  = (len != m_length);
    for (int i = 0; i < n; ++i)
    {
        if (m_steps[static_cast<std::size_t>(i)] != packed[i])
        {
            m_steps[static_cast<std::size_t>(i)] = packed[i];
            changed = true;
        }
    }
    m_length = len;
    if (changed)
        m_dirty = true;
}

void JtRmlBassGrid::setPlayhead(int step)
{
    if (step == m_playhead)
        return;
    m_playhead = step;
    m_dirty = true;
}

// =============================================================================
// Hit testing
// =============================================================================
int JtRmlBassGrid::rowAt(float y)
{
    const float h = GetClientHeight();
    if (h <= 0.0f)
        return 0;
    // Row 0 (degree 0, the root) is at the BOTTOM, as on a piano roll.
    const int r = kRows - 1 - static_cast<int>(y / (h / static_cast<float>(kRows)));
    return std::max(0, std::min(kRows - 1, r));
}

bool JtRmlBassGrid::hitTest(float x, float y, int& step, int& row)
{
    const float w = GetClientWidth();
    const float h = GetClientHeight();
    if (w <= 0.0f || h <= 0.0f || x < 0.0f || y < 0.0f || x >= w || y >= h)
        return false;
    step = std::min(m_length - 1,
                    static_cast<int>(x / (w / static_cast<float>(m_length))));
    row  = rowAt(y);
    return true;
}

// =============================================================================
// Edits
// =============================================================================
void JtRmlBassGrid::commit(int step, uint16_t packed)
{
    // A hand edit is always marked USER, so the generator leaves it alone.
    packed = static_cast<uint16_t>(packed | kUserBit);
    // Rest invariant (same as the engine's setStepPacked): no accent or slide.
    if (!gated(packed))
        packed = static_cast<uint16_t>(packed & ~(kAccentBit | kSlideBit));

    // Optimistic local update: the panel's regenerated pattern arrives right
    // after through setPattern() and confirms it.
    m_steps[static_cast<std::size_t>(step)] = packed;
    m_dirty = true;

    if (onEdit)     onEdit(step, packed);
    if (onStepInfo) onStepInfo(step, packed);
}

void JtRmlBassGrid::ProcessDefaultAction(Rml::Event& event)
{
    Rml::Element::ProcessDefaultAction(event);

    const auto id = event.GetId();
    const float x = event.GetParameter<float>("mouse_x", 0.0f) - GetAbsoluteOffset().x;
    const float y = event.GetParameter<float>("mouse_y", 0.0f) - GetAbsoluteOffset().y;

    // ---- double-click: hand the step back to the generator ---------------
    if (id == Rml::EventId::Dblclick)
    {
        int step = 0, row = 0;
        if (hitTest(x, y, step, row) && onEdit)
        {
            onEdit(step, 0);                       // 0 = "generated"
            if (onStepInfo) onStepInfo(step, 0);
        }
        m_dragStep = -1;
        event.StopPropagation();
        return;
    }

    if (id == Rml::EventId::Mousedown)
    {
        int step = 0, row = 0;
        if (!hitTest(x, y, step, row))
            return;
        const uint16_t s     = m_steps[static_cast<std::size_t>(step)];
        const int      btn   = event.GetParameter<int>("button", 0);
        const bool     shift = event.GetParameter<int>("shift_key", 0) != 0;

        if (btn == 1)                              // right: accent/slide cycle
        {
            if (gated(s))
            {
                // State as 2 bits: 0 none, 1 accent, 2 slide, 3 both.
                // Order: none -> accent -> slide -> both -> none, i.e. +1 mod 4.
                const int cur = ((s & kAccentBit) ? 1 : 0) | ((s & kSlideBit) ? 2 : 0);
                const int nxt = (cur + 1) & 3;
                uint16_t v = static_cast<uint16_t>(s & ~(kAccentBit | kSlideBit));
                if (nxt & 1) v |= kAccentBit;
                if (nxt & 2) v |= kSlideBit;
                commit(step, v);
            }
            event.StopPropagation();
            return;
        }

        if (shift)                                 // shift-left: octave cycle
        {
            if (gated(s))
            {
                // field 1 (0) -> 2 (+1) -> 0 (-1) -> 1 (0)
                const int f = octField(s);
                commit(step, withOct(s, f == 1 ? 2 : (f == 2 ? 0 : 1)));
            }
            event.StopPropagation();
            return;
        }

        // Plain left press.
        m_dragStep = step;
        m_moved    = false;
        if (gated(s) && degreeOf(s) == row)
        {
            m_pendingRemove = true;                // decided on release
        }
        else
        {
            m_pendingRemove = false;
            uint16_t v = withDegree(s, row);
            if (!gated(s))                         // a NEW note: plain, octave 0
                v = withOct(static_cast<uint16_t>(v & ~(kAccentBit | kSlideBit)), 1);
            commit(step, static_cast<uint16_t>(v | kGateBit));
        }
        event.StopPropagation();
        return;
    }

    if (id == Rml::EventId::Drag && m_dragStep >= 0)
    {
        // Vertical only: a drag edits the PRESSED step's degree, so a sloppy
        // horizontal wobble cannot smear the edit onto a neighbour.
        const int row = rowAt(y);
        const uint16_t s = m_steps[static_cast<std::size_t>(m_dragStep)];
        if (gated(s) && degreeOf(s) != row)
        {
            m_moved = true;
            m_pendingRemove = false;
            commit(m_dragStep, withDegree(s, row));
        }
        event.StopPropagation();
        return;
    }

    if ((id == Rml::EventId::Mouseup || id == Rml::EventId::Dragend) && m_dragStep >= 0)
    {
        if (m_pendingRemove && !m_moved)
        {
            const uint16_t s = m_steps[static_cast<std::size_t>(m_dragStep)];
            commit(m_dragStep, static_cast<uint16_t>(s & ~(kGateBit | kAccentBit | kSlideBit)));
        }
        m_dragStep = -1;
        m_pendingRemove = false;
        event.StopPropagation();
    }
}

// =============================================================================
// Drawing
// =============================================================================
void JtRmlBassGrid::rebuildGeometry()
{
    Rml::RenderManager* rm = GetRenderManager();
    if (rm == nullptr)
        return;
    const float w = GetClientWidth();
    const float h = GetClientHeight();
    if (w <= 1.0f || h <= 1.0f)
        return;

    const float colW = w / static_cast<float>(m_length);
    const float rowH = h / static_cast<float>(kRows);

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
        mesh.indices.insert(mesh.indices.end(), { b, b + 1, b + 2, b, b + 2, b + 3 });
        ++quad;
    };
    // Top edge of a degree row (row 0 at the bottom).
    const auto rowTop = [h, rowH](int deg) { return h - static_cast<float>(deg + 1) * rowH; };

    // ---- background guides ------------------------------------------------
    for (int s = 0; s < m_length; s += 8)               // every other beat (4 steps)
        addQuad(static_cast<float>(s) * colW, 0.0f,
                std::min(w, static_cast<float>(s + 4) * colW), h, kBeat);
    for (int r = 1; r < kRows; r += 2)                  // alternate degree rows
        addQuad(0.0f, rowTop(r), w, rowTop(r) + rowH, kRowLine);
    if (m_playhead >= 0 && m_playhead < m_length)
        addQuad(static_cast<float>(m_playhead) * colW, 0.0f,
                static_cast<float>(m_playhead + 1) * colW, h, kPlay);

    // ---- steps ---------------------------------------------------------------
    for (int i = 0; i < m_length; ++i)
    {
        const uint16_t s = m_steps[static_cast<std::size_t>(i)];
        const int   deg  = std::min(kRows - 1, degreeOf(s));
        const float x0   = static_cast<float>(i) * colW + 3.0f;
        const float x1   = static_cast<float>(i + 1) * colW - 3.0f;
        const float y0   = rowTop(deg) + 2.0f;
        const float y1   = y0 + rowH - 4.0f;

        if (!gated(s))
        {
            addQuad(x0, y0 + rowH * 0.3f, x1, y1 - rowH * 0.3f, kGhost);
        }
        else
        {
            const bool accent = (s & kAccentBit) != 0;
            addQuad(x0, y0, x1, y1, accent ? kAccent : kNote);

            // Slide: a tie bar into the next PLAYED step (forward order, the
            // way the grid reads).  Drawn only when the next step is a note.
            const int nxt = (i + 1 < m_length) ? i + 1 : 0;
            if ((s & kSlideBit) && gated(m_steps[static_cast<std::size_t>(nxt)]) && nxt != 0)
            {
                const float ym = (y0 + y1) * 0.5f;
                addQuad(x1, ym - rowH * 0.12f, x1 + 6.0f, ym + rowH * 0.12f, kNote);
            }

            // Octave jump: a band above (+1) or below (-1) the note.
            const int f = octField(s);
            if (f == 2) addQuad(x0, y0 - 4.0f, x1, y0 - 1.0f, kOctave);
            if (f == 0) addQuad(x0, y1 + 1.0f, x1, y1 + 4.0f, kOctave);
        }

        // Hand edit: a thin outline around the cell's note/ghost position.
        if (s & kUserBit)
        {
            constexpr float t = 2.0f;
            addQuad(x0 - t, y0 - t, x1 + t, y0, kOutline);
            addQuad(x0 - t, y1, x1 + t, y1 + t, kOutline);
            addQuad(x0 - t, y0, x0, y1, kOutline);
            addQuad(x1, y0, x1 + t, y1, kOutline);
        }
    }

    m_geometry = rm->MakeGeometry(std::move(mesh));
    m_dirty = false;
    m_lastSize = Rml::Vector2f(w, h);
}

void JtRmlBassGrid::OnRender()
{
    Rml::Element::OnRender();
    if (m_dirty
        || std::fabs(m_lastSize.x - GetClientWidth()) > 0.5f
        || std::fabs(m_lastSize.y - GetClientHeight()) > 0.5f)
        rebuildGeometry();
    if (m_geometry)
        m_geometry.Render(GetAbsoluteOffset());
}

} // namespace jt
