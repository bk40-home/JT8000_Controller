/*
 * JtRmlSystem.cpp — implementation
 * See JtRmlSystem.h for the reasoning on the clock and the cursor.
 */

#include "JtRmlSystem.h"

#include <juce_core/juce_core.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace jt
{

JtRmlSystem::JtRmlSystem()
    : m_start(juce::Time::getMillisecondCounterHiRes() * 0.001)
{
}

double JtRmlSystem::GetElapsedTime()
{
    /* Monotonic. Wall time would let the delta go negative if the machine
     * re-syncs its clock, and RmlUi's animation stepping does not expect that. */
    return juce::Time::getMillisecondCounterHiRes() * 0.001 - m_start;
}

bool JtRmlSystem::LogMessage(const Rml::Log::Type type,
                             const Rml::String& message)
{
    const char* tag = "rml";
    switch (type)
    {
    case Rml::Log::LT_ERROR:   tag = "rml ERROR";   break;
    case Rml::Log::LT_ASSERT:  tag = "rml ASSERT";  break;
    case Rml::Log::LT_WARNING: tag = "rml warn";    break;
    case Rml::Log::LT_INFO:    tag = "rml info";    break;
    case Rml::Log::LT_DEBUG:   tag = "rml debug";   break;
    default: break;
    }

    /* Warnings and errors always surface: a mistyped property or a missing
     * sprite is reported here and nowhere else, and silently swallowing it is
     * how a skin ends up mysteriously blank. */
    const bool important = (type == Rml::Log::LT_ERROR
                            || type == Rml::Log::LT_ASSERT
                            || type == Rml::Log::LT_WARNING);
    const juce::String line = juce::String(tag) + ": "
                              + juce::String(message.c_str());

    if (important || m_verbose)
        juce::Logger::writeToLog(line);

    /* Kept regardless of the verbose flag: this ring is what the panel shows
     * when it cannot draw, and a Release build has no console. */
    {
        const std::lock_guard<std::mutex> lock(m_logMutex);
        m_log.push_back(line);
        while (m_log.size() > kLogKeep)
            m_log.pop_front();
    }

    return true;        // true: continue execution
}

juce::StringArray JtRmlSystem::recentLog() const
{
    const std::lock_guard<std::mutex> lock(m_logMutex);
    juce::StringArray out;
    for (const auto& l : m_log)
        out.add(l);
    return out;
}

void JtRmlSystem::SetMouseCursor(const Rml::String& cursorName)
{
    if (cursorName == m_cursor)
        return;
    m_cursor = cursorName;
    m_cursorDirty.store(true, std::memory_order_release);
}

bool JtRmlSystem::takeCursorIfChanged(Rml::String& name)
{
    if (!m_cursorDirty.exchange(false, std::memory_order_acq_rel))
        return false;
    name = m_cursor;
    return true;
}

void JtRmlSystem::SetClipboardText(const Rml::String& text)
{
    juce::SystemClipboard::copyTextToClipboard(juce::String(text.c_str()));
}

void JtRmlSystem::GetClipboardText(Rml::String& text)
{
    text = juce::SystemClipboard::getTextFromClipboard().toStdString();
}

} // namespace jt
