/*
 * JtRmlFont.h — RmlUi FontEngineInterface backed by juce::Font
 *
 * WHY THIS EXISTS
 *   The skin bakes every static legend into its background, so most of the
 *   panel needs no glyphs at all. Two things do: a combo's selected value, and
 *   the LCD's patch name and live readout. Those change at runtime and cannot
 *   be baked, so RmlUi needs some way to draw text - and building it with
 *   RMLUI_FONT_ENGINE=none, as originally planned, would have left both blank.
 *
 *   Using JUCE's own font rendering keeps the dependency list at RmlUi alone.
 *
 * WHOLE-STRING TEXTURES, NOT A GLYPH ATLAS
 *   A conventional font engine rasterises glyphs into an atlas and emits one
 *   quad per character. That is the right design when text is arbitrary and
 *   plentiful. Here it is neither: the panel draws perhaps forty short strings,
 *   almost all of them drawn from fixed option sets.
 *
 *   So a string is rendered ONCE into its own texture and emitted as a single
 *   quad. That has three consequences worth stating:
 *     - it lands on JtRmlRenderer's axis-aligned quad fast path, so text costs
 *       exactly one drawImage;
 *     - kerning and shaping are JUCE's problem, already solved, rather than
 *       ours;
 *     - the cache is keyed by face and string, and is bounded - see kMaxCache.
 *
 *   Textures are rendered IN THEIR FINAL COLOUR (colour is part of the cache
 *   key) and emitted with a white vertex colour. JtRmlRenderer only applies
 *   the alpha of a vertex colour, so tinting a white texture lost the RGB and
 *   every string drew white regardless of the RCSS.
 *
 * PHYSICAL-PIXEL RASTERISATION
 *   RmlUi lays text out in LOGICAL pixels (dp x window ratio). On a scaled
 *   desktop (e.g. Windows at 250%) one logical pixel is several physical ones,
 *   so a string rasterised at logical size was stretched 2.5x when drawn -
 *   blurry, and small bold glyphs closed up (S read as 8). The owner now
 *   reports the physical scale via setRasterScale(); strings are rasterised at
 *   logical size x scale and drawn into a quad of the original logical size,
 *   so each texel lands on one physical pixel.
 *
 * FACE HANDLES
 *   RmlUi asks for a face by family, style, weight and size, and expects an
 *   opaque handle back. Handles here are indices into a small table; a font is
 *   never "loaded" in the FreeType sense, since juce::Font resolves families
 *   itself.
 */

#pragma once

#include <RmlUi/Core/FontEngineInterface.h>

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

namespace jt
{

class JtRmlFont final : public Rml::FontEngineInterface
{
public:
    JtRmlFont();
    ~JtRmlFont() override;

    /* Beyond this many cached string textures the least recently used are
     * dropped. Forty is generous for this panel; the bound exists so a patch
     * browser scrolling through hundreds of names cannot grow without limit. */
    static constexpr std::size_t kMaxCache = 256;

    // --- Rml::FontEngineInterface -------------------------------------------
    bool LoadFontFace(const Rml::String& fileName, int faceIndex,
                      bool fallbackFace, Rml::Style::FontWeight weight) override;

    bool LoadFontFace(Rml::Span<const Rml::byte> data, int faceIndex,
                      const Rml::String& family, Rml::Style::FontStyle style,
                      Rml::Style::FontWeight weight, bool fallbackFace) override;

    Rml::FontFaceHandle GetFontFaceHandle(const Rml::String& family,
                                          Rml::Style::FontStyle style,
                                          Rml::Style::FontWeight weight,
                                          int size) override;

    const Rml::FontMetrics& GetFontMetrics(Rml::FontFaceHandle handle) override;

    int GetStringWidth(Rml::FontFaceHandle handle, Rml::StringView string,
                       const Rml::TextShapingContext& shaping,
                       Rml::Character priorCharacter) override;

    int GenerateString(Rml::RenderManager& renderManager,
                       Rml::FontFaceHandle faceHandle,
                       Rml::FontEffectsHandle fontEffectsHandle,
                       Rml::StringView string, Rml::Vector2f position,
                       Rml::ColourbPremultiplied colour, float opacity,
                       const Rml::TextShapingContext& shaping,
                       Rml::TexturedMeshList& meshList) override;

    int GetVersion(Rml::FontFaceHandle handle) override;
    void ReleaseFontResources() override;

    // --- Host interface -------------------------------------------------------
    /* Physical pixels per logical pixel for the surface being painted. Call
     * before each Render(). A change drops the cached textures and bumps the
     * face version, which makes RmlUi regenerate its text geometry. Cheap when
     * unchanged (one compare), so it is safe to call every paint. */
    void setRasterScale(float physicalPerLogical);

private:
    struct Face
    {
        std::string family;
        int size = 12;
        bool bold = false;
        bool italic = false;
        Rml::FontMetrics metrics{};
    };

    struct Entry;                                   // one cached string texture

    Face* face(Rml::FontFaceHandle handle);
    /* Cache key: face, final straight-alpha colour (ARGB), string. */
    using Key = std::tuple<std::uintptr_t, std::uint32_t, std::string>;

    Entry* lookup(Rml::RenderManager& rm, Rml::FontFaceHandle handle,
                  std::uint32_t argb, const std::string& text);
    void trimCache();

    std::vector<std::unique_ptr<Face>> m_faces;
    std::map<Key, std::unique_ptr<Entry>> m_cache;
    std::uint64_t m_clock = 0;                      // for least-recently-used

    float m_rasterScale = 1.0f;                     // physical px per logical px
    int m_version = 0;                              // bumped on scale change

    Rml::FontMetrics m_fallbackMetrics{};
};

} // namespace jt
