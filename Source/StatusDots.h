/*
 * StatusDots.h — JT-8000 engine status strip (Phase J1d)
 *
 * Eight voice-activity dots plus a RUN marker, fed from the NRPN 0x3FFF
 * status word (see firmware SynthCore::statusWord() and ValueLaw.h for the
 * packing).  Header-only, like TouchSafeViewport — it is a paint-only leaf
 * with no behaviour worth a .cpp.
 *
 * Change-driven: setStatus() repaints only when the visible state actually
 * differs, so the editor can call it every poll tick for free.  When the
 * link is stale (no status cluster for a few heartbeats) the strip greys
 * out — "wire dead", the same meaning the ESP32's LINK indicator carries.
 */

#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <cstdint>
#include "ValueLaw.h"

class StatusDots : public juce::Component
{
public:
    StatusDots()
    {
        setInterceptsMouseClicks(false, false);   // purely informational
    }

    // word = the raw 14-bit status payload; alive = link heartbeat seen
    // recently (the editor derives this from statusCount() movement).
    void setStatus(uint16_t word, bool alive)
    {
        if (word == _word && alive == _alive)
            return;                               // nothing visible changed
        _word  = word;
        _alive = alive;
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        const auto area = getLocalBounds().toFloat();
        const uint8_t mask    = ValueLaw::statusVoiceMask(_word);
        const bool    running = ValueLaw::statusRunning(_word);

        // Eight dots left-to-right (voice 0..7), then the RUN marker.
        // Sized off the strip height so the header can resize freely.
        const float d   = juce::jmin(area.getHeight() * 0.55f,
                                     area.getWidth() / 11.0f);
        const float gap = d * 0.45f;
        float x = area.getX();
        const float y = area.getCentreY() - d * 0.5f;

        for (int v = 0; v < 8; ++v)
        {
            const bool on = _alive && ((mask >> v) & 1);
            g.setColour(on ? kVoiceOn : (_alive ? kOff : kDead));
            if (on) g.fillEllipse(x, y, d, d);
            else    g.drawEllipse(x + 0.5f, y + 0.5f, d - 1.0f, d - 1.0f, 1.0f);
            x += d + gap;
        }

        // RUN marker: a small triangle, green while the sequencer runs.
        x += gap;
        g.setColour(_alive && running ? kRun : (_alive ? kOff : kDead));
        juce::Path tri;
        tri.addTriangle(x, y, x, y + d, x + d * 0.9f, y + d * 0.5f);
        if (_alive && running) g.fillPath(tri);
        else                   g.strokePath(tri, juce::PathStrokeType(1.0f));
    }

private:
    // Theme colours (match PluginEditor.cpp constants).
    static constexpr juce::uint32 kVoiceOnRgb = 0xfff08020;   // orange = sounding
    static constexpr juce::uint32 kRunRgb     = 0xff28ff58;   // green  = running
    static constexpr juce::uint32 kOffRgb     = 0xff2a3650;   // idle outline
    static constexpr juce::uint32 kDeadRgb    = 0xff1a2030;   // link stale

    const juce::Colour kVoiceOn { kVoiceOnRgb };
    const juce::Colour kRun     { kRunRgb };
    const juce::Colour kOff     { kOffRgb };
    const juce::Colour kDead    { kDeadRgb };

    uint16_t _word  = 0;
    bool     _alive = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StatusDots)
};
