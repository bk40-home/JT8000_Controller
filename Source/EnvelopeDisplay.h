/*
 * EnvelopeDisplay.h — Visual ADSR envelope editor for JT-8000 Controller
 *
 * Custom juce::Component that draws the envelope shape with draggable dots.
 * Listens to APVTS parameters for live updates from hardware or automation.
 *
 * Three flavours: AMP (unipolar, 7 points), FILTER/PITCH (bipolar, 8 points).
 * Shows parameter values as small labels next to each dot.
 *
 * INTEGRATION:
 *   Call SectionComponent::addEnvelope(apvts, flavour) which creates one of
 *   these and wires it into the flow layout.
 */

#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "CCDefinitions.h"

// ---------------------------------------------------------------------------
//  Envelope flavour
// ---------------------------------------------------------------------------
enum class JuceEnvFlavour { AMP, FILTER, PITCH };

// ---------------------------------------------------------------------------
//  EnvelopeDisplay
// ---------------------------------------------------------------------------
class EnvelopeDisplay : public juce::Component,
                        private juce::Timer
{
public:
    EnvelopeDisplay(juce::AudioProcessorValueTreeState& apvts,
                    JuceEnvFlavour flavour);
    ~EnvelopeDisplay() override;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;

private:
    // --- timer for polling parameter changes ---
    void timerCallback() override;

    // --- mapping helpers ---
    static float ccToTimeMs(float cc);
    static float ccToCurve(float cc);
    static float curveToAlpha(float curve);

    // --- point definitions ---
    enum PointId {
        PT_ATK_TIME = 0, PT_ATK_CURVE, PT_DEPTH,
        PT_DEC_TIME, PT_DEC_CURVE, PT_SUS_LEVEL,
        PT_REL_TIME, PT_REL_CURVE,
        PT_COUNT
    };

    struct PointDef {
        const char* paramId;    // APVTS parameter ID
        const char* label;      // display label (e.g. "ATK")
    };

    static const PointDef kAmpPoints[PT_COUNT];
    static const PointDef kFilterPoints[PT_COUNT];
    static const PointDef kPitchPoints[PT_COUNT];

    const PointDef* _points;
    int _pointCount;    // 7 for amp, 8 for bipolar

    // --- layout computation ---
    struct StageLayout {
        float xAtk, xDec, xSus, xEnd;
        float yBase, yPeak, ySustain;
        float x0, y0, drawW, drawH;
    };

    StageLayout computeLayout() const;
    juce::Point<float> getPointPosition(int ptId, const StageLayout& L) const;

    // --- draw helpers ---
    void drawCurvedSegment(juce::Graphics& g, float x0, float y0,
                           float x1, float y1, float curve,
                           juce::Colour colour, float thickness) const;

    juce::Point<float> curveMidpoint(float x0, float y0,
                                     float x1, float y1,
                                     float curve) const;

    // --- read parameter value (0-127 float) ---
    float getParamValue(int ptId) const;

    // --- format value for display ---
    juce::String formatValue(int ptId, float ccVal) const;

    // --- state ---
    juce::AudioProcessorValueTreeState& _apvts;
    JuceEnvFlavour _flavour;
    int _selectedPt = -1;       // currently selected point (-1 = none)
    int _dragPt     = -1;       // point being dragged

    // cached param values for change detection
    float _cachedVals[PT_COUNT] = {};

    // --- colours (matching JT8000LookAndFeel) ---
    static constexpr juce::uint32 kColBg       = 0xff0a1018;
    static constexpr juce::uint32 kColPanel     = 0xff111620;
    static constexpr juce::uint32 kColBorder    = 0xff1e2a3e;
    static constexpr juce::uint32 kColLine      = 0xfff08020;   // orange
    static constexpr juce::uint32 kColLineDim   = 0xff6a3508;   // dim orange
    static constexpr juce::uint32 kColDot       = 0xfff08020;   // orange
    static constexpr juce::uint32 kColDotSel    = 0xffffa040;   // bright orange
    static constexpr juce::uint32 kColLabel     = 0xff8090b0;
    static constexpr juce::uint32 kColValue     = 0xffc8d8f4;
    static constexpr juce::uint32 kColBaseline  = 0xff2e3a50;

    static constexpr float kDotRadius    = 5.0f;
    static constexpr float kDotRadiusSel = 8.0f;
    static constexpr float kHitRadius    = 14.0f;  // click target
    static constexpr float kPadX         = 12.0f;
    static constexpr float kPadY         = 20.0f;  // room for value labels above
    static constexpr float kPadBottom    = 14.0f;  // room for labels below

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EnvelopeDisplay)
};
