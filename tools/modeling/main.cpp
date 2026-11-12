// Headless authoring uses the same Workspace operations as editor and MCP.
#include "apps/editor/core/Workspace.h"
#include <iostream>
namespace aiverse::platform {
bool readContentFile(const std::string &, std::string &) {
    return false;
}
} // namespace aiverse::platform
int main(int argc, char **argv) {
    if (argc < 2) {
        std::cerr << "Usage: aiverse_modeling <project-dir> [engine-dir]\nJSON lines: {op, arguments}\n";
        return 2;
    }
    aiverse::editor::Workspace ws;
    ws.project.dir = argv[1];
    if (argc > 2)
        ws.engineDir = argv[2];
    std::string error;
    if (!ws.openGame(argv[1], &error)) {
        std::cerr << error << "\n";
        return 2;
    }
    std::string line;
    while (std::getline(std::cin, line)) {
        try {
            auto req = aiverse::Json::parse(line);
            std::cout << ws.call(req.getStr("op"), req["arguments"], "agent").dump(-1) << std::endl;
        } catch (const std::exception &e) {
            auto r = aiverse::Json::object();
            r.set("error", aiverse::Json::string(e.what()));
            std::cout << r.dump(-1) << std::endl;
        }
    }
}
