/*
 * JtTheme.h — Shared JT-8000 colour palette (single source of truth)
 *
 * The dark-navy theme with the orange accent was previously hardcoded as
 * inline hex literals in SyxPickerOverlay, JT8000LookAndFeel and several
 * grid/display components — six files independently repeating 0xfff08020.
 * That drift is why the SYX picker slowly stopped matching the panels behind
 * it.  This header centralises the palette so every component references one
 * set of tokens.  Values are taken verbatim from the existing theme, so
 * adopting it changes nothing visually — it only removes the duplication.
 *
 * Usage:
 *     #include "JtTheme.h"
 *     g.setColour(JtTheme::accent);
 *     label.setColour(juce::Label::textColourId, JtTheme::textHi);
 */

#pragma once
#include <juce_graphics/juce_graphics.h>

namespace JtTheme
{
    // ---- Surfaces ----
    inline const juce::Colour bg          { 0xff0b0e14 };  // deepest background
    inline const juce::Colour panel       { 0xff141828 };  // panel / knob body
    inline const juce::Colour panel2      { 0xff111620 };  // dialog / raised panel
    inline const juce::Colour row         { 0xff161c28 };  // list row / button bg
    inline const juce::Colour rowSelected { 0xff1d2740 };  // selected list row

    // ---- Borders ----
    inline const juce::Colour border      { 0xff2e3a50 };  // primary border
    inline const juce::Colour borderSoft  { 0xff1e2a3e };  // subtle divider / scale line

    // ---- Text ----
    inline const juce::Colour textHi      { 0xffc8d8f4 };  // primary / selected text
    inline const juce::Colour text        { 0xff8090b0 };  // secondary text
    inline const juce::Colour textDim     { 0xff5a6a8a };  // tertiary / captions

    // ---- Accent + semantics ----
    inline const juce::Colour accent      { 0xfff08020 };  // JT-8000 orange (identity)
    inline const juce::Colour accentDim   { 0xff3a2a14 };  // low-alpha accent fill / tag bg
    inline const juce::Colour onAccent    { 0xff0b0e14 };  // text on an accent fill
    inline const juce::Colour green       { 0xff28c060 };  // Layer A / positive
    inline const juce::Colour purple      { 0xff9060ff };  // Layer B / alternate

    // ---- Scrim (modal/overlay backdrop) ----
    inline const juce::Colour scrim       { 0xcc000000 };  // dim behind a non-modal panel
}
