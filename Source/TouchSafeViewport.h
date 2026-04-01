/*
 * TouchSafeViewport.h — Touch-aware Viewport for JT-8000 Controller
 *
 * PROBLEM:
 *   juce::Viewport's built-in touch scroll intercepts vertical drag
 *   gestures.  When the user drags a rotary knob or vertical slider,
 *   the Viewport also scrolls — making knob editing unusable on
 *   touch screens.
 *
 * SOLUTION:
 *   Override the Viewport so that touch-initiated scrolling is only
 *   allowed when the drag starts on a non-interactive area (empty
 *   panel background, section headers, labels).  If the touch lands
 *   on a Slider, ComboBox, ToggleButton, or StepSequencerGrid the
 *   Viewport ignores the drag and lets the control handle it.
 *
 * HOW IT WORKS:
 *   1. On mouseDown, walk up from the clicked component to see if it
 *      (or any ancestor below the Viewport) is an interactive control.
 *   2. If yes → suppress scroll for this gesture.
 *   3. If no  → allow normal Viewport scroll behaviour.
 *
 *   Mouse-wheel scrolling is always allowed (not affected by touch).
 *
 * USAGE:
 *   Drop-in replacement for juce::Viewport in PluginEditor.
 *   Change the member type from juce::Viewport to TouchSafeViewport.
 */

#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "StepSequencerGrid.h"

class TouchSafeViewport : public juce::Viewport
{
public:
    TouchSafeViewport(const juce::String& name = {})
        : juce::Viewport(name)
    {
        // Disable JUCE's built-in drag-to-scroll entirely.
        // We re-implement it selectively in the mouse callbacks below.
        setScrollOnDragMode(juce::Viewport::ScrollOnDragMode::never);
    }

    // =========================================================================
    // Mouse overrides — manual drag-scroll only on non-interactive areas
    // =========================================================================

    void mouseDown(const juce::MouseEvent& e) override
    {
        _dragScrollActive = false;
        _dragStartY = 0;
        _scrollStartY = 0;

        // Check if the click target is an interactive control.
        // getComponentAt() returns the deepest child under the click point.
        if (auto* viewed = getViewedComponent())
        {
            auto localPt = viewed->getLocalPoint(this, e.getPosition());
            auto* hitComp = viewed->getComponentAt(localPt);

            if (hitComp != nullptr && isInteractiveControl(hitComp))
            {
                // Let the control handle the gesture — no viewport scroll.
                juce::Viewport::mouseDown(e);
                return;
            }
        }

        // Non-interactive area — we will handle drag-scroll manually.
        _dragScrollActive = true;
        _dragStartY = e.getPosition().getY();
        _scrollStartY = getViewPositionY();

        juce::Viewport::mouseDown(e);
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (_dragScrollActive)
        {
            const int dy = _dragStartY - e.getPosition().getY();
            setViewPosition(getViewPositionX(), _scrollStartY + dy);
            return; // Consume — do not forward to Viewport base (which does nothing anyway)
        }

        juce::Viewport::mouseDrag(e);
    }

    void mouseUp(const juce::MouseEvent& e) override
    {
        _dragScrollActive = false;
        juce::Viewport::mouseUp(e);
    }

private:
    bool _dragScrollActive = false;
    int  _dragStartY = 0;
    int  _scrollStartY = 0;

    // =========================================================================
    // isInteractiveControl — returns true if comp (or an ancestor below the
    // Viewport's viewed component) is a slider, combo, toggle, or grid.
    //
    // Walk up the parent chain so that child sub-components of a Slider
    // (e.g. the thumb, text box) also count as interactive.
    // =========================================================================
    bool isInteractiveControl(juce::Component* comp) const
    {
        auto* viewed = getViewedComponent();

        while (comp != nullptr && comp != viewed && comp != this)
        {
            if (dynamic_cast<juce::Slider*>(comp) != nullptr)       return true;
            if (dynamic_cast<juce::ComboBox*>(comp) != nullptr)     return true;
            if (dynamic_cast<juce::ToggleButton*>(comp) != nullptr) return true;
            if (dynamic_cast<juce::Button*>(comp) != nullptr)       return true;

            // StepSequencerGrid handles mouse drag for bar editing.
            if (dynamic_cast<StepSequencerGrid*>(comp) != nullptr) return true;

            comp = comp->getParentComponent();
        }
        return false;
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TouchSafeViewport)
};
