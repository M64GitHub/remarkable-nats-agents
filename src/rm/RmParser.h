#pragma once

// Parses a reMarkable `.rm` v6 ("lines", version=5/6) file into a `Page`.
//
// Layout: a 43-byte ASCII header, then a sequence of length-prefixed blocks. We
// dispatch on block_type and fully decode SceneLineItem (0x05 — the strokes);
// every other block is skipped by its declared length, so unknown/newer blocks
// never abort the parse (forward-compat). Because each block is resynced to its
// recorded boundary, a clean file parses with zero leftover bytes — the headless
// `AGENT_CHAT_TEST=render` path asserts that against the byte-validated fixture.

#include "rm/RmTypes.h"

#include <QByteArray>
#include <QHash>
#include <QString>

namespace rm {

struct ParseStats {
    bool headerOk = false;
    int version = 0;
    int blocks = 0;
    int sceneLineItems = 0;   // 0x05 blocks seen
    int valuelessItems = 0;   // SceneLineItems with no value subblock(6)
    int strokes = 0;
    int points = 0;
    size_t leftover = 0;      // bytes after the final block (0 == byte-exact)
    QHash<int, int> tools;    // tool_id histogram
    QHash<int, int> colors;   // color_id histogram
};

class RmParser {
public:
    // Returns true if the header was valid and parsing completed without a reader
    // underrun. `stats` and `err` are optional out-params.
    static bool parse(const QByteArray &data, Page &out,
                      ParseStats *stats = nullptr, QString *err = nullptr);
};

}  // namespace rm
