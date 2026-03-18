/*
 * SliderComponent.cpp — JT-8000 Custom Vertical Fader
 *
 * Draws a vertical fader with:
 *   - Scale tick marks on the left side of the track
 *   - Dark track background
 *   - Orange filled region from bottom to current value
 *   - Horizontal thumb line at current position
 *   - Grey label text below
 */

#include "SliderComponent.h"

// -----------------------------------------------------------------------
// Colour palette — matches the JT-8000 dark theme
// -----------------------------------------------------------------------
static const juce::Colour kTrackBg    (0xff0a1018);   // Track background (same as knob arc track)
static const juce::Colour kTrackFill  (0xfff08020);   // Orange fill (same as knob arc fill)
static const juce::Colour kThumb      (0xffc0c8e0);   // Thumb line (same as knob dot)
static const juce::Colour kScaleLine  (0xff1e2a3e);   // Scale tick marks (subtle)
static const juce::Colour kLabel      (0xff8090b0);   // Label text below slider
static const juce::Colour kTrackBorder(0xff2e3a50);   // Track outline

// -----------------------------------------------------------------------
// Layout constants
// -----------------------------------------------------------------------
static constexpr float kTrackWidth   = 6.0f;    // Width of the fader track
static constexpr float kThumbHeight  = 3.0f;    // Height of the thumb line
static constexpr float kThumbOverhang = 6.0f;   // How far thumb extends beyond track
static constexpr float kScaleTickLen = 5.0f;    // Length of scale tick marks
static constexpr int   kLabelHeight  = 14;       // Space reserved for label text

// -----------------------------------------------------------------------
// Constructor
// -----------------------------------------------------------------------
JT8000Slider::JT8000Slider(const juce::String& label)
    : _labelText(label)
{
    // Hidden slider handles mouse interaction and parameter binding
    _slider.setSliderStyle(juce::Slider::LinearVertical);
    _slider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    _slider.setRange(0, 127, 1);
    _slider.setDoubleClickReturnValue(true, 0.0);
    _slider.addListener(this);
    addAndMakeVisible(_slider);
    _slider.setAlpha(0.0f);   // Invisible — we draw custom graphics
}

// -----------------------------------------------------------------------
// APVTS binding
// -----------------------------------------------------------------------
void JT8000Slider::setAttachment(juce::AudioProcessorValueTreeState& apvts,
                                  const juce::String& paramID)
{
    _attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, paramID, _slider);
}

// -----------------------------------------------------------------------
// Layout — hidden slider fills our entire bounds
// -----------------------------------------------------------------------
void JT8000Slider::resized()
{
    _slider.setBounds(getLocalBounds());
}

// -----------------------------------------------------------------------
// Custom paint — track, fill, scale ticks, thumb, label
// -----------------------------------------------------------------------
void JT8000Slider::paint(juce::Graphics& g)
{
    const float w = static_cast<float>(getWidth());
    const float h = static_cast<float>(getHeight());

    // Track area — centred horizontally, leaving room for label at bottom
    const float trackTop    = 4.0f;
    const float trackBottom = h - static_cast<float>(kLabelHeight) - 4.0f;
    const float trackHeight = trackBottom - trackTop;
    const float trackX      = (w - kTrackWidth) * 0.5f;

    // Only draw if we have enough vertical space
    if (trackHeight < 10.0f) return;

    // Normalise slider value to 0..1
    const float norm = static_cast<float>(_slider.getValue() / 127.0);

    // --- Scale tick marks (left side of track) ---
    {
        g.setColour(kScaleLine);
        const float tickX = trackX - kScaleTickLen - 2.0f;

        for (int i = 0; i <= kScaleDivisions; ++i)
        {
            const float frac = static_cast<float>(i) / static_cast<float>(kScaleDivisions);
            const float tickY = trackBottom - frac * trackHeight;
            g.drawHorizontalLine(static_cast<int>(tickY), tickX, trackX - 1.0f);
        }
    }

    // --- Track background (dark rounded rect) ---
    {
        g.setColour(kTrackBg);
        g.fillRoundedRectangle(trackX, trackTop, kTrackWidth, trackHeight, 2.0f);
        g.setColour(kTrackBorder);
        g.drawRoundedRectangle(trackX, trackTop, kTrackWidth, trackHeight, 2.0f, 0.5f);
    }

    // --- Track fill (orange, from bottom up to value position) ---
    if (norm > 0.005f)
    {
        const float fillHeight = norm * trackHeight;
        const float fillTop    = trackBottom - fillHeight;
        g.setColour(kTrackFill);
        g.fillRoundedRectangle(trackX, fillTop, kTrackWidth, fillHeight, 2.0f);
    }

    // --- Thumb line (horizontal bar at current value) ---
    {
        const float thumbY = trackBottom - norm * trackHeight;
        const float thumbX = trackX - kThumbOverhang;
        const float thumbW = kTrackWidth + kThumbOverhang * 2.0f;
        g.setColour(kThumb);
        g.fillRoundedRectangle(thumbX, thumbY - kThumbHeight * 0.5f,
                               thumbW, kThumbHeight, 1.5f);
    }

    // --- Label (below track, grey text) ---
    {
        g.setColour(kLabel);
        g.setFont(juce::FontOptions(9.0f));
        g.drawText(_labelText, 0, static_cast<int>(trackBottom + 2),
                   static_cast<int>(w), kLabelHeight, juce::Justification::centred);
    }
}

// -----------------------------------------------------------------------
// Mouse event forwarding to the hidden slider
// -----------------------------------------------------------------------
void JT8000Slider::mouseDown(const juce::MouseEvent& e)
{
    _slider.mouseDown(e.getEventRelativeTo(&_slider));
}

void JT8000Slider::mouseDrag(const juce::MouseEvent& e)
{
    _slider.mouseDrag(e.getEventRelativeTo(&_slider));
}

void JT8000Slider::mouseDoubleClick(const juce::MouseEvent& e)
{
    _slider.mouseDoubleClick(e.getEventRelativeTo(&_slider));
}

void JT8000Slider::mouseWheelMove(const juce::MouseEvent& e,
                                    const juce::MouseWheelDetails& w)
{
    _slider.mouseWheelMove(e.getEventRelativeTo(&_slider), w);
}
