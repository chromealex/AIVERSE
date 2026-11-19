// TerraWeave editor integration: real Workspace operation registration,
// MapDoc undo/redo, overwrite protection, and optional generator metadata.
#include "apps/editor/core/Workspace.h"
#include <cassert>
#include <cstdint>
#include <vector>

using namespace aiverse;
using namespace aiverse::editor;

int main() {
    Workspace ws;
    ws.project.dir = "."; // operations require an open game; source=editor avoids log I/O
    ws.map.reset(16, 12);

    Json config = Json::object();
    config.set("seed", Json::number(int64_t(10)));
    config.set("baseHeightCm", Json::number(int64_t(0)));
    config.set("reliefCm", Json::number(int64_t(1600)));
    config.set("basePeriodCm", Json::number(int64_t(2200)));
    config.set("octaves", Json::number(int64_t(4)));
    const auto beforeRevision = ws.map.revision;

    const Json preview = ws.call("terraweave_preview", config, "editor");
    assert(preview.getBool("ok", false));
    assert(!preview.has("error"));
    assert(preview["samplesCm"].size() == 9);
    assert(ws.map.revision == beforeRevision);
    assert(ws.map.data.corners.empty());
    assert(!ws.map.data.extra.has("terraweave"));

    const Json generated = ws.call("terraweave_generate", config, "editor");
    assert(generated.getBool("ok", false));
    assert(!generated.has("error"));
    const std::vector<int16_t> heights = ws.map.data.corners;
    assert(heights.size() == 17 * 13);
    assert(ws.map.data.extra["terraweave"].getInt("version", 0) == 1);

    const auto afterFirst = ws.map.revision;
    const Json blocked = ws.call("terraweave_generate", config, "editor");
    assert(blocked.has("error"));
    assert(ws.map.revision == afterFirst);
    assert(ws.map.data.corners == heights);

    ws.map.undo();
    assert(ws.map.data.corners.empty());
    assert(!ws.map.data.extra.has("terraweave"));
    ws.map.redo();
    assert(ws.map.data.corners == heights);
    assert(ws.map.data.extra.has("terraweave"));

    // An authored detail surface masks corner heights in the view.
    // Explicit replacement must clear it, but Undo must restore it.
    ws.map.beginEdit();
    ws.map.data.detailScale = 2;
    ws.map.data.detail.assign(size_t(16 * 2 + 1) * size_t(12 * 2 + 1), 77);
    ws.map.data.detailSplat.assign(ws.map.data.detail.size() * 4, 128);
    ws.map.data.detailFile = "maps/authored.detail.r16";
    ws.map.touched();
    const auto authoredDetail = ws.map.data.detail;
    const auto authoredSplat = ws.map.data.detailSplat;
    const auto detailRevision = ws.map.revision;
    assert(ws.call("terraweave_generate", config, "editor").has("error"));
    assert(ws.map.revision == detailRevision);

    config.set("overwrite", Json::fromBool(true));
    const Json replaced = ws.call("terraweave_generate", config, "editor");
    assert(replaced.getBool("ok", false) && !replaced.has("error"));
    assert(ws.map.data.detail.empty());
    assert(ws.map.data.detailSplat.empty());
    assert(ws.map.data.detailScale == 0);
    assert(ws.map.data.detailFile.empty());
    ws.map.undo();
    assert(ws.map.data.detail == authoredDetail);
    assert(ws.map.data.detailSplat == authoredSplat);
    assert(ws.map.data.detailScale == 2);
    assert(ws.map.data.detailFile == "maps/authored.detail.r16");
    ws.map.redo();
    assert(ws.map.data.detail.empty());

    // Saved JSON must include the explicit generator recipe.
    const Json saved = ws.map.toJson();
    assert(saved.has("terraweave") ||
           (saved.has("terrain") && saved["terrain"].has("terraweave")));
    return 0;
}
