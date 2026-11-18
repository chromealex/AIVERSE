#pragma once
// Legacy source-compatible PRISMA API. Use PrismaData.h in new code.
// "postfx" remains the serialized game/map key and editor MCP prefix.
#include "PrismaData.h"

namespace aiverse::render {
using PostFxQuality = PrismaQuality;
using PostFxCostClass = PrismaCostClass;
using PostFxType = PrismaType;
using PostFxParamKind = PrismaParamKind;
using PostFxParamDef = PrismaParamDef;
using PostFxTypeDef = PrismaTypeDef;
using PostFxEffect = PrismaEffect;
using PostFxSettings = PrismaSettings;
using PostFxPreset = PrismaPreset;
using PostFxDeviceProfile = PrismaDeviceProfile;
using PostFxWork = PrismaWork;

inline const std::vector<PostFxTypeDef>& postFxCatalog() { return prismaCatalog(); }
inline const PostFxTypeDef* postFxType(const std::string& id) { return prismaType(id); }
inline const PostFxTypeDef& postFxType(PostFxType type) { return prismaType(type); }
inline const char* postFxQualityName(PostFxQuality q) { return prismaQualityName(q); }
inline bool parsePostFxQuality(const std::string& s, PostFxQuality& out) { return parsePrismaQuality(s, out); }
inline const char* postFxCostName(PostFxCostClass c) { return prismaCostName(c); }
inline PostFxSettings parsePostFx(const Json& game, const Json& map,
                                  std::vector<std::string>* issues = nullptr) {
    return parsePrisma(game, map, issues);
}
inline PostFxEffect parsePostFxEffect(const Json& j, bool strict,
                                      std::vector<std::string>* issues = nullptr) {
    return parsePrismaEffect(j, strict, issues);
}
inline Json postFxEffectJson(const PostFxEffect& e) { return prismaEffectJson(e); }
inline PostFxEffect defaultPostFxEffect(PostFxType t) { return defaultPrismaEffect(t); }
inline const std::vector<PostFxPreset>& postFxPresets() { return prismaPresets(); }
inline const PostFxPreset* postFxPreset(const std::string& id) { return prismaPreset(id); }
inline const std::vector<PostFxDeviceProfile>& postFxProfiles() { return prismaProfiles(); }
inline PostFxWork postFxEffectWork(const PostFxEffect& e, PostFxQuality q,
                                   bool ignoreMinQuality = false) {
    return prismaEffectWork(e,q,ignoreMinQuality);
}
inline PostFxWork postFxBaseWork() { return prismaBaseWork(); }
inline float postFxMs(const PostFxWork& w,const PostFxDeviceProfile& p) { return prismaMs(w,p); }
inline Json postFxCostReport(const PostFxSettings& s) { return prismaCostReport(s); }
inline std::vector<std::string> postFxEffectWarnings(const PostFxEffect& e) { return prismaEffectWarnings(e); }
} // namespace aiverse::render
