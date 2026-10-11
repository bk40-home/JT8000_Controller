// =============================================================================
// JtEncoderStrip.h — on-screen stand-in for a hardware encoder + push switch
// =============================================================================
//
// The plugin has no physical encoder, so the LCD menu is driven by this one
// dial. Three gestures, mapped 1:1 onto the menu's primitives, so a real
// encoder with a push switch would wire in identically later:
//
//     rotate            drag up/down, or the mouse wheel  -> onEncoder(delta)
//     short press       click and release (no drag)       -> onEnter()
//     press-and-hold    hold past kHoldMs (no drag)       -> onBack()
//
// The dial spins as it turns: each detent advances a rendered angle that eases
// toward its target on a timer. It is an endless encoder, so the angle is free
// to pass 360 and keep going — there is no travel limit.
//
// APPEARANCE
//   setKnobImage() supplies the same artwork the panel knobs use; the dial then
//   draws that image rotated by the current angle. Until an image is set (or if
//   it is invalid) it falls back to a clean procedural knob. No text is drawn.
//
// It carries no menu state: every gesture is reported as a delta or a press,
// and the menu owns all navigation/edit state.
// =============================================================================

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace jt
{

class JtEncoderStrip : public juce::Component,
                       private juce::Timer
{
public:
    JtEncoderStrip();

    // Rotate. `coarse` is true while Shift is held, so continuous params can
    // jump in larger steps; the menu decides what that means per parameter.
    std::function<void(int delta, bool coarse)> onEncoder;

    // Short press (click and release without dragging or holding).
    std::function<void()> onEnter;

    // Press-and-hold past kHoldMs — steps the menu out one level.
    std::function<void()> onBack;

    // The artwork to rotate — pass the panel's knob image so the dial matches
    // the knobs. An invalid image keeps the procedural fallback.
    void setKnobImage(juce::Image image);

    void resized() override;
    void paint(juce::Graphics&) override;

private:
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&,
                        const juce::MouseWheelDetails&) override;
    void timerCallback() override;          // drives the hold AND the spin ease

    void turn(int dir, bool coarse);        // one detent: emit + advance angle
    void ensureTicking();                   // start the timer if work is pending

    juce::Image          m_knob;            // optional artwork (else procedural)
    juce::Rectangle<int> m_dial;            // the whole face, hit-tested here

    // Rotation, in radians. m_angle eases toward m_angleTarget each tick.
    float m_angle       = 0.0f;
    float m_angleTarget = 0.0f;

    juce::Point<int> m_lastDrag;
    int              m_dragAccum = 0;        // accumulated pixels -> detents

    bool     m_pressing   = false;           // a press is in progress
    bool     m_dragged    = false;           // it turned into a rotation
    bool     m_longFired  = false;           // the hold already fired onBack
    uint32_t m_pressStart = 0;               // ms counter at mouseDown

    static constexpr int   kPixelsPerDetent = 8;      // drag sensitivity
    static constexpr int   kHoldMs          = 450;    // press-and-hold -> BACK
    static constexpr int   kTickMs          = 16;     // ~60 Hz animation
    static constexpr float kAnglePerDetent  = 0.30f;  // radians per detent (~17°)
    static constexpr float kEase            = 0.35f;  // spin smoothing

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JtEncoderStrip)
};

} // namespace jt
