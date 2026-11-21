#include "apps/editor/core/DocumentLightIdentity.h"
// FPS authoring lighting adapter: modify a COPY and commit it via fps_set.
// Do not use MapDoc.extra for FPS lighting: MapDoc::toJson() serializes fps instead.
#pragma once
#include "apps/editor/core/LightOps.h"
#include "StudioFpsLightingContract.h"
#include <stdexcept>
#include <string>

namespace aiverse::studio {
inline Json fpsLightingView(const Json& document) {
    Json result = Json::object();
    result.set("ok", Json::fromBool(true));
    result.set("lights", document["lights"].isArray() ? document["lights"] : Json::array());
    result.set("lighting", document["lighting"]);
    return result;
}
inline int64_t fpsLightingIndex(const Json& params, const Json& items) {
    const Json& raw = params["index"];
    if (!raw.isNumber() || raw.toDouble() != double(raw.integer()))
        throw std::runtime_error("Light index must be an integer");
    const int64_t index=raw.integer(-1);
    if (index < 0 || !items.isArray() || uint64_t(index) >= items.size())
        throw std::runtime_error("Unknown light index");
    return index;
}
inline Json editFpsLighting(Json document, const std::string& method, const Json& params) {
    if (!document.isObject()) throw std::runtime_error("FPS project document unavailable");
    if (method=="studio.lightingSet") {
        const std::string key=params.getStr("key");
        const Json& value=params["value"];
        if (!validFpsLightingChange(method,key,value.toDouble(),value.isNumber(),false,{},false))
            throw std::runtime_error("Invalid lighting parameter");
        Json block=document["lighting"].isObject() ? document["lighting"] : Json::object();
        block.set(key,value);
        document.set("lighting",std::move(block));
    } else if (method=="studio.lightAdd") {
        Json item=Json::object();
        item.set("x",Json::number(int64_t(0)));
        item.set("z",Json::number(int64_t(0)));
        item.set("height",Json::number(int64_t(1)));
        item.set("radius",Json::number(int64_t(4)));
        item.set("intensity",Json::number(1.5,3));
        if (!document["lights"].isArray()) document.set("lights",Json::array());
        document.member("lights").push(std::move(item));
    } else if (method=="studio.lightUpdate" || method=="studio.lightDelete") {
        const int64_t index=fpsLightingIndex(params,document["lights"]);
        if (method=="studio.lightDelete") document.member("lights").eraseAt(size_t(index));
        else {
            const std::string key=params.getStr("key");
            const Json& value=params["value"];
            if (!validFpsLightingChange(method,key,value.toDouble(),value.isNumber(),
                                        value.isString(),value.str(),value.type()==Json::Type::Bool))
                throw std::runtime_error("Invalid light property");
            Json patch=Json::object();
            patch.set(key,value);
            editor::applyLightParams(document.member("lights").at(size_t(index)),patch);
        }
    } else throw std::runtime_error("Unsupported lighting method");
    editor::ensureDocumentLightIds(document);
    return document;
}
} // namespace aiverse::studio
