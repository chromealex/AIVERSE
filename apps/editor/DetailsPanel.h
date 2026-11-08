// Inspector «Детали» (docs/editor-world-tree.md): everything about the selected world objects — one or many.
// Header card, the map document fields (edit mode, editable; several objects: shared values, "—" when they
// differ, an edit applies to all as one undo step), effective stats, every ECS component with its fields,
// Lua data, the rules block of the type. Live / game-created objects are read-only. Search over properties,
// copy a value / path / section as JSON.
#pragma once
#include <string>
#include <vector>
#include "Editor.h"

namespace rts::editor {

class DetailsPanel {
public:
    void draw(Editor& ed);

private:
    struct Item { Selection sel; int doc = -1; bool hasRow = false; WorldRow row; Json data; };
    void collect(Editor& ed);
    void header(Editor& ed);
    void mapFields(Editor& ed);
    void sections(Editor& ed);
    void section(Editor& ed, const char* id, const char* title, const std::vector<const Json*>& values, size_t of, bool defaultOpen);
    void property(Editor& ed, const std::string& key, const Json& v, const std::string& path);
    bool matches(const std::string& key, const Json& v) const;
    void rowMenu(const std::string& path, const Json& v);

    std::vector<Item> items_;
    uint64_t cacheKey_ = 0;
    std::string filter_;
    float posEdit_[2] = {0, 0};
    bool posActive_ = false;
    int truncated_ = 0;
};

} // namespace rts::editor
