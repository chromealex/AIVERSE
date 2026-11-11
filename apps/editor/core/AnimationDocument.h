#pragma once
#include "engine/data/Json.h"
namespace aiverse::editor::animation {
Json makeGraph();
Json makeNode(const std::string& kind,const std::string& id,const std::string& input="bind");
Json makeContacts(const std::string& root,const std::string& foot);
Json makeUnit(const std::string& op);
void sort(Json& graph); // Stable topological ordering; throws without changing input on failure.
void connect(Json& graph,const std::string& source,const std::string& target,const std::string& port);
void rename(Json& graph,const std::string& from,const std::string& to);
void remove(Json& graph,const std::string& id); // Refuses connected/output nodes.
}
