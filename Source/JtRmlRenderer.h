/*
 * JtRmlRenderer.h — RmlUi RenderInterface backed by juce::Graphics
 *
 * WHY A SOFTWARE RENDERER
 *   The alternative is an OpenGL context attached to a VST3 editor, which on
 *   Windows carries host-specific pitfalls we would rather not own. Everything
 *   this skin draws is static artwork; there is no reason to take a GPU
 *   dependency for it.
 *
 * WHAT RMLUI ACTUALLY HANDS US
 *   Triangles. Every element is compiled to a Rml::Vertex list (position,
 *   premultiplied RGBA, texture coordinate) plus an index list, and the
 *   renderer is asked to draw them. juce::Graphics has no textured-triangle
 *   primitive, so a naive implementation would need a rasteriser.
 *
 *   It does not need one HERE, because of a decision made earlier: every
 *   legend, rule and section title is baked into the background PNG, so the
 *   document contains only image decorators and solid fills. Those compile to
 *   AXIS-ALIGNED QUADS — two triangles sharing an edge, four distinct corners,
 *   axis-aligned UVs. That case is a drawImage or a fillRect.
 *
 *   So the renderer classifies each pair of triangles:
 *       axis-aligned quad, textured    -> drawImage with a source clip
 *       axis-aligned quad, untextured  -> fillRect
 *       anything else                  -> juce::Path fill, flat-shaded
 *   The Path fallback exists so an unexpected decorator degrades rather than
 *   disappears; if it is ever hit often, that is a signal the skin has started
 *   using features outside the agreed budget.
 *
 * PREMULTIPLIED ALPHA
 *   Rml::ColourbPremultiplied is premultiplied; juce::Colour is not. The
 *   conversion has to un-premultiply or colours darken as they fade. See
 *   toJuce().
 *
 * COORDINATE SPACE
 *   RmlUi works in pixels. The context is given a density-independent pixel
 *   ratio so that dp in the skin map to pixels here; this class does no
 *   scaling of its own.
 *
 * THREADING
 *   All calls arrive on the message thread from the host component's paint().
 *   Nothing here is thread-safe and nothing needs to be.
 */

#pragma once

#include <RmlUi/Core/RenderInterface.h>
#include <RmlUi/Core/Types.h>

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace juce
{
    class Graphics;
    class Image;
}

namespace jt
{

class JtRmlRenderer final : public Rml::RenderInterface
{
public:
    JtRmlRenderer();
    ~JtRmlRenderer() override;

    /* Called by the host component around Context::Render(). The Graphics
     * reference is only valid between these two calls. */
    void beginFrame(juce::Graphics& g);
    void endFrame();

    /* Counters for the frame just drawn. `pathFallbacks` should stay at zero:
     * anything else means the skin is producing geometry the fast paths do not
     * recognise, which is worth knowing about before it becomes a performance
     * problem. */
    struct Stats
    {
        int quads = 0;
        int fills = 0;
        int pathFallbacks = 0;
    };
    const Stats& stats() const { return m_stats; }

    // --- Rml::RenderInterface ------------------------------------------------
    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices,
                                                Rml::Span<const int> indices) override;
    void RenderGeometry(Rml::CompiledGeometryHandle geometry,
                        Rml::Vector2f translation,
                        Rml::TextureHandle texture) override;
    void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override;

    Rml::TextureHandle LoadTexture(Rml::Vector2i& textureDimensions,
                                   const Rml::String& source) override;
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source,
                                       Rml::Vector2i sourceDimensions) override;
    void ReleaseTexture(Rml::TextureHandle texture) override;

    void EnableScissorRegion(bool enable) override;
    void SetScissorRegion(Rml::Rectanglei region) override;

    /* Where LoadTexture resolves relative paths from. Set once at startup. */
    void setResourceRoot(const Rml::String& path) { m_root = path; }

private:
    struct Geometry
    {
        std::vector<Rml::Vertex> vertices;
        std::vector<int> indices;
    };

    /* A pair of triangles that form an axis-aligned rectangle, if they do. */
    struct Quad
    {
        float x0, y0, x1, y1;       // destination rect
        float u0, v0, u1, v1;       // texture rect, 0..1
        Rml::ColourbPremultiplied colour;
        bool flipU = false;
        bool flipV = false;
    };

    static bool asQuad(const Rml::Vertex* v, const int* idx, Quad& out);

    void drawQuad(const Quad& q, Rml::Vector2f translation, juce::Image* tex);
    void drawTriangles(const Geometry& geo, Rml::Vector2f translation);

    juce::Graphics* m_g = nullptr;
    Rml::String m_root;

    /* Handles are opaque integers to RmlUi; we hand out indices into these
     * maps rather than raw pointers, so a stale handle is detectable instead of
     * being a wild pointer. */
    std::unordered_map<std::uintptr_t, Geometry> m_geometry;
    std::unordered_map<std::uintptr_t, juce::Image*> m_textures;
    std::uintptr_t m_nextGeometry = 1;
    std::uintptr_t m_nextTexture = 1;

    bool m_scissorEnabled = false;
    Rml::Rectanglei m_scissor;

    Stats m_stats;
};

} // namespace jt
