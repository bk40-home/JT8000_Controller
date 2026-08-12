/*
 * JtRmlFile.h — RmlUi FileInterface serving embedded assets
 *
 * WHERE THE SKIN LIVES
 *   A plugin cannot rely on files sitting beside it: a VST3 is copied, moved
 *   and sandboxed, and the host may run it from anywhere. So the skin -
 *   jt8000.rml, jt8000.rcss and every PNG - is compiled into the binary as
 *   BinaryData and served from memory.
 *
 *   A disk root can be set on top of that. When it is, a file present on disk
 *   WINS over the embedded copy. That is what makes iteration bearable: edit
 *   the rcss, reload the document, see the change, with no rebuild. It is a
 *   development affordance and the root is simply left unset in release.
 *
 * HANDLES
 *   Rml::FileHandle is an opaque integer. We hand out indices into a table
 *   rather than pointers, so a stale handle is a lookup miss rather than a
 *   wild pointer dereference.
 *
 * Read/Seek/Tell must behave like stdio, including seeking past the end and
 * clamping reads - RmlUi's parsers rely on short reads at EOF rather than
 * checking length first.
 */

#pragma once

#include <RmlUi/Core/FileInterface.h>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace jt
{

class JtRmlFile final : public Rml::FileInterface
{
public:
    JtRmlFile() = default;
    ~JtRmlFile() override = default;

    /* Register an embedded asset. `data` must outlive this object, which is
     * true of BinaryData. */
    void addEmbedded(const std::string& name, const void* data, std::size_t size);

    /* Optional development override; a file found here wins over the embedded
     * copy. Leave unset in release. */
    void setDiskRoot(const std::string& path) { m_diskRoot = path; }
    const std::string& diskRoot() const { return m_diskRoot; }

    /* True when the named asset resolved from disk rather than from the binary
     * - worth surfacing, so a stale file on disk cannot silently shadow the
     * shipped skin without anyone noticing. */
    bool lastOpenCameFromDisk() const { return m_lastFromDisk; }

    /* How many embedded assets are registered. Zero here explains a blank
     * panel completely: every decorator resolves to nothing. */
    std::size_t embeddedCount() const { return m_embedded.size(); }

    // --- Rml::FileInterface --------------------------------------------------
    Rml::FileHandle Open(const Rml::String& path) override;
    void Close(Rml::FileHandle file) override;
    std::size_t Read(void* buffer, std::size_t size, Rml::FileHandle file) override;
    bool Seek(Rml::FileHandle file, long offset, int origin) override;
    std::size_t Tell(Rml::FileHandle file) override;
    std::size_t Length(Rml::FileHandle file) override;

private:
    struct Stream
    {
        const std::uint8_t* data = nullptr;   // embedded, not owned
        std::vector<std::uint8_t> owned;      // disk copy, owned
        std::size_t size = 0;
        std::size_t pos = 0;
    };

    /* Strip any directory part: the skin refers to assets by bare name, and a
     * document loaded from disk would otherwise arrive with a full path that
     * does not match the embedded key. */
    static std::string leafName(const std::string& path);

    std::unordered_map<std::string, std::pair<const void*, std::size_t>> m_embedded;
    std::unordered_map<std::uintptr_t, std::unique_ptr<Stream>> m_open;
    std::uintptr_t m_nextHandle = 1;
    std::string m_diskRoot;
    bool m_lastFromDisk = false;
};

} // namespace jt
