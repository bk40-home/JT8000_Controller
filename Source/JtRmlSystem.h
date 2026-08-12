/*
 * JtRmlSystem.h — RmlUi SystemInterface backed by JUCE
 *
 * RmlUi asks the host for time, logging, the mouse cursor, and the clipboard.
 * None of it is difficult; the parts worth commenting are the two that are easy
 * to get subtly wrong.
 *
 * TIME
 *   RmlUi drives animations, transitions and its own timers from
 *   GetElapsedTime(). It must be a MONOTONIC high-resolution clock, not wall
 *   time: a wall clock can step backwards when the machine syncs, and RmlUi
 *   will produce a negative delta and misbehave. juce::Time's high-resolution
 *   counter is monotonic.
 *
 * CURSOR
 *   RmlUi tells us WHICH cursor it wants, by name, at the moment it decides.
 *   It cannot set one itself, and a juce::Component can only change its cursor
 *   from the message thread. So the request is recorded here and the host
 *   component applies it after Context::Update() - see takeCursorIfChanged().
 *
 * A note on what is deliberately NOT implemented: ActivateKeyboard and
 * DeactivateKeyboard are for platforms with a soft keyboard. On desktop they
 * are no-ops, and the base class already does nothing, so they are not
 * overridden at all.
 */

#pragma once

#include <RmlUi/Core/SystemInterface.h>

#include <juce_core/juce_core.h>

#include <atomic>
#include <deque>
#include <mutex>

namespace jt
{

class JtRmlSystem final : public Rml::SystemInterface
{
public:
    JtRmlSystem();
    ~JtRmlSystem() override = default;

    /* Seconds since construction, from a monotonic clock. */
    double GetElapsedTime() override;

    bool LogMessage(Rml::Log::Type type, const Rml::String& message) override;

    void SetMouseCursor(const Rml::String& cursorName) override;
    void SetClipboardText(const Rml::String& text) override;
    void GetClipboardText(Rml::String& text) override;

    /* Cursor requests are recorded rather than applied, because only the host
     * component can set a cursor and only on the message thread. Returns true
     * and fills `name` when the request has changed since the last call, so the
     * host can skip the work when nothing moved. */
    bool takeCursorIfChanged(Rml::String& name);

    /* Set false to silence RmlUi's info/debug chatter in release builds while
     * keeping warnings and errors. */
    void setVerboseLogging(bool v) { m_verbose = v; }

    /* The last few messages RmlUi produced. A document that fails to load says
     * exactly why here and nowhere else, and in a Release plugin there is no
     * console to read it from - so the panel draws it instead. */
    juce::StringArray recentLog() const;

private:
    double m_start = 0.0;
    Rml::String m_cursor;
    std::atomic<bool> m_cursorDirty{false};
    bool m_verbose = true;

    mutable std::mutex m_logMutex;
    std::deque<juce::String> m_log;      // most recent last, capped
    static constexpr std::size_t kLogKeep = 12;
};

} // namespace jt
