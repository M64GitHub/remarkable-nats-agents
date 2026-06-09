#include "rm/RmTaggedReader.h"

#include <cstring>

namespace rm {

uint64_t RmTaggedReader::readVaruint()
{
    uint64_t result = 0;
    int shift = 0;
    while (true) {
        if (!ensure(1))
            return result;
        const uint8_t b = m_data[m_pos++];
        result |= uint64_t(b & 0x7F) << shift;
        if (!(b & 0x80))
            break;
        shift += 7;
        if (shift > 63) {  // overlong / corrupt varuint
            fail();
            break;
        }
    }
    return result;
}

uint8_t RmTaggedReader::readU8()
{
    if (!ensure(1))
        return 0;
    return m_data[m_pos++];
}

uint16_t RmTaggedReader::readU16()
{
    if (!ensure(2))
        return 0;
    const uint16_t v = uint16_t(m_data[m_pos]) | (uint16_t(m_data[m_pos + 1]) << 8);
    m_pos += 2;
    return v;
}

uint32_t RmTaggedReader::readU32()
{
    if (!ensure(4))
        return 0;
    const uint32_t v = uint32_t(m_data[m_pos]) | (uint32_t(m_data[m_pos + 1]) << 8)
                     | (uint32_t(m_data[m_pos + 2]) << 16) | (uint32_t(m_data[m_pos + 3]) << 24);
    m_pos += 4;
    return v;
}

float RmTaggedReader::readF32()
{
    const uint32_t bits = readU32();
    float f;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}

double RmTaggedReader::readF64()
{
    if (!ensure(8))
        return 0.0;
    uint64_t bits = 0;
    for (int i = 0; i < 8; ++i)
        bits |= uint64_t(m_data[m_pos + i]) << (8 * i);
    m_pos += 8;
    double d;
    std::memcpy(&d, &bits, sizeof(d));
    return d;
}

bool RmTaggedReader::readBool()
{
    return readU8() != 0;
}

CrdtId RmTaggedReader::readCrdtId()
{
    CrdtId id;
    id.author = readU8();
    id.value = readVaruint();
    return id;
}

QByteArray RmTaggedReader::readBytes(size_t n)
{
    if (!ensure(n))
        return {};
    QByteArray b(reinterpret_cast<const char *>(m_data + m_pos), int(n));
    m_pos += n;
    return b;
}

uint64_t RmTaggedReader::peekTag()
{
    if (m_pos >= m_size)
        return UINT64_MAX;
    const size_t savePos = m_pos;
    const bool saveErr = m_error;
    const uint64_t tag = readVaruint();
    m_pos = savePos;
    m_error = saveErr;
    return tag;
}

bool RmTaggedReader::checkTag(uint32_t index, TagType type)
{
    return peekTag() == tagFor(index, type);
}

bool RmTaggedReader::readTag(uint32_t index, TagType type)
{
    const uint64_t tag = readVaruint();
    if (tag != tagFor(index, type)) {
        fail();
        return false;
    }
    return true;
}

CrdtId RmTaggedReader::readId(uint32_t index)
{
    readTag(index, TagType::Id);
    return readCrdtId();
}

uint32_t RmTaggedReader::readInt(uint32_t index)
{
    readTag(index, TagType::Byte4);
    return readU32();
}

float RmTaggedReader::readFloat(uint32_t index)
{
    readTag(index, TagType::Byte4);
    return readF32();
}

double RmTaggedReader::readDouble(uint32_t index)
{
    readTag(index, TagType::Byte8);
    return readF64();
}

bool RmTaggedReader::readBoolField(uint32_t index)
{
    readTag(index, TagType::Byte1);
    return readBool();
}

uint32_t RmTaggedReader::readSubblockStart(uint32_t index, size_t &outEnd)
{
    readTag(index, TagType::Length4);
    const uint32_t len = readU32();
    outEnd = m_pos + len;
    return len;
}

QString RmTaggedReader::readString(uint32_t index)
{
    size_t end = 0;
    readSubblockStart(index, end);
    const uint64_t len = readVaruint();
    readU8();   // is-ascii flag (unused)
    const QByteArray bytes = readBytes(size_t(len));
    seek(end);  // skip any trailing per-run format
    return QString::fromUtf8(bytes);
}

}  // namespace rm
