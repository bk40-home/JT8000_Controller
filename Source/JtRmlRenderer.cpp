/*
 * JtRmlRenderer.cpp — implementation
 * See JtRmlRenderer.h for the design and for why quad classification matters.
 */

#include "JtRmlRenderer.h"

#include <juce_graphics/juce_graphics.h>

#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/FileInterface.h>

#include <cstdio>          // SEEK_SET

#include <algorithm>
#include <cmath>

namespace jt
{

namespace
{
    /* Rml::ColourbPremultiplied is premultiplied; juce::Colour is not.
     * Dividing the channels back out is not optional - skipping it makes every
     * partially transparent element darken as it fades. */
    juce::Colour toJuce(const Rml::ColourbPremultiplied c)
    {
        if (c.alpha == 0)
            return juce::Colour();

        const float a = static_cast<float>(c.alpha) / 255.0f;
        const auto un = [a](std::uint8_t v)
        {
            return static_cast<std::uint8_t>(
                std::min(255.0f, static_cast<float>(v) / a));
        };
        return juce::Colour(un(c.red), un(c.green), un(c.blue), c.alpha);
    }

    constexpr float kEps = 0.01f;

    bool nearly(float a, float b) { return std::fabs(a - b) < kEps; }
}

// =============================================================================
JtRmlRenderer::JtRmlRenderer() = default;

JtRmlRenderer::~JtRmlRenderer()
{
    for (auto& t : m_textures)
        delete t.second;
}

void JtRmlRenderer::beginFrame(juce::Graphics& g)
{
    m_g = &g;
    m_stats = Stats{};
}

void JtRmlRenderer::endFrame()
{
    m_g = nullptr;
}

// =============================================================================
// Geometry
// =============================================================================
Rml::CompiledGeometryHandle JtRmlRenderer::CompileGeometry(
    Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices)
{
    const auto handle = m_nextGeometry++;
    Geometry geo;
    geo.vertices.assign(vertices.begin(), vertices.end());
    geo.indices.assign(indices.begin(), indices.end());
    m_geometry.emplace(handle, std::move(geo));
    return static_cast<Rml::CompiledGeometryHandle>(handle);
}

void JtRmlRenderer::ReleaseGeometry(Rml::CompiledGeometryHandle geometry)
{
    m_geometry.erase(static_cast<std::uintptr_t>(geometry));
}

/*
 * Two triangles form an axis-aligned rectangle when their six indices refer to
 * exactly four distinct positions, those positions take only two distinct x and
 * two distinct y values, and the texture coordinates follow the same pattern.
 * That is the shape every image decorator and solid fill compiles to.
 */
bool JtRmlRenderer::asQuad(const Rml::Vertex* v, const int* idx, Quad& out)
{
    float xs[4], ys[4], us[4], vs[4];
    int n = 0;

    for (int i = 0; i < 6; ++i)
    {
        const Rml::Vertex& vert = v[idx[i]];
        bool seen = false;
        for (int j = 0; j < n; ++j)
            if (nearly(xs[j], vert.position.x) && nearly(ys[j], vert.position.y))
            {
                seen = true;
                break;
            }
        if (seen)
            continue;
        if (n == 4)
            return false;                       // more than four corners
        xs[n] = vert.position.x;
        ys[n] = vert.position.y;
        us[n] = vert.tex_coord.x;
        vs[n] = vert.tex_coord.y;
        ++n;
    }
    if (n != 4)
        return false;

    const float x0 = *std::min_element(xs, xs + 4);
    const float x1 = *std::max_element(xs, xs + 4);
    const float y0 = *std::min_element(ys, ys + 4);
    const float y1 = *std::max_element(ys, ys + 4);

    /* Every corner must sit on one of the two x values and one of the two y
     * values, or the shape is a quadrilateral rather than a rectangle. */
    for (int i = 0; i < 4; ++i)
    {
        if (!nearly(xs[i], x0) && !nearly(xs[i], x1)) return false;
        if (!nearly(ys[i], y0) && !nearly(ys[i], y1)) return false;
    }

    out.x0 = x0; out.y0 = y0; out.x1 = x1; out.y1 = y1;
    out.u0 = *std::min_element(us, us + 4);
    out.u1 = *std::max_element(us, us + 4);
    out.v0 = *std::min_element(vs, vs + 4);
    out.v1 = *std::max_element(vs, vs + 4);
    out.colour = v[idx[0]].colour;

    /* Detect a mirrored source rect: the corner at minimum x must also hold the
     * minimum u, otherwise the texture is flipped horizontally. */
    for (int i = 0; i < 4; ++i)
    {
        if (nearly(xs[i], x0)) out.flipU = !nearly(us[i], out.u0);
        if (nearly(ys[i], y0)) out.flipV = !nearly(vs[i], out.v0);
    }
    return true;
}

void JtRmlRenderer::drawQuad(const Quad& q, Rml::Vector2f t, juce::Image* tex)
{
    const juce::Rectangle<float> dest(q.x0 + t.x, q.y0 + t.y,
                                      q.x1 - q.x0, q.y1 - q.y0);
    if (dest.getWidth() < 0.25f || dest.getHeight() < 0.25f)
        return;

    if (tex == nullptr)
    {
        m_g->setColour(toJuce(q.colour));
        m_g->fillRect(dest);
        ++m_stats.fills;
        return;
    }

    /* Texture coordinates are normalised; the source rect is in pixels. */
    const int tw = tex->getWidth();
    const int th = tex->getHeight();
    const int sx = static_cast<int>(std::lround(q.u0 * tw));
    const int sy = static_cast<int>(std::lround(q.v0 * th));
    const int sw = std::max(1, static_cast<int>(std::lround((q.u1 - q.u0) * tw)));
    const int sh = std::max(1, static_cast<int>(std::lround((q.v1 - q.v0) * th)));

    /* Tint: RmlUi modulates the texture by the vertex colour. White means no
     * tint, which is the common case, so the multiply is skipped for it. */
    const juce::Colour tint = toJuce(q.colour);
    const bool plain = (tint.getRed() == 255 && tint.getGreen() == 255
                        && tint.getBlue() == 255);

    juce::Graphics::ScopedSaveState save(*m_g);
    if (!plain)
        m_g->setOpacity(tint.getFloatAlpha());

    m_g->drawImage(*tex,
                   dest.getX(), dest.getY(), dest.getWidth(), dest.getHeight(),
                   sx, sy, sw, sh, false);
    ++m_stats.quads;
}

void JtRmlRenderer::drawTriangles(const Geometry& geo, Rml::Vector2f t)
{
    /* Flat-shaded fallback. Reached only by geometry the quad classifier does
     * not recognise; see the header for why that should not happen with a baked
     * background. */
    for (std::size_t i = 0; i + 2 < geo.indices.size(); i += 3)
    {
        const Rml::Vertex& a = geo.vertices[geo.indices[i]];
        const Rml::Vertex& b = geo.vertices[geo.indices[i + 1]];
        const Rml::Vertex& c = geo.vertices[geo.indices[i + 2]];

        juce::Path p;
        p.startNewSubPath(a.position.x + t.x, a.position.y + t.y);
        p.lineTo(b.position.x + t.x, b.position.y + t.y);
        p.lineTo(c.position.x + t.x, c.position.y + t.y);
        p.closeSubPath();

        m_g->setColour(toJuce(a.colour));
        m_g->fillPath(p);
        ++m_stats.pathFallbacks;
    }
}

void JtRmlRenderer::RenderGeometry(Rml::CompiledGeometryHandle geometry,
                                   Rml::Vector2f translation,
                                   Rml::TextureHandle texture)
{
    if (m_g == nullptr)
        return;

    const auto it = m_geometry.find(static_cast<std::uintptr_t>(geometry));
    if (it == m_geometry.end())
        return;
    const Geometry& geo = it->second;

    juce::Image* tex = nullptr;
    if (texture != 0)
    {
        const auto t = m_textures.find(static_cast<std::uintptr_t>(texture));
        if (t != m_textures.end())
            tex = t->second;
    }

    juce::Graphics::ScopedSaveState save(*m_g);
    if (m_scissorEnabled)
        m_g->reduceClipRegion(juce::Rectangle<int>(
            m_scissor.Left(), m_scissor.Top(),
            m_scissor.Width(), m_scissor.Height()));

    /* Walk the index list in pairs of triangles, taking the quad path whenever
     * the pair forms a rectangle. */
    std::size_t i = 0;
    for (; i + 5 < geo.indices.size(); i += 6)
    {
        Quad q;
        if (asQuad(geo.vertices.data(), geo.indices.data() + i, q))
        {
            drawQuad(q, translation, tex);
        }
        else
        {
            Geometry sub;
            sub.vertices = geo.vertices;
            sub.indices.assign(geo.indices.begin() + static_cast<long>(i),
                               geo.indices.begin() + static_cast<long>(i + 6));
            drawTriangles(sub, translation);
        }
    }

    if (i < geo.indices.size())     // a trailing lone triangle
    {
        Geometry sub;
        sub.vertices = geo.vertices;
        sub.indices.assign(geo.indices.begin() + static_cast<long>(i),
                           geo.indices.end());
        drawTriangles(sub, translation);
    }
}

// =============================================================================
// Textures
// =============================================================================
Rml::TextureHandle JtRmlRenderer::LoadTexture(Rml::Vector2i& dims,
                                              const Rml::String& source)
{
    /* Textures MUST come through RmlUi's FileInterface, not straight off disk.
     * The skin is embedded as BinaryData - there is no file to open - and
     * reading via juce::File silently failed for every asset, which showed up
     * as "Could not load texture: bg_main.png" and a blank panel.
     *
     * JtRmlFile serves the embedded copy, or a disk override when one is set,
     * so going through the interface gets both for free. */
    Rml::FileInterface* files = Rml::GetFileInterface();
    if (files == nullptr)
        return 0;

    const Rml::FileHandle handle = files->Open(source);
    if (handle == 0)
        return 0;

    const std::size_t size = files->Length(handle);
    std::vector<char> bytes(size);
    if (size > 0)
    {
        files->Seek(handle, 0, SEEK_SET);
        files->Read(bytes.data(), size, handle);
    }
    files->Close(handle);

    if (bytes.empty())
        return 0;

    juce::Image img = juce::ImageFileFormat::loadFrom(bytes.data(), size);
    if (!img.isValid())
        return 0;

    dims.x = img.getWidth();
    dims.y = img.getHeight();

    const auto h = m_nextTexture++;
    m_textures.emplace(h, new juce::Image(std::move(img)));
    return static_cast<Rml::TextureHandle>(h);
}

Rml::TextureHandle JtRmlRenderer::GenerateTexture(Rml::Span<const Rml::byte> source,
                                                  Rml::Vector2i dims)
{
    if (dims.x <= 0 || dims.y <= 0)
        return 0;

    auto* img = new juce::Image(juce::Image::ARGB, dims.x, dims.y, true);
    juce::Image::BitmapData bmp(*img, juce::Image::BitmapData::writeOnly);

    /* RmlUi supplies tightly packed RGBA8, NOT premultiplied. */
    const Rml::byte* src = source.data();
    for (int y = 0; y < dims.y; ++y)
    {
        for (int x = 0; x < dims.x; ++x)
        {
            const Rml::byte* p = src + (static_cast<std::size_t>(y) * dims.x + x) * 4;
            bmp.setPixelColour(x, y, juce::Colour(p[0], p[1], p[2], p[3]));
        }
    }

    const auto handle = m_nextTexture++;
    m_textures.emplace(handle, img);
    return static_cast<Rml::TextureHandle>(handle);
}

void JtRmlRenderer::ReleaseTexture(Rml::TextureHandle texture)
{
    const auto it = m_textures.find(static_cast<std::uintptr_t>(texture));
    if (it == m_textures.end())
        return;
    delete it->second;
    m_textures.erase(it);
}

// =============================================================================
// Scissor
// =============================================================================
void JtRmlRenderer::EnableScissorRegion(bool enable)
{
    m_scissorEnabled = enable;
}

void JtRmlRenderer::SetScissorRegion(Rml::Rectanglei region)
{
    m_scissor = region;
}

} // namespace jt
