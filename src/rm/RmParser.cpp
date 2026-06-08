#include "rm/RmParser.h"

#include "rm/RmTaggedReader.h"

#include <QLatin1String>

#include <limits>

namespace rm {

namespace {

constexpr char kHeaderPrefix[] = "reMarkable .lines file, version=";
constexpr int kHeaderPrefixLen = 32;   // index of the version digit
constexpr int kHeaderLen = 43;         // fixed-width, space-padded

// Decode one stroke point. v2 (Paper Pro) is the 14-byte quantised layout;
// v1 is 24 bytes of float32. We only keep x, y, width, pressure for rendering.
Point readPointV2(RmTaggedReader &r)
{
    Point p;
    p.x = r.readF32();
    p.y = r.readF32();
    /* speed    */ r.readU16();
    const uint16_t widthRaw = r.readU16();   // stored as value * 4
    /* direction*/ r.readU8();
    const uint8_t pressureRaw = r.readU8();  // stored as value * 255
    p.width = float(widthRaw) / 4.0f;
    p.pressure = float(pressureRaw) / 255.0f;
    return p;
}

Point readPointV1(RmTaggedReader &r)
{
    Point p;
    p.x = r.readF32();
    p.y = r.readF32();
    /* speed     */ r.readF32();
    /* direction */ r.readF32();
    p.width = r.readF32();
    p.pressure = r.readF32();
    return p;
}

// Parse a SceneLineItem (0x05) body: the shared SceneItem CRDT envelope, then an
// OPTIONAL value subblock(6) holding the Line. ~3.6% of items in the fixture are
// value-less; unconditionally reading the subblock would derail byte accounting.
// `r` is bounded to exactly this block's body, so a short/odd item can never read
// into the next block (peeks past the end simply report EOF).
void parseSceneLineItem(RmTaggedReader &r, uint8_t curVersion,
                        Layer &layer, ParseStats &st)
{
    st.sceneLineItems++;

    // SceneItem envelope (shared by every SceneItem block).
    r.readId(1);            // parent_id
    r.readId(2);            // item_id
    r.readId(3);            // left_id
    r.readId(4);            // right_id
    r.readInt(5);           // deleted_length

    if (!r.checkTag(6, TagType::Length4)) {
        st.valuelessItems++;
        return;             // value-less item — no Line to read
    }

    size_t subEnd = 0;
    r.readSubblockStart(6, subEnd);
    r.readU8();             // item_type (== Line's ITEM_TYPE)

    Stroke s;
    // Line value. Each tag is optional/guarded; whatever we miss is recovered by
    // the subEnd resync below, keeping the block boundary exact.
    if (r.checkTag(1, TagType::Byte4)) s.tool = int(r.readInt(1));
    if (r.checkTag(2, TagType::Byte4)) s.color = int(r.readInt(2));
    if (r.checkTag(3, TagType::Byte8)) s.thicknessScale = r.readDouble(3);
    if (r.checkTag(4, TagType::Byte4)) r.readFloat(4);   // starting_length (unused)

    if (r.checkTag(5, TagType::Length4)) {
        size_t ptsEnd = 0;
        const uint32_t ptsLen = r.readSubblockStart(5, ptsEnd);
        const int pointSize = (curVersion >= 2) ? 14 : 24;
        if (ptsLen % uint32_t(pointSize) == 0) {
            const int count = int(ptsLen) / pointSize;
            s.points.reserve(size_t(count));
            for (int i = 0; i < count && r.ok(); ++i)
                s.points.push_back(pointSize == 14 ? readPointV2(r) : readPointV1(r));
        }
        r.seek(ptsEnd);     // resync past the point array regardless
    }

    // Optional trailing tags (timestamp / move_id / explicit ARGB).
    if (r.checkTag(6, TagType::Id)) r.readId(6);          // timestamp
    if (r.checkTag(7, TagType::Id)) r.readId(7);          // move_id
    if (r.checkTag(8, TagType::Byte4)) {
        s.rgba = r.readInt(8);
        s.hasRgba = true;
    }

    r.seek(subEnd);         // resync to the value subblock end

    st.strokes++;
    st.points += int(s.points.size());
    st.tools[s.tool]++;
    st.colors[s.color]++;
    layer.strokes.push_back(std::move(s));
}

}  // namespace

bool RmParser::parse(const QByteArray &data, Page &out, ParseStats *statsOut, QString *err)
{
    ParseStats st;
    out = Page{};

    if (data.size() < kHeaderLen) {
        if (err) *err = QStringLiteral("file too small for a .rm header");
        if (statsOut) *statsOut = st;
        return false;
    }
    if (!data.startsWith(QByteArray(kHeaderPrefix, kHeaderPrefixLen))) {
        if (err) *err = QStringLiteral("bad .rm header (not a lines file)");
        if (statsOut) *statsOut = st;
        return false;
    }
    const char versionCh = data.at(kHeaderPrefixLen);
    if (versionCh != '5' && versionCh != '6') {
        if (err) *err = QStringLiteral("unsupported .rm version '%1'").arg(versionCh);
        if (statsOut) *statsOut = st;
        return false;
    }
    st.headerOk = true;
    st.version = versionCh - '0';

    Layer layer;   // M1: one flat layer for all strokes
    layer.name = QStringLiteral("Layer 1");

    RmTaggedReader r(reinterpret_cast<const uint8_t *>(data.constData()),
                     size_t(data.size()));
    r.seek(kHeaderLen);

    // Block loop. Block header = u32 body_length, u8 unknown(=0), u8 min_version,
    // u8 current_version, u8 block_type. body spans body_length bytes after it.
    while (r.remaining() >= 8) {
        const uint32_t bodyLen = r.readU32();
        r.readU8();                          // unknown (== 0)
        r.readU8();                          // min_version
        const uint8_t curVersion = r.readU8();
        const uint8_t blockType = r.readU8();

        const size_t bodyStart = r.pos();
        const size_t bodyEnd = bodyStart + bodyLen;
        if (bodyEnd > r.size() || !r.ok())
            break;                           // truncated/corrupt — stop cleanly

        if (BlockType(blockType) == BlockType::SceneLineItem) {
            // Decode the body through a reader bounded to it; its error state is
            // isolated, so a malformed item never poisons the block loop.
            RmTaggedReader body(reinterpret_cast<const uint8_t *>(data.constData()) + bodyStart,
                                bodyLen);
            parseSceneLineItem(body, curVersion, layer, st);
        }
        // Other blocks (TreeNode, RootText, PageInfo, …) are skipped by length.

        r.seek(bodyEnd);                     // resync to the declared boundary
        st.blocks++;
    }

    st.leftover = r.remaining();

    // Bounding box over every kept point.
    float minX = std::numeric_limits<float>::max();
    float minY = std::numeric_limits<float>::max();
    float maxX = std::numeric_limits<float>::lowest();
    float maxY = std::numeric_limits<float>::lowest();
    for (const Stroke &s : layer.strokes)
        for (const Point &p : s.points) {
            minX = std::min(minX, p.x);
            minY = std::min(minY, p.y);
            maxX = std::max(maxX, p.x);
            maxY = std::max(maxY, p.y);
        }
    if (!layer.strokes.empty() && st.points > 0) {
        out.hasContent = true;
        out.minX = minX; out.minY = minY; out.maxX = maxX; out.maxY = maxY;
    }

    out.layers.push_back(std::move(layer));

    if (statsOut) *statsOut = st;
    if (err && !r.ok()) *err = QStringLiteral("reader underrun during parse");
    return r.ok();
}

}  // namespace rm
