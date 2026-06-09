#pragma once

// A forward-only cursor over a `.rm` byte buffer. Implements the v6 primitives:
// little-endian fixed ints, LEB128 varuint, IEEE floats, and the tagged-value
// scheme where every field is prefixed by `tag = (field_index << 4) | tag_type`.
//
// The reader never throws. On a short read it latches an error flag (`ok()`) and
// returns zero/defaults, so a malformed or truncated file fails the parse cleanly
// instead of reading out of bounds.

#include <QByteArray>
#include <QString>
#include <cstdint>

namespace rm {

// tag_type nibble values — copied verbatim from rmscene's `TagType` enum
// (tagged_block_common.py). NB: an earlier draft of docs/RM-PARSER-RENDERER.md
// listed these inverted; the byte-validated fixture proved the rmscene values
// below are correct (subblock(6) decodes as 0x6C = (6<<4)|Length4).
enum class TagType : uint8_t {
    Id = 0xF,        // CrdtId: uint8 author + varuint value
    Length4 = 0xC,   // subblock: uint32 byte-length, then nested tagged data
    Byte8 = 0x8,     // 8-byte double
    Byte4 = 0x4,     // 4-byte int / float32
    Byte1 = 0x1,     // 1-byte bool
};

struct CrdtId {
    uint8_t author = 0;
    uint64_t value = 0;
};

class RmTaggedReader {
public:
    explicit RmTaggedReader(const QByteArray &data)
        : m_data(reinterpret_cast<const uint8_t *>(data.constData())),
          m_size(static_cast<size_t>(data.size())) {}
    RmTaggedReader(const uint8_t *data, size_t size) : m_data(data), m_size(size) {}

    size_t pos() const { return m_pos; }
    size_t size() const { return m_size; }
    bool atEnd() const { return m_pos >= m_size; }
    size_t remaining() const { return m_pos <= m_size ? m_size - m_pos : 0; }
    bool ok() const { return !m_error; }
    void seek(size_t p) { m_pos = p; }

    // --- primitives ---
    uint64_t readVaruint();
    uint8_t readU8();
    uint16_t readU16();
    uint32_t readU32();
    float readF32();
    double readF64();
    bool readBool();
    CrdtId readCrdtId();
    QByteArray readBytes(size_t n);

    // --- tag handling ---
    // peekTag returns the next tag varuint WITHOUT consuming; UINT64_MAX at EOF.
    uint64_t peekTag();
    bool checkTag(uint32_t index, TagType type);  // peek: does the next tag match?
    bool readTag(uint32_t index, TagType type);   // consume; latches error on mismatch

    // --- tagged readers (each consumes the 1-byte tag + payload) ---
    CrdtId readId(uint32_t index);
    uint32_t readInt(uint32_t index);     // Byte4 as uint32
    float readFloat(uint32_t index);      // Byte4 as float32
    double readDouble(uint32_t index);    // Byte8
    bool readBoolField(uint32_t index);   // Byte1
    // Subblock: consumes tag + uint32 length; returns the length and sets
    // outEnd to the absolute offset where the subblock ends.
    uint32_t readSubblockStart(uint32_t index, size_t &outEnd);
    // String stored in a subblock: varuint length, 1 flag byte, then UTF-8 bytes.
    // Seeks past the whole subblock (skips any trailing format), returns the text.
    QString readString(uint32_t index);

    static constexpr uint64_t tagFor(uint32_t index, TagType type) {
        return (uint64_t(index) << 4) | uint64_t(type);
    }

private:
    void fail() { m_error = true; }
    bool ensure(size_t n) {
        if (m_pos + n > m_size) { fail(); return false; }
        return true;
    }

    const uint8_t *m_data;
    size_t m_size;
    size_t m_pos = 0;
    bool m_error = false;
};

}  // namespace rm
