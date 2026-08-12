/*
 * JtRmlBinding.cpp — implementation
 * See JtRmlBinding.h for the echo problem and the threading rule.
 */

#include "JtRmlBinding.h"

#include "JtRmlComponent.h"

namespace jt
{

namespace
{
    constexpr int kDrainHz = 30;

    /* The document already carries APVTS ids: gen_skin.py resolves the panel's
     * dotted keys through JtBridgeTable.h and CCDefinitions.h and writes the id
     * into the `param` attribute, keeping the dotted key alongside in `key` for
     * diagnostics. Only 26 of 160 names match by substitution, so doing this at
     * runtime would have meant shipping the same join in C++.
     *
     * A control whose parameter does not exist gets an empty `param` and is
     * reported by the emitter, so it fails visibly at build time rather than
     * silently as a dead knob. */
    juce::String toParamId(const juce::String& key)
    {
        return key;
    }
}

// =============================================================================
JtRmlBinding::JtRmlBinding(juce::AudioProcessorValueTreeState& state,
                           JtRmlComponent& panel)
    : m_state(state), m_panel(panel)
{
    /* One Pending slot per parameter, allocated up front: allocating on the
     * audio thread when a value arrives would be a real-time violation. */
    for (auto* p : m_state.processor.getParameters())
    {
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(p))
        {
            const juce::String id = rp->paramID;
            m_ids.push_back(id);
            m_pending.emplace(id.toStdString(), std::make_unique<Pending>());
            m_state.addParameterListener(id, this);
        }
    }

    m_panel.onParameterChanged = [this](const juce::String& key, float v)
    {
        controlMoved(key, v);
    };
    m_panel.onHover = [this](const juce::String& key) { hoverChanged(key); };

    startTimerHz(kDrainHz);
}

JtRmlBinding::~JtRmlBinding()
{
    stopTimer();
    for (const auto& id : m_ids)
        m_state.removeParameterListener(id, this);

    m_panel.onParameterChanged = nullptr;
    m_panel.onHover = nullptr;
}

// =============================================================================
// Panel -> APVTS
// =============================================================================
void JtRmlBinding::controlMoved(const juce::String& key, const float normalised)
{
    if (m_pushing)
        return;                     // this move came from us; do not echo it

    auto* p = m_state.getParameter(toParamId(key));
    if (p == nullptr)
        return;

    /* setValueNotifyingHost, not setValue: the host has to learn about the
     * change too, or automation recording and undo miss it. */
    p->setValueNotifyingHost(normalised);

    if (key == m_hovered && onLcdChanged)
        onLcdChanged(m_patchName,
                     p->getName(24) + "   " + displayValue(*p));
}

// =============================================================================
// APVTS -> panel
// =============================================================================
void JtRmlBinding::parameterChanged(const juce::String& id, const float newValue)
{
    /* This may be the audio thread. Park the value; the timer applies it. */
    const auto it = m_pending.find(id.toStdString());
    if (it == m_pending.end())
        return;

    auto* p = m_state.getParameter(id);
    const float norm = (p != nullptr)
        ? p->convertTo0to1(newValue)
        : newValue;

    it->second->value.store(norm, std::memory_order_relaxed);
    it->second->dirty.store(true, std::memory_order_release);
}

void JtRmlBinding::timerCallback()
{
    for (const auto& id : m_ids)
    {
        Pending& slot = *m_pending[id.toStdString()];
        if (!slot.dirty.exchange(false, std::memory_order_acq_rel))
            continue;

        const juce::ScopedValueSetter<bool> guard(m_pushing, true);
        m_panel.setParameterValue(id, slot.value.load(std::memory_order_relaxed));
    }
}

void JtRmlBinding::syncAll()
{
    const juce::ScopedValueSetter<bool> guard(m_pushing, true);
    for (const auto& id : m_ids)
        if (auto* p = m_state.getParameter(id))
            m_panel.setParameterValue(id, p->getValue());
}

// =============================================================================
// LCD
// =============================================================================
juce::String JtRmlBinding::displayValue(const juce::RangedAudioParameter& p)
{
    /* The parameter formats itself: units, curves and option names all live in
     * the parameter model, and duplicating them here is how a UI ends up
     * disagreeing with the engine. */
    return p.getCurrentValueAsText();
}

void JtRmlBinding::hoverChanged(const juce::String& key)
{
    m_hovered = key;

    if (!onLcdChanged)
        return;

    if (key.isEmpty())
    {
        onLcdChanged(m_patchName, {});
        return;
    }

    auto* p = m_state.getParameter(toParamId(key));
    if (p == nullptr)
    {
        onLcdChanged(m_patchName, key);
        return;
    }

    if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(p))
        onLcdChanged(m_patchName, rp->getName(24) + "   " + displayValue(*rp));
}

void JtRmlBinding::setPatchName(juce::String name)
{
    m_patchName = std::move(name);
    if (onLcdChanged)
        onLcdChanged(m_patchName, {});
}

} // namespace jt
