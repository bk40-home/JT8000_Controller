/*
 * JtRmlFont.cpp — implementation
 * See JtRmlFont.h for why strings are cached whole rather than as glyphs.
 */

#include "JtRmlFont.h"

#include <RmlUi/Core/CallbackTexture.h>
#include <RmlUi/Core/Mesh.h>
#include <RmlUi/Core/RenderManager.h>
#include <RmlUi/Core/StringUtilities.h>   // StringView is only forward-declared

#include <juce_graphics/juce_graphics.h>

#include <algorithm>
#include <cmath>

namespace jt
{

struct JtRmlFont::Entry
{
    Rml::CallbackTexture texture;
    float width = 0.0f;     // quad size in LOGICAL px (texture is width x scale)
    float height = 0.0f;
    float ascent = 0.0f;    // logical px from quad top to baseline
    float advance = 0.0f;   // logical advance width returned to RmlUi
    std::uint64_t used = 0;
};

namespace
{
    /* A little breathing room around the rendered string: JUCE's advance width
     * can be a fraction short of the inked extent for italic or heavily kerned
     * faces, and a clipped final glyph is the sort of thing that survives to
     * release. */
    constexpr int kPad = 2;

    juce::Font makeFont(const std::string& family, const int size,
                        const bool bold, const bool italic)
    {
        int flags = juce::Font::plain;
        if (bold)   flags |= juce::Font::bold;
        if (italic) flags |= juce::Font::italic;
        return juce::Font(juce::String(family), static_cast<float>(size),
                          flags);
    }
}

// =============================================================================
JtRmlFont::JtRmlFont()
{
    /* Returned for an unknown handle rather than dereferencing null; RmlUi
     * calls GetFontMetrics without checking. */
    m_fallbackMetrics.size = 12;
    m_fallbackMetrics.ascent = 9.0f;
    m_fallbackMetrics.descent = 3.0f;
    m_fallbackMetrics.line_spacing = 14.0f;
    m_fallbackMetrics.x_height = 6.0f;
    m_fallbackMetrics.underline_position = 2.0f;
    m_fallbackMetrics.underline_thickness = 1.0f;
    m_fallbackMetrics.has_ellipsis = false;
}

JtRmlFont::~JtRmlFont() = default;

// =============================================================================
// Face management
// =============================================================================
bool JtRmlFont::LoadFontFace(const Rml::String&, int, bool,
                             Rml::Style::FontWeight)
{
    /* Nothing to load: juce::Font resolves families from the system, so a
     * font-face rule in the RCSS is simply accepted and the family name is used
     * when a face handle is requested. */
    return true;
}

bool JtRmlFont::LoadFontFace(Rml::Span<const Rml::byte>, int,
                             const Rml::String&, Rml::Style::FontStyle,
                             Rml::Style::FontWeight, bool)
{
    /* Embedded font data is not supported. Returning true keeps document
     * loading going; the family falls back to whatever JUCE resolves. */
    return true;
}

Rml::FontFaceHandle JtRmlFont::GetFontFaceHandle(const Rml::String& family,
                                                 const Rml::Style::FontStyle style,
                                                 const Rml::Style::FontWeight weight,
                                                 const int size)
{
    const bool bold = (weight >= Rml::Style::FontWeight::Bold);
    const bool italic = (style == Rml::Style::FontStyle::Italic);
    const std::string fam(family.c_str());

    for (std::size_t i = 0; i < m_faces.size(); ++i)
    {
        const Face& f = *m_faces[i];
        if (f.family == fam && f.size == size && f.bold == bold
            && f.italic == italic)
            return static_cast<Rml::FontFaceHandle>(i + 1);
    }

    auto f = std::make_unique<Face>();
    f->family = fam;
    f->size = size;
    f->bold = bold;
    f->italic = italic;

    const juce::Font jf = makeFont(fam, size, bold, italic);
    f->metrics.size = size;
    f->metrics.ascent = jf.getAscent();
    f->metrics.descent = jf.getDescent();
    f->metrics.line_spacing = jf.getHeight() + jf.getExtraKerningFactor();
    f->metrics.x_height = jf.getAscent() * 0.52f;   // JUCE exposes no x-height
    f->metrics.underline_position = jf.getDescent() * 0.5f;
    f->metrics.underline_thickness = std::max(1.0f, static_cast<float>(size) / 14.0f);
    f->metrics.has_ellipsis = false;

    m_faces.push_back(std::move(f));
    return static_cast<Rml::FontFaceHandle>(m_faces.size());
}

JtRmlFont::Face* JtRmlFont::face(const Rml::FontFaceHandle handle)
{
    const auto i = static_cast<std::size_t>(handle);
    if (i == 0 || i > m_faces.size())
        return nullptr;
    return m_faces[i - 1].get();
}

const Rml::FontMetrics& JtRmlFont::GetFontMetrics(const Rml::FontFaceHandle handle)
{
    const Face* f = face(handle);
    return f != nullptr ? f->metrics : m_fallbackMetrics;
}

int JtRmlFont::GetVersion(Rml::FontFaceHandle)
{
    /* Faces themselves never change, but their rasterisation does when the
     * physical scale changes; RmlUi compares this to regenerate text. */
    return m_version;
}

void JtRmlFont::setRasterScale(const float physicalPerLogical)
{
    /* Quantise to quarter steps so sub-pixel jitter in the reported scale
     * (window drags between monitors, zoom) cannot thrash the cache, and clamp
     * to a sane range so a bad value cannot allocate huge textures. */
    const float s = juce::jlimit(1.0f, 4.0f,
                                 std::round(physicalPerLogical * 4.0f) / 4.0f);
    if (juce::approximatelyEqual(s, m_rasterScale))
        return;                                     // common case: unchanged

    m_rasterScale = s;
    m_cache.clear();                                // textures are scale-specific
    ++m_version;                                    // RmlUi regenerates geometry
}

void JtRmlFont::ReleaseFontResources()
{
    m_cache.clear();
}

// =============================================================================
// Measurement
// =============================================================================
int JtRmlFont::GetStringWidth(const Rml::FontFaceHandle handle,
                              const Rml::StringView string,
                              const Rml::TextShapingContext& shaping,
                              Rml::Character)
{
    Face* f = face(handle);
    if (f == nullptr)
        return 0;

    const juce::String s(std::string(string.begin(), string.end()));
    const juce::Font jf = makeFont(f->family, f->size, f->bold, f->italic);

    /* letter_spacing is applied by RmlUi's layout expectations, so it has to be
     * included here or text will be measured narrower than it draws. */
    const float extra = shaping.letter_spacing * static_cast<float>(s.length());
    return static_cast<int>(std::ceil(jf.getStringWidthFloat(s) + extra));
}

// =============================================================================
// Generation
// =============================================================================
JtRmlFont::Entry* JtRmlFont::lookup(Rml::RenderManager& rm,
                                    const Rml::FontFaceHandle handle,
                                    const std::uint32_t argb,
                                    const std::string& text)
{
    const Key key(static_cast<std::uintptr_t>(handle), argb, text);
    const auto it = m_cache.find(key);
    if (it != m_cache.end())
    {
        it->second->used = ++m_clock;
        return it->second.get();
    }

    Face* f = face(handle);
    if (f == nullptr)
        return nullptr;

    const juce::Font jf = makeFont(f->family, f->size, f->bold, f->italic);
    const juce::String s(text);

    /* Logical box (what RmlUi lays out) and physical texture (what is drawn).
     * The texture is the logical box x scale, so drawing it into the logical
     * quad maps one texel to one physical pixel - no stretch, no blur. */
    const float scale = m_rasterScale;
    const float textW = jf.getStringWidthFloat(s);
    const float logicalW = std::ceil(textW) + static_cast<float>(kPad * 2);
    const float logicalH = std::ceil(jf.getHeight()) + static_cast<float>(kPad * 2);
    const int w = static_cast<int>(std::ceil(logicalW * scale));
    const int h = static_cast<int>(std::ceil(logicalH * scale));
    if (w <= 0 || h <= 0)
        return nullptr;

    /* Rendered in its final colour (part of the cache key) - the renderer
     * applies only the alpha of a vertex colour, so a white texture tinted by
     * the vertex colour lost its RGB. Glyphs are rasterised by JUCE at the
     * physical size via the transform, so hinting happens at the real size. */
    juce::Image img(juce::Image::ARGB, w, h, true);
    {
        juce::Graphics g(img);
        g.addTransform(juce::AffineTransform::scale(scale));
        g.setFont(jf);
        g.setColour(juce::Colour(argb));
        g.drawSingleLineText(s, kPad, kPad + static_cast<int>(jf.getAscent()));
    }

    std::vector<Rml::byte> rgba(static_cast<std::size_t>(w) * h * 4);
    {
        const juce::Image::BitmapData bmp(img, juce::Image::BitmapData::readOnly);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                const juce::Colour c = bmp.getPixelColour(x, y);
                const std::size_t o = (static_cast<std::size_t>(y) * w + x) * 4;
                rgba[o + 0] = c.getRed();
                rgba[o + 1] = c.getGreen();
                rgba[o + 2] = c.getBlue();
                rgba[o + 3] = c.getAlpha();
            }
    }

    auto entry = std::make_unique<Entry>();
    entry->width = logicalW;
    entry->height = logicalH;
    entry->ascent = jf.getAscent() + static_cast<float>(kPad);
    entry->advance = std::ceil(textW);
    entry->used = ++m_clock;

    /* The callback is invoked by RmlUi when it needs the pixels; the data is
     * captured by value because generation may be deferred. */
    entry->texture = rm.MakeCallbackTexture(
        [rgba, w, h](const Rml::CallbackTextureInterface& ti) -> bool
        {
            return ti.GenerateTexture(Rml::Span<const Rml::byte>(rgba.data(),
                                                                 rgba.size()),
                                      Rml::Vector2i(w, h));
        });

    Entry* raw = entry.get();
    m_cache.emplace(key, std::move(entry));
    trimCache();
    return raw;
}

void JtRmlFont::trimCache()
{
    while (m_cache.size() > kMaxCache)
    {
        auto oldest = m_cache.begin();
        for (auto it = m_cache.begin(); it != m_cache.end(); ++it)
            if (it->second->used < oldest->second->used)
                oldest = it;
        m_cache.erase(oldest);
    }
}

int JtRmlFont::GenerateString(Rml::RenderManager& renderManager,
                              const Rml::FontFaceHandle faceHandle,
                              Rml::FontEffectsHandle,
                              const Rml::StringView string,
                              const Rml::Vector2f position,
                              const Rml::ColourbPremultiplied colour,
                              float /*opacity*/,  // already folded into `colour`; RmlUi
                                                  // passes it separately for font effects
                              const Rml::TextShapingContext&,
                              Rml::TexturedMeshList& meshList)
{
    const std::string text(string.begin(), string.end());
    if (text.empty())
        return 0;

    /* RmlUi hands over a PREMULTIPLIED colour (element opacity already in its
     * alpha); the texture wants the straight colour. */
    const float a = static_cast<float>(colour.alpha) / 255.0f;
    if (a <= 0.0f)
        return 0;                                   // fully transparent: nothing to draw
    const auto un = [a](const Rml::byte v)
    {
        return static_cast<juce::uint8>(juce::jlimit(0.0f, 255.0f,
                                                     static_cast<float>(v) / a));
    };
    const juce::Colour straight(un(colour.red), un(colour.green), un(colour.blue),
                                juce::jlimit(0.0f, 1.0f, a));

    Entry* e = lookup(renderManager, faceHandle, straight.getARGB(), text);
    if (e == nullptr)
        return 0;

    /* Colour and alpha are already in the texels, so the vertex colour is
     * plain opaque white - the renderer's untinted fast path. */
    const Rml::ColourbPremultiplied white(255, 255, 255, 255);

    /* `position` is the baseline of the first character; the texture's origin
     * is its top-left, so it is lifted by the ascent. */
    const float x0 = position.x - static_cast<float>(kPad);
    const float y0 = position.y - e->ascent;
    const float x1 = x0 + e->width;
    const float y1 = y0 + e->height;

    Rml::TexturedMesh tm;
    tm.texture = e->texture;
    tm.mesh.vertices = {
        Rml::Vertex{ Rml::Vector2f(x0, y0), white, Rml::Vector2f(0.0f, 0.0f) },
        Rml::Vertex{ Rml::Vector2f(x1, y0), white, Rml::Vector2f(1.0f, 0.0f) },
        Rml::Vertex{ Rml::Vector2f(x1, y1), white, Rml::Vector2f(1.0f, 1.0f) },
        Rml::Vertex{ Rml::Vector2f(x0, y1), white, Rml::Vector2f(0.0f, 1.0f) },
    };
    /* Two triangles forming an axis-aligned rectangle - exactly the shape
     * JtRmlRenderer takes on its fast path. */
    tm.mesh.indices = { 0, 1, 2, 0, 2, 3 };
    meshList.push_back(std::move(tm));

    return static_cast<int>(e->advance);
}

} // namespace jt
