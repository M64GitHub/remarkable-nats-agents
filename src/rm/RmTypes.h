#pragma once

// Plain data types for the in-app reMarkable `.rm` (v6 "lines") renderer.
// Only the integer enum tables and POD structs live here — no Qt rendering types,
// so the parser stays headless-testable. Enum integers are copied verbatim from
// rmscene (`scene_items.py`); see docs/RM-PARSER-RENDERER.md for provenance.

#include <QString>
#include <cstdint>
#include <vector>

namespace rm {

// rmscene `Pen` — tool_id values. Each tool has a v1 and a v2 variant with
// distinct ints; the Paper Pro writes the v2 family (e.g. Fineliner2 = 17).
enum class Pen : int {
    Paintbrush1 = 0,  Pencil1 = 1,  Ballpoint1 = 2,  Marker1 = 3,
    Fineliner1 = 4,   Highlighter1 = 5, Eraser = 6,  MechanicalPencil1 = 7,
    EraserArea = 8,   MechanicalPencil2 = 13, Pencil2 = 14, Ballpoint2 = 15,
    Marker2 = 16,     Fineliner2 = 17, Highlighter2 = 18, Calligraphy = 21,
    Shader = 23,
};

// rmscene `PenColor` — color_id values (the palette index). When a stroke also
// carries an explicit ARGB (Line tag 8, Paper Pro colour), prefer that.
enum class PenColor : int {
    Black = 0,  Gray = 1,  White = 2,  Yellow = 3,  Green = 4,  Pink = 5,
    Blue = 6,   Red = 7,   GrayOverlap = 8, Highlight = 9, Green2 = 10,
    Cyan = 11,  Magenta = 12, Yellow2 = 13,
};

enum class BlockType : uint8_t {
    MigrationInfo = 0x00, SceneTree = 0x01, TreeNode = 0x02, SceneGlyphItem = 0x03,
    SceneGroupItem = 0x04, SceneLineItem = 0x05, SceneTextItem = 0x06, RootText = 0x07,
    SceneTombstoneItem = 0x08, AuthorIds = 0x09, PageInfo = 0x0A, SceneInfo = 0x0D,
};

// A single sampled pen point. Coordinates are the `.rm` centre-origin float space.
struct Point {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;     // decoded stroke width at this point (rm units)
    float pressure = 0.0f;  // 0..1
};

struct Stroke {
    int tool = 0;                 // Pen
    int color = 0;                // PenColor
    double thicknessScale = 1.0;  // global width multiplier for the stroke
    bool hasRgba = false;
    uint32_t rgba = 0;            // explicit ARGB when hasRgba (Line tag 8)
    std::vector<Point> points;
};

struct Layer {
    QString name;
    bool visible = true;
    std::vector<Stroke> strokes;
};

// One parsed page = one `.rm` file. M1 collapses all strokes into a single layer;
// TreeNode-driven layering is a later-milestone refinement.
struct Page {
    std::vector<Layer> layers;

    // Typed text (RootText 0x07): the concatenated string plus its layout anchor in
    // the same centre-origin coordinate space as strokes. A page can have both typed
    // text and strokes (a "mixed" page).
    QString text;
    bool hasText = false;
    double textX = 0.0, textY = 0.0;   // top-left anchor of the text box
    double textWidth = 0.0;            // wrap width

    // Bounding box over every stroke point (valid when hasContent).
    float minX = 0.0f, minY = 0.0f, maxX = 0.0f, maxY = 0.0f;
    bool hasContent = false;

    int strokeCount() const {
        int n = 0;
        for (const Layer &l : layers) n += int(l.strokes.size());
        return n;
    }
    int pointCount() const {
        int n = 0;
        for (const Layer &l : layers)
            for (const Stroke &s : l.strokes) n += int(s.points.size());
        return n;
    }
};

}  // namespace rm
