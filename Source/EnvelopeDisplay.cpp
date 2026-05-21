/*
 * EnvelopeDisplay.cpp — Visual ADSR envelope editor for JT-8000 Controller
 *
 * See EnvelopeDisplay.h for overview.
 */

#include "EnvelopeDisplay.h"

// ===========================================================================
//  Point definitions per flavour — APVTS param IDs must match CCDefinitions.h
//
//  NOTE: Curve CCs (147-155) must be added to CCDefinitions.h / kAllParams[]
//  as KNOB-style params before this component will work.  Suggested IDs:
//    amp_atk_crv, amp_dec_crv, amp_rel_crv
//    fe_atk_crv,  fe_dec_crv,  fe_rel_crv
//    pe_atk_crv,  pe_dec_crv,  pe_rel_crv
// ===========================================================================

const EnvelopeDisplay::PointDef EnvelopeDisplay::kAmpPoints[PT_COUNT] = {
    { "amp_a",       "ATK"  },   // PT_ATK_TIME
    { "amp_atk_crv", "CRV"  },   // PT_ATK_CURVE
    { nullptr,       ""     },   // PT_DEPTH — not used for amp
    { "amp_d",       "DEC"  },   // PT_DEC_TIME
    { "amp_dec_crv", "CRV"  },   // PT_DEC_CURVE
    { "amp_s",       "SUS"  },   // PT_SUS_LEVEL
    { "amp_r",       "REL"  },   // PT_REL_TIME
    { "amp_rel_crv", "CRV"  },   // PT_REL_CURVE
};

const EnvelopeDisplay::PointDef EnvelopeDisplay::kFilterPoints[PT_COUNT] = {
    { "fe_a",        "ATK"   },
    { "fe_atk_crv",  "CRV"   },
    { "flt_env",     "DEPTH" },  // PT_DEPTH = filter env amount (CC 48)
    { "fe_d",        "DEC"   },
    { "fe_dec_crv",  "CRV"   },
    { "fe_s",        "SUS"   },
    { "fe_r",        "REL"   },
    { "fe_rel_crv",  "CRV"   },
};

const EnvelopeDisplay::PointDef EnvelopeDisplay::kPitchPoints[PT_COUNT] = {
    { "pe_a",        "ATK"   },
    { "pe_atk_crv",  "CRV"   },
    { "pe_depth",    "DEPTH" },
    { "pe_d",        "DEC"   },
    { "pe_dec_crv",  "CRV"   },
    { "pe_s",        "SUS"   },
    { "pe_r",        "REL"   },
    { "pe_rel_crv",  "CRV"   },
};


// ===========================================================================
//  Mapping helpers — must match firmware Mapping.h exactly
// ===========================================================================

static constexpr float kMsMin = 1.0f;
static constexpr float kMsMax = 11880.0f;

float EnvelopeDisplay::ccToTimeMs(float cc)
{
    float t = cc / 127.0f;
    return kMsMin * std::pow(kMsMax / kMsMin, t);
}

float EnvelopeDisplay::ccToCurve(float cc)
{
    if (cc <= 64.0f) return 0.15f + (cc / 64.0f) * 0.85f;
    return 1.0f + ((cc - 64.0f) / 63.0f) * 4.0f;
}

float EnvelopeDisplay::curveToAlpha(float curve)
{
    if (curve <= 1.0f) return -6.0f * (1.0f - curve) / 0.85f;
    return 6.0f * (curve - 1.0f) / 4.0f;
}


// ===========================================================================
//  Constructor / Destructor
// ===========================================================================

EnvelopeDisplay::EnvelopeDisplay(juce::AudioProcessorValueTreeState& apvts,
                                 JuceEnvFlavour flavour)
    : _apvts(apvts), _flavour(flavour)
{
    switch (flavour) {
        case JuceEnvFlavour::AMP:    _points = kAmpPoints;    _pointCount = 7; break;
        case JuceEnvFlavour::FILTER: _points = kFilterPoints;  _pointCount = 8; break;
        case JuceEnvFlavour::PITCH:  _points = kPitchPoints;   _pointCount = 8; break;
    }

    // poll parameters at 30 Hz for repaint
    startTimerHz(30);
}

EnvelopeDisplay::~EnvelopeDisplay()
{
    stopTimer();
}


// ===========================================================================
//  Parameter reading
// ===========================================================================

float EnvelopeDisplay::getParamValue(int ptId) const
{
    if (ptId < 0 || ptId >= PT_COUNT) return 0.0f;
    const char* pid = _points[ptId].paramId;
    if (pid == nullptr) return 64.0f;   // depth N/A for amp → return centre

    auto* param = _apvts.getRawParameterValue(pid);
    return param ? param->load() : 0.0f;
}


// ===========================================================================
//  Value formatting for labels
// ===========================================================================

juce::String EnvelopeDisplay::formatValue(int ptId, float ccVal) const
{
    // curve points: show as "Lin" at CC64, or the curve value
    if (ptId == PT_ATK_CURVE || ptId == PT_DEC_CURVE || ptId == PT_REL_CURVE) {
        float crv = ccToCurve(ccVal);
        if (std::abs(crv - 1.0f) < 0.05f) return "Lin";
        return juce::String(crv, 1);
    }

    // time points: show milliseconds
    if (ptId == PT_ATK_TIME || ptId == PT_DEC_TIME || ptId == PT_REL_TIME) {
        float ms = ccToTimeMs(ccVal);
        if (ms >= 1000.0f)
            return juce::String(ms / 1000.0f, 1) + "s";
        return juce::String((int)ms) + "ms";
    }

    // sustain: show as percentage
    if (ptId == PT_SUS_LEVEL) {
        return juce::String((int)(ccVal / 127.0f * 100.0f + 0.5f)) + "%";
    }

    // depth: show as bipolar
    if (ptId == PT_DEPTH) {
        int d = (int)ccVal - 64;
        if (d == 0) return "0";
        return juce::String(d);
    }

    return juce::String((int)ccVal);
}


// ===========================================================================
//  Layout computation
// ===========================================================================

EnvelopeDisplay::StageLayout EnvelopeDisplay::computeLayout() const
{
    StageLayout L;
    auto b = getLocalBounds().toFloat();
    L.x0    = b.getX() + kPadX;
    L.y0    = b.getY() + kPadY;
    L.drawW = b.getWidth() - 2.0f * kPadX;
    L.drawH = b.getHeight() - kPadY - kPadBottom;

    float ccAtk = getParamValue(PT_ATK_TIME);
    float ccDec = getParamValue(PT_DEC_TIME);
    float ccRel = getParamValue(PT_REL_TIME);
    float ccSus = getParamValue(PT_SUS_LEVEL);

    float msAtk = ccToTimeMs(ccAtk);
    float msDec = ccToTimeMs(ccDec);
    float msRel = ccToTimeMs(ccRel);
    float msSus = (msAtk + msDec + msRel) * 0.3f;
    if (msSus < 50.0f) msSus = 50.0f;

    float total = msAtk + msDec + msSus + msRel;
    if (total < 1.0f) total = 1.0f;

    // proportional widths with min/max clamping
    constexpr float minW = 30.0f, maxW = 200.0f, minZero = 6.0f;
    auto stageW = [&](float ms) -> float {
        float px = (ms / total) * L.drawW;
        if (ms > 0.0f && px < minW) px = minW;
        if (ms == 0.0f) px = minZero;
        if (px > maxW) px = maxW;
        return px;
    };

    float wAtk = stageW(msAtk);
    float wDec = stageW(msDec);
    float wSus = stageW(msSus);
    float wRel = stageW(msRel);

    // normalise to fit drawW
    float wTotal = wAtk + wDec + wSus + wRel;
    if (wTotal > 0.0f) {
        float scale = L.drawW / wTotal;
        wAtk *= scale; wDec *= scale; wSus *= scale;
        wRel = L.drawW - wAtk - wDec - wSus;
    }

    L.xAtk = L.x0 + wAtk;
    L.xDec = L.xAtk + wDec;
    L.xSus = L.xDec + wSus;
    L.xEnd = L.xSus + wRel;

    // vertical
    float susNorm = ccSus / 127.0f;

    if (_flavour == JuceEnvFlavour::AMP) {
        L.yBase    = L.y0 + L.drawH;
        L.yPeak    = L.y0;
        L.ySustain = L.y0 + (1.0f - susNorm) * L.drawH;
    } else {
        // bipolar: positive from bottom, negative from top (matches TFT)
        float ccDepth = getParamValue(PT_DEPTH);
        float depthNorm = (ccDepth - 64.0f) / 63.0f;
        depthNorm = juce::jlimit(-1.0f, 1.0f, depthNorm);

        if (depthNorm >= 0.0f) {
            L.yBase    = L.y0 + L.drawH;
            L.yPeak    = L.y0 + (1.0f - depthNorm) * L.drawH;
            float ext  = L.yBase - L.yPeak;
            L.ySustain = L.yBase - susNorm * ext;
        } else {
            L.yBase    = L.y0;
            L.yPeak    = L.y0 + (-depthNorm) * L.drawH;
            float ext  = L.yPeak - L.yBase;
            L.ySustain = L.yBase + susNorm * ext;
        }
    }

    return L;
}


// ===========================================================================
//  Curved segment drawing — geometric series polyline
// ===========================================================================

void EnvelopeDisplay::drawCurvedSegment(juce::Graphics& g,
    float x0, float y0, float x1, float y1,
    float curve, juce::Colour colour, float thickness) const
{
    constexpr int N = 12;   // segments per stage (higher res than TFT)
    float alpha = curveToAlpha(curve);

    juce::Path path;
    path.startNewSubPath(x0, y0);

    if (std::abs(alpha) < 0.01f) {
        // linear
        path.lineTo(x1, y1);
    } else {
        float r  = std::exp(alpha / (float)N);
        float rN = std::pow(r, (float)N);
        float denom = rN - 1.0f;
        if (std::abs(denom) < 1e-6f) {
            path.lineTo(x1, y1);
        } else {
            float inc = (r - 1.0f) / denom;
            float cum = 0.0f;
            for (int i = 1; i <= N; i++) {
                cum += inc;
                inc *= r;
                float t = (float)i / (float)N;
                float px = x0 + t * (x1 - x0);
                float py = y0 + cum * (y1 - y0);
                path.lineTo(px, py);
            }
        }
    }

    g.setColour(colour);
    g.strokePath(path, juce::PathStrokeType(thickness,
        juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}


// ===========================================================================
//  Curve midpoint — for placing curve dots
// ===========================================================================

juce::Point<float> EnvelopeDisplay::curveMidpoint(
    float x0, float y0, float x1, float y1, float curve) const
{
    float mx = (x0 + x1) * 0.5f;
    float alpha = curveToAlpha(curve);

    if (std::abs(alpha) < 0.01f)
        return { mx, (y0 + y1) * 0.5f };

    constexpr int N = 12;
    constexpr int halfN = N / 2;
    float r  = std::exp(alpha / (float)N);
    float rN = std::pow(r, (float)N);
    float denom = rN - 1.0f;
    if (std::abs(denom) < 1e-6f)
        return { mx, (y0 + y1) * 0.5f };

    float inc = (r - 1.0f) / denom;
    float cum = 0.0f;
    for (int i = 0; i < halfN; i++) { cum += inc; inc *= r; }

    return { mx, y0 + cum * (y1 - y0) };
}


// ===========================================================================
//  Point positions
// ===========================================================================

juce::Point<float> EnvelopeDisplay::getPointPosition(int ptId,
    const StageLayout& L) const
{
    float crvAtk = ccToCurve(getParamValue(PT_ATK_CURVE));
    float crvDec = ccToCurve(getParamValue(PT_DEC_CURVE));
    float crvRel = ccToCurve(getParamValue(PT_REL_CURVE));

    switch (ptId) {
        case PT_ATK_TIME:   return { L.xAtk, L.yPeak };
        case PT_ATK_CURVE:  return curveMidpoint(L.x0, L.yBase, L.xAtk, L.yPeak, crvAtk);
        case PT_DEPTH:      return { L.xAtk, L.yPeak };  // shares peak pos
        case PT_DEC_TIME:   return { L.xDec, L.ySustain };
        case PT_DEC_CURVE:  return curveMidpoint(L.xAtk, L.yPeak, L.xDec, L.ySustain, crvDec);
        case PT_SUS_LEVEL:  return { (L.xDec + L.xSus) * 0.5f, L.ySustain };
        case PT_REL_TIME:   return { L.xEnd, L.yBase };
        case PT_REL_CURVE:  return curveMidpoint(L.xSus, L.ySustain, L.xEnd, L.yBase, crvRel);
        default:            return { 0.0f, 0.0f };
    }
}


// ===========================================================================
//  Paint
// ===========================================================================

void EnvelopeDisplay::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // background
    g.setColour(juce::Colour(kColPanel));
    g.fillRoundedRectangle(bounds, 4.0f);
    g.setColour(juce::Colour(kColBorder));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 4.0f, 1.0f);

    auto L = computeLayout();

    // read curve values
    float crvAtk = ccToCurve(getParamValue(PT_ATK_CURVE));
    float crvDec = ccToCurve(getParamValue(PT_DEC_CURVE));
    float crvRel = ccToCurve(getParamValue(PT_REL_CURVE));

    juce::Colour lineCol(kColLine);

    // draw segments
    drawCurvedSegment(g, L.x0, L.yBase, L.xAtk, L.yPeak, crvAtk, lineCol, 2.0f);
    drawCurvedSegment(g, L.xAtk, L.yPeak, L.xDec, L.ySustain, crvDec, lineCol, 2.0f);

    // sustain flat line
    g.setColour(lineCol);
    g.drawLine(L.xDec, L.ySustain, L.xSus, L.ySustain, 2.0f);

    drawCurvedSegment(g, L.xSus, L.ySustain, L.xEnd, L.yBase, crvRel, lineCol, 2.0f);

    // draw dots and labels
    g.setFont(juce::FontOptions(9.0f));

    for (int i = 0; i < PT_COUNT; i++) {
        // skip depth for amp, skip null points
        if (_points[i].paramId == nullptr) continue;
        // for amp, skip PT_DEPTH
        if (_flavour == JuceEnvFlavour::AMP && i == PT_DEPTH) continue;

        auto pos = getPointPosition(i, L);
        bool isSel = (i == _selectedPt);
        float r = isSel ? kDotRadiusSel : kDotRadius;

        // dot
        g.setColour(juce::Colour(isSel ? kColDotSel : kColDot));
        g.fillEllipse(pos.x - r, pos.y - r, r * 2.0f, r * 2.0f);

        // value label — above the dot
        float ccVal = getParamValue(i);
        juce::String valStr = formatValue(i, ccVal);
        g.setColour(juce::Colour(isSel ? kColValue : kColLabel));
        g.drawText(valStr,
                   (int)(pos.x - 24), (int)(pos.y - r - 13), 48, 12,
                   juce::Justification::centred, false);

        // param label — below the dot
        g.setColour(juce::Colour(kColLabel).withAlpha(0.6f));
        g.drawText(_points[i].label,
                   (int)(pos.x - 20), (int)(pos.y + r + 1), 40, 11,
                   juce::Justification::centred, false);
    }
}


// ===========================================================================
//  Mouse interaction — click to select, drag to adjust
// ===========================================================================

void EnvelopeDisplay::mouseDown(const juce::MouseEvent& e)
{
    auto L = computeLayout();
    float bestDist = kHitRadius;
    int bestPt = -1;

    for (int i = 0; i < PT_COUNT; i++) {
        if (_points[i].paramId == nullptr) continue;
        if (_flavour == JuceEnvFlavour::AMP && i == PT_DEPTH) continue;

        auto pos = getPointPosition(i, L);
        float dx = e.position.x - pos.x;
        float dy = e.position.y - pos.y;
        float dist = std::sqrt(dx * dx + dy * dy);
        if (dist < bestDist) {
            bestDist = dist;
            bestPt = i;
        }
    }

    _selectedPt = bestPt;
    _dragPt = bestPt;
    repaint();
}

void EnvelopeDisplay::mouseDrag(const juce::MouseEvent& e)
{
    if (_dragPt < 0 || _points[_dragPt].paramId == nullptr) return;

    auto* param = _apvts.getParameter(_points[_dragPt].paramId);
    if (!param) return;

    auto b = getLocalBounds().toFloat();

    // map mouse position to 0-127 CC range
    float newVal = 0.0f;

    // time points: map horizontal position to CC
    if (_dragPt == PT_ATK_TIME || _dragPt == PT_DEC_TIME || _dragPt == PT_REL_TIME) {
        float norm = (e.position.x - kPadX) / (b.getWidth() - 2.0f * kPadX);
        newVal = juce::jlimit(0.0f, 127.0f, norm * 127.0f);
    }
    // sustain: map vertical position to CC
    else if (_dragPt == PT_SUS_LEVEL) {
        float norm = 1.0f - (e.position.y - kPadY) / (b.getHeight() - kPadY - kPadBottom);
        newVal = juce::jlimit(0.0f, 127.0f, norm * 127.0f);
    }
    // depth: map vertical to bipolar
    else if (_dragPt == PT_DEPTH) {
        float norm = 1.0f - (e.position.y - kPadY) / (b.getHeight() - kPadY - kPadBottom);
        newVal = juce::jlimit(0.0f, 127.0f, norm * 127.0f);
    }
    // curves: map vertical to CC (away from straight line = more curve)
    else if (_dragPt == PT_ATK_CURVE || _dragPt == PT_DEC_CURVE || _dragPt == PT_REL_CURVE) {
        float norm = 1.0f - (e.position.y - kPadY) / (b.getHeight() - kPadY - kPadBottom);
        newVal = juce::jlimit(0.0f, 127.0f, norm * 127.0f);
    }

    // set via APVTS normalised range (0..1 for int params)
    param->setValueNotifyingHost(param->convertTo0to1(newVal));
    repaint();
}


// ===========================================================================
//  Timer — poll for parameter changes and repaint
// ===========================================================================

void EnvelopeDisplay::timerCallback()
{
    bool changed = false;
    for (int i = 0; i < PT_COUNT; i++) {
        if (_points[i].paramId == nullptr) continue;
        float v = getParamValue(i);
        if (std::abs(v - _cachedVals[i]) > 0.01f) {
            _cachedVals[i] = v;
            changed = true;
        }
    }
    if (changed) repaint();
}
