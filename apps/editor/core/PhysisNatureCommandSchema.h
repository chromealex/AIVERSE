#pragma once

namespace aiverse::editor {
inline constexpr char windShowcaseSchema[] = R"({"properties":{"forest":{"type":"boolean"},"hierarchical":{"type":"boolean"},"nature":{"type":"boolean"},"grass":{"type":"boolean"},"flames":{"type":"boolean"},"sound":{"type":"boolean"},"playing":{"type":"boolean"},"windSpeed":{"type":"number"},"windDirection":{"type":"number"},"grassQuality":{"type":"integer"},"grassMeadow":{"description":"auto | legacy | meadow | lushMeadow | dryMeadow | pasture | lawn | steppe | wetland, or a meadow JSON object"},"lodScale":{"type":"number"},"treeGeometry":{"type":"string","enum":["auto","merged","perBranch"]},"horizon":{"type":"boolean"},"debug":{"type":"string","enum":["off","parts","depth","weight","skeleton"]}}})";
inline constexpr char physisNatureCommandSchema[] = R"({"properties":{"enabled":{"type":"boolean"},"playing":{"type":"boolean"},"speed":{"type":"number"},
            "windSpeed":{"type":"number"},"windDirection":{"type":"number"},"humidity":{"type":"number"},
            "cellMeters":{"type":"integer","minimum":0,"maximum":64},"grass":{"type":"boolean"},"flames":{"type":"boolean"},
            "sound":{"type":"boolean"},"volume":{"type":"number"},"shoreSand":{"type":"number"},
            "ignite":{"type":"object","properties":{"x":{"type":"number"},"z":{"type":"number"}},"required":["x","z"]},
            "advanceTicks":{"type":"integer","minimum":0,"maximum":600},"reset":{"type":"boolean"}},"additionalProperties":false})";
}
