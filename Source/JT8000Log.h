/*
 * JT8000Log.h — File-based diagnostic logger for VST3 debugging
 *
 * DBG() output is invisible in VST3 hosts on Windows unless a debug viewer
 * (e.g. Sysinternals DebugView) is running. This logger writes to a plain
 * text file so diagnostics are always available.
 *
 * Log file location: %TEMP%\JT8000_debug.log
 *   (typically C:\Users\<you>\AppData\Local\Temp\JT8000_debug.log)
 *
 * Usage:
 *   JT8000Log::write("message");
 *   JT8000Log::write("value = " + juce::String(42));
 *
 * The file is opened in append mode so entries accumulate across sessions.
 * Delete the file manually when no longer needed.
 *
 * REMOVE THIS FILE from the build once VST3 SysEx output is confirmed working.
 */

#pragma once
#include <juce_core/juce_core.h>

namespace JT8000Log
{

/** Append one timestamped line to the log file. Thread-safe via CriticalSection. */
inline void write(const juce::String& msg)
{
    static juce::CriticalSection lock;
    const juce::ScopedLock sl(lock);

    auto logFile = juce::File::getSpecialLocation(
        juce::File::tempDirectory).getChildFile("JT8000_debug.log");

    logFile.appendText(
        juce::Time::getCurrentTime().toString(true, true, true, true)
        + "  " + msg + "\n");
}

} // namespace JT8000Log
