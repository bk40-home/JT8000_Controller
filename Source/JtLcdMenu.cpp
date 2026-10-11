// =============================================================================
// JtLcdMenu.cpp — implementation
// See JtLcdMenu.h for the three-level navigation, single source of truth, one
// write path and one LCD writer.
// =============================================================================

#include "JtLcdMenu.h"

#include "gen/JtParamTable.h"   // JtParams::kParams / kSectionNames

namespace jt
{

namespace
{
    // Continuous nudge, normalised. Fine == one 7-bit wire step, so the LCD
    // moves the value at the resolution the firmware receives; coarse is a
    // usable jump for long sweeps. Discrete params step by whole indices.
    constexpr float kFineStep   = 1.0f / 127.0f;
    constexpr float kCoarseStep = 1.0f / 16.0f;

    juce::String sectionName(uint8_t section)
    {
        return (section < JtParams::kNumSections)
                   ? juce::String(JtParams::kSectionNames[section])
                   : juce::String();
    }
}

// =============================================================================
JtLcdMenu::JtLcdMenu(juce::AudioProcessorValueTreeState& state)
    : m_state(state)
{
    // Group parameters by section, preserving table order both between sections
    // (first appearance wins) and within each. Only sections that actually have
    // a parameter become navigable.
    for (int i = 0; i < JtParams::kNumParams; ++i)
    {
        const uint8_t sec = JtParams::kParams[i].section;

        Section* slot = nullptr;
        for (auto& s : m_sections)
            if (s.index == sec) { slot = &s; break; }

        if (slot == nullptr)
        {
            m_sections.push_back({ sec, {} });
            slot = &m_sections.back();
        }
        slot->params.push_back(i);
    }
}

// =============================================================================
// Selection helpers
// =============================================================================
int JtLcdMenu::currentTableIndex() const
{
    if (m_sections.empty())
        return -1;
    const auto& sec = m_sections[(size_t) m_secCursor];
    if (sec.params.empty())
        return -1;
    return sec.params[(size_t) m_parCursor];
}

juce::RangedAudioParameter* JtLcdMenu::currentParam() const
{
    const int idx = currentTableIndex();
    if (idx < 0)
        return nullptr;
    return m_state.getParameter(JtParams::kParams[idx].juceId);
}

// -----------------------------------------------------------------------------
// One encoder detent in edit mode. Discrete params step by whole indices off
// the generated table (no fragile dynamic_cast); continuous params nudge in
// normalised space. Every path ends at setValueNotifyingHost — the one write
// path — so the host, undo and the NRPN bridge all see the change.
// -----------------------------------------------------------------------------
void JtLcdMenu::applyStep(int delta, bool coarse)
{
    auto* p = currentParam();
    const int idx = currentTableIndex();
    if (p == nullptr || idx < 0 || delta == 0)
        return;

    const auto& r = JtParams::kParams[idx];
    const float cur = p->getValue();               // normalised 0..1
    float newNorm = cur;

    if (r.widget == JtParams::Widget::Combo)
    {
        const int count = juce::jmax(1, (int) r.optionCount);
        const int i0    = (count > 1) ? juce::roundToInt(cur * (count - 1)) : 0;
        const int i1    = juce::jlimit(0, count - 1, i0 + delta);
        newNorm = (count > 1) ? (float) i1 / (float) (count - 1) : 0.0f;
    }
    else if (r.widget == JtParams::Widget::Toggle)
    {
        const int i0 = (cur >= 0.5f) ? 1 : 0;
        newNorm = (float) juce::jlimit(0, 1, i0 + delta);
    }
    else if (r.type == JtParams::Type::Int)
    {
        const int lo   = juce::roundToInt(r.min);
        const int hi   = juce::roundToInt(r.max);
        const int span = juce::jmax(1, hi - lo);
        const int v0   = lo + juce::roundToInt(cur * span);
        const int v1   = juce::jlimit(lo, hi, v0 + delta);
        newNorm = (float) (v1 - lo) / (float) span;
    }
    else // Continuous
    {
        const float step = coarse ? kCoarseStep : kFineStep;
        newNorm = juce::jlimit(0.0f, 1.0f, cur + (float) delta * step);
    }

    if (! juce::approximatelyEqual(newNorm, cur))
        p->setValueNotifyingHost(newNorm);         // THE write path
}

// =============================================================================
// Input primitives
// =============================================================================
void JtLcdMenu::encoder(int delta, bool coarse)
{
    if (delta == 0 || m_sections.empty())
        return;

    switch (m_mode)
    {
        case Mode::Idle:
            m_mode = Mode::Sections;               // first turn wakes the menu
            [[fallthrough]];
        case Mode::Sections:
            m_secCursor = juce::jlimit(0, (int) m_sections.size() - 1,
                                       m_secCursor + delta);
            m_parCursor = 0;                       // fresh section starts at top
            break;
        case Mode::Params:
        {
            const int n = (int) m_sections[(size_t) m_secCursor].params.size();
            m_parCursor = juce::jlimit(0, juce::jmax(0, n - 1),
                                       m_parCursor + delta);
            break;
        }
        case Mode::Edit:
            applyStep(delta, coarse);
            break;
    }
    render();
}

void JtLcdMenu::enter()
{
    if (m_sections.empty())
        return;

    switch (m_mode)
    {
        case Mode::Idle:     m_mode = Mode::Sections; break;
        case Mode::Sections: m_mode = Mode::Params; m_parCursor = 0; break;
        case Mode::Params:   m_mode = Mode::Edit;   break;
        case Mode::Edit:     m_mode = Mode::Params; break;   // confirm (live)
    }
    render();
}

void JtLcdMenu::back()
{
    switch (m_mode)
    {
        case Mode::Edit:     m_mode = Mode::Params;   render(); break;
        case Mode::Params:   m_mode = Mode::Sections; render(); break;
        case Mode::Sections:
            m_mode = Mode::Idle;                    // hand the LCD back
            if (onLcd)
                onLcd(m_patchName, {});
            break;
        case Mode::Idle:
            break;
    }
}

// =============================================================================
// Idle-side updates. In the menu, transient readouts are dropped so they cannot
// fight the menu for the two lines.
// =============================================================================
void JtLcdMenu::showTransient(const juce::String& line1,
                              const juce::String& line2)
{
    if (line2.isEmpty())                            // patch-name push
        m_patchName = line1;

    if (m_mode == Mode::Idle && onLcd)
        onLcd(line1, line2);
}

void JtLcdMenu::setPatchName(const juce::String& name)
{
    m_patchName = name;
    if (m_mode == Mode::Idle && onLcd)
        onLcd(m_patchName, {});
}

void JtLcdMenu::refresh()
{
    // Only Params and Edit show a live parameter value; those can fall stale
    // when the value changes outside the menu.
    if (m_mode == Mode::Params || m_mode == Mode::Edit)
        render();
}

// =============================================================================
// Rendering (menu levels only; Idle is driven by showTransient)
// =============================================================================
void JtLcdMenu::render()
{
    if (! onLcd || m_sections.empty())
        return;

    const auto& sec = m_sections[(size_t) m_secCursor];

    if (m_mode == Mode::Sections)
    {
        juce::String l1, l2;
        l1 << sectionName(sec.index);
        l2 << "Section " << (m_secCursor + 1) << "/" << (int) m_sections.size()
           << "   " << (int) sec.params.size() << " params";
        onLcd(l1, l2);
        return;
    }

    // Params / Edit — both show the selected parameter and its value.
    const int idx = currentTableIndex();
    if (idx < 0)
        return;

    const auto& p  = JtParams::kParams[idx];
    auto* rp       = currentParam();
    const juce::String value = (rp != nullptr) ? rp->getCurrentValueAsText()
                                               : juce::String();

    juce::String l1;
    l1 << sectionName(sec.index) << "  " << (m_parCursor + 1) << "/"
       << (int) sec.params.size();

    juce::String l2;
    if (m_mode == Mode::Edit)
        l2 << p.name << "  [" << value << "]";      // brackets = actively editing
    else
        l2 << p.name << "  " << value;

    onLcd(l1, l2);
}

} // namespace jt
