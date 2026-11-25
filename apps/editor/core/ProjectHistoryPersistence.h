// Persistent checkpoint for the saved Project owner only. Asset plans are not
// accepted here: their payloads belong to a different transaction owner.
#pragma once
#include "ProjectHistory.h"
#include <map>
namespace aiverse::editor {
using ProjectHistoryBaseline=std::map<std::string,std::string>;
std::string encodeProjectHistory(const std::filesystem::path& project,const ProjectHistory& history);
ProjectHistory decodeProjectHistory(const std::filesystem::path& project,const std::string& bytes,
                                    const ProjectHistoryBaseline& baseline);
std::optional<std::string> readProjectHistoryCheckpoint(const std::filesystem::path& project);
ProjectFileEdit prepareProjectHistoryCheckpoint(const std::filesystem::path& project,
    const std::optional<std::string>& expected,const ProjectHistory& next);
}
