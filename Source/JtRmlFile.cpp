/*
 * JtRmlFile.cpp — implementation
 * See JtRmlFile.h for why assets are embedded and how the disk override works.
 */

#include "JtRmlFile.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace jt
{

void JtRmlFile::addEmbedded(const std::string& name, const void* data,
                            const std::size_t size)
{
    m_embedded[leafName(name)] = { data, size };
}

std::string JtRmlFile::leafName(const std::string& path)
{
    const auto slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

Rml::FileHandle JtRmlFile::Open(const Rml::String& path)
{
    const std::string leaf = leafName(std::string(path.c_str()));
    auto stream = std::make_unique<Stream>();
    m_lastFromDisk = false;

    // --- disk first, when a development root is set --------------------------
    if (!m_diskRoot.empty())
    {
        const juce::File f =
            juce::File(juce::String(m_diskRoot)).getChildFile(juce::String(leaf));
        if (f.existsAsFile())
        {
            juce::MemoryBlock mb;
            if (f.loadFileAsData(mb))
            {
                stream->owned.resize(mb.getSize());
                std::memcpy(stream->owned.data(), mb.getData(), mb.getSize());
                stream->data = stream->owned.data();
                stream->size = stream->owned.size();
                m_lastFromDisk = true;
            }
        }
    }

    // --- otherwise the embedded copy ----------------------------------------
    if (stream->data == nullptr)
    {
        const auto it = m_embedded.find(leaf);
        if (it == m_embedded.end())
            return 0;                       // 0 is RmlUi's "failed to open"
        stream->data = static_cast<const std::uint8_t*>(it->second.first);
        stream->size = it->second.second;
    }

    const auto handle = m_nextHandle++;
    m_open.emplace(handle, std::move(stream));
    return static_cast<Rml::FileHandle>(handle);
}

void JtRmlFile::Close(Rml::FileHandle file)
{
    m_open.erase(static_cast<std::uintptr_t>(file));
}

std::size_t JtRmlFile::Read(void* buffer, const std::size_t size,
                            Rml::FileHandle file)
{
    const auto it = m_open.find(static_cast<std::uintptr_t>(file));
    if (it == m_open.end())
        return 0;
    Stream& s = *it->second;

    /* Clamp rather than fail: RmlUi's parsers ask for a fixed block and rely on
     * a short read to detect the end of the file. */
    const std::size_t avail = s.size > s.pos ? s.size - s.pos : 0;
    const std::size_t n = std::min(size, avail);
    if (n > 0)
        std::memcpy(buffer, s.data + s.pos, n);
    s.pos += n;
    return n;
}

bool JtRmlFile::Seek(Rml::FileHandle file, const long offset, const int origin)
{
    const auto it = m_open.find(static_cast<std::uintptr_t>(file));
    if (it == m_open.end())
        return false;
    Stream& s = *it->second;

    long base = 0;
    switch (origin)
    {
    case SEEK_SET: base = 0; break;
    case SEEK_CUR: base = static_cast<long>(s.pos); break;
    case SEEK_END: base = static_cast<long>(s.size); break;
    default: return false;
    }

    const long target = base + offset;
    if (target < 0)
        return false;

    /* Seeking past the end is legal, as it is with stdio; the next read simply
     * returns nothing. */
    s.pos = static_cast<std::size_t>(target);
    return true;
}

std::size_t JtRmlFile::Tell(Rml::FileHandle file)
{
    const auto it = m_open.find(static_cast<std::uintptr_t>(file));
    return it == m_open.end() ? 0 : it->second->pos;
}

std::size_t JtRmlFile::Length(Rml::FileHandle file)
{
    const auto it = m_open.find(static_cast<std::uintptr_t>(file));
    return it == m_open.end() ? 0 : it->second->size;
}

} // namespace jt
