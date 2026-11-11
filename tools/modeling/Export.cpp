#include "Modeling.h"
#include "tools/branding/BrandImage.h"
#include <bit>
#include <cstring>
namespace aiverse::modeling {
namespace {
Json num(int64_t n) {
    return Json::number(n);
}
Json floats(std::initializer_list<float> list) {
    auto a = Json::array();
    for (float f : list)
        a.push(Json::number(double(f), 8));
    return a;
}
void u32(std::string &s, uint32_t v) {
    for (int i = 0; i < 4; ++i)
        s.push_back(char((v >> (i * 8)) & 255));
}
void f32(std::string &s, float v) {
    u32(s, std::bit_cast<uint32_t>(v));
}
struct Writer {
    Json root = Json::parse(
        R"({"asset":{"version":"2.0","generator":"AIVERSE modeling"},"scene":0,"scenes":[],"nodes":[],"meshes":[],"materials":[],"skins":[],"animations":[],"bufferViews":[],"accessors":[],"buffers":[]})");
    std::string bin;
    int accessor(const std::vector<float> &v, int comps, const char *type, bool bounds = false) {
        std::string data;
        for (float f : v)
            f32(data, f);
        return add(data, 5126, int(v.size() / size_t(comps)), type, bounds ? &v : nullptr, comps);
    }
    int add(const std::string &data, int component, int count, const char *type,
            const std::vector<float> *bounds = nullptr, int comps = 1) {
        while (bin.size() % 4)
            bin.push_back(0);
        auto view = Json::object();
        view.set("buffer", num(0));
        view.set("byteOffset", num(int64_t(bin.size())));
        view.set("byteLength", num(int64_t(data.size())));
        int vi = int(root["bufferViews"].size());
        root.member("bufferViews").push(view);
        bin += data;
        auto a = Json::object();
        a.set("bufferView", num(vi));
        a.set("componentType", num(component));
        a.set("count", num(count));
        a.set("type", Json::string(type));
        if (bounds && count) {
            auto lo = Json::array(), hi = Json::array();
            for (int c = 0; c < comps; ++c) {
                float mn = (*bounds)[size_t(c)], mx = mn;
                for (size_t i = size_t(c); i < bounds->size(); i += size_t(comps)) {
                    mn = std::min(mn, (*bounds)[i]);
                    mx = std::max(mx, (*bounds)[i]);
                }
                lo.push(Json::number(double(mn), 8));
                hi.push(Json::number(double(mx), 8));
            }
            a.set("min", lo);
            a.set("max", hi);
        }
        int ai = int(root["accessors"].size());
        root.member("accessors").push(a);
        return ai;
    }
};
} // namespace
std::string exportGlb(const Model &m) {
    Writer w;
    auto scene = Json::object(), roots = Json::array();
    for (size_t i = 0; i < m.nodes.size(); ++i) {
        auto &n = m.nodes[i];
        auto j = Json::object();
        j.set("name", Json::string(n.name));
        j.set("translation", floats({n.t.x, n.t.y, n.t.z}));
        j.set("rotation", floats({n.r.x, n.r.y, n.r.z, n.r.w}));
        j.set("scale", floats({n.s.x, n.s.y, n.s.z}));
        if (n.mesh >= 0)
            j.set("mesh", num(n.mesh));
        if (n.skin >= 0)
            j.set("skin", num(n.skin));
        if (!n.children.empty()) {
            auto ch = Json::array();
            for (int c : n.children)
                ch.push(num(c));
            j.set("children", ch);
        }
        if (n.parent < 0)
            roots.push(num(int64_t(i)));
        w.root.member("nodes").push(j);
    }
    scene.set("nodes", roots);
    w.root.member("scenes").push(scene);
    if (!m.images.empty()) {
        auto images=Json::array(),textures=Json::array(),samplers=Json::array();
        for (const auto& im:m.images) {
            require(im.width>0 && im.height>0 && im.rgba.size()==size_t(im.width)*size_t(im.height)*4,
                    "GLB export: texture CPU pixels unavailable");
            branding::Image pixels;pixels.width=im.width;pixels.height=im.height;pixels.rgba=im.rgba;
            const auto png=branding::encodePng(pixels);
            require(!png.empty(),"GLB export: PNG encoding failed");
            while(w.bin.size()%4)w.bin.push_back(0);
            auto view=Json::object();view.set("buffer",num(0));view.set("byteOffset",num(int64_t(w.bin.size())));view.set("byteLength",num(int64_t(png.size())));
            auto image=Json::object();image.set("bufferView",num(int64_t(w.root["bufferViews"].size())));image.set("mimeType",Json::string("image/png"));image.set("name",Json::string(im.name));
            w.root.member("bufferViews").push(view);w.bin+=png;images.push(image);
            auto sampler=Json::object();sampler.set("wrapS",num(im.repeat?10497:33071));sampler.set("wrapT",num(im.repeat?10497:33071));
            sampler.set("magFilter",num(im.nearest?9728:9729));sampler.set("minFilter",num(im.nearest?9984:9987));samplers.push(sampler);
            auto tex=Json::object();tex.set("source",num(int64_t(textures.size())));tex.set("sampler",num(int64_t(textures.size())));textures.push(tex);
        }
        w.root.set("images",images);w.root.set("textures",textures);w.root.set("samplers",samplers);
    }
    auto texture=[&](Json& object,const char* key,int id){if(id<0)return;require(size_t(id)<m.images.size(),"GLB export: texture index out of range");
        auto t=Json::object();t.set("index",num(id));object.set(key,t);};
    for (auto &mt : m.materials) {
        auto j = Json::object(), pbr = Json::object();
        j.set("name", Json::string(mt.name));
        pbr.set("baseColorFactor", floats({mt.baseColor.x, mt.baseColor.y, mt.baseColor.z, mt.baseColor.w}));
        pbr.set("metallicFactor", Json::number(double(mt.metallic), 8));
        pbr.set("roughnessFactor", Json::number(double(mt.roughness), 8));
        texture(pbr,"baseColorTexture",mt.baseTex);texture(pbr,"metallicRoughnessTexture",mt.mrTex);
        texture(j,"normalTexture",mt.normalTex);texture(j,"occlusionTexture",mt.occlusionTex);texture(j,"emissiveTexture",mt.emissiveTex);
        if(mt.normalTex>=0)j.member("normalTexture").set("scale",Json::number(double(mt.normalScale),8));
        if(mt.occlusionTex>=0)j.member("occlusionTexture").set("strength",Json::number(double(mt.occlusionStrength),8));
        if(mt.alpha==AlphaMode::Mask){j.set("alphaMode",Json::string("MASK"));j.set("alphaCutoff",Json::number(double(mt.alphaCutoff),8));}
        j.set("pbrMetallicRoughness", pbr);
        j.set("emissiveFactor", floats({mt.emissive.x, mt.emissive.y, mt.emissive.z}));
        j.set("doubleSided", Json::fromBool(mt.doubleSided));
        if (mt.alpha == AlphaMode::Blend)
            j.set("alphaMode", Json::string("BLEND"));
        w.root.member("materials").push(j);
    }
    for (auto &mesh : m.meshes) {
        auto j = Json::object(), prims = Json::array();
        j.set("name", Json::string(mesh.name));
        for (auto &p : mesh.primitives) {
            auto pj = Json::object(), attrs = Json::object();
            std::vector<float> pos, nrm, uv, tan, weights;
            std::string joints;
            for (uint32_t i = 0; i < p.vertexCount; ++i) {
                auto &v = m.vertices[p.firstVertex + i];
                for (float f : {v.pos.x, v.pos.y, v.pos.z})
                    pos.push_back(f);
                for (float f : {v.nrm.x, v.nrm.y, v.nrm.z})
                    nrm.push_back(f);
                for (float f : {v.uv.x, v.uv.y})
                    uv.push_back(f);
                for (float f : {v.tangent.x, v.tangent.y, v.tangent.z, v.tangent.w})
                    tan.push_back(f);
                if (!p.jointPalette.empty())
                    for (int k = 0; k < 4; ++k) {
                        require(v.joints[k] < p.jointPalette.size(), "invalid joint palette slot");
                        uint16_t joint = p.jointPalette[v.joints[k]];
                        joints.push_back(char(joint & 255));
                        joints.push_back(char(joint >> 8));
                        weights.push_back(float(v.weights[k]) / 255.f);
                    }
            }
            attrs.set("POSITION", num(w.accessor(pos, 3, "VEC3", true)));
            attrs.set("NORMAL", num(w.accessor(nrm, 3, "VEC3")));
            attrs.set("TEXCOORD_0", num(w.accessor(uv, 2, "VEC2")));
            attrs.set("TANGENT", num(w.accessor(tan, 4, "VEC4")));
            if (!p.jointPalette.empty()) {
                attrs.set("JOINTS_0", num(w.add(joints, 5123, int(p.vertexCount), "VEC4")));
                attrs.set("WEIGHTS_0", num(w.accessor(weights, 4, "VEC4")));
            }
            std::string indices;
            for (uint32_t i = 0; i < p.indexCount; ++i)
                u32(indices, m.indices[p.firstIndex + i] - p.firstVertex);
            pj.set("attributes", attrs);
            pj.set("indices", num(w.add(indices, 5125, int(p.indexCount), "SCALAR")));
            pj.set("material", num(p.material));
            prims.push(pj);
        }
        j.set("primitives", prims);
        w.root.member("meshes").push(j);
    }
    for (auto &skin : m.skins) {
        auto j = Json::object(), joints = Json::array();
        std::vector<float> matrices;
        for (int n : skin.joints)
            joints.push(num(n));
        for (auto &mat : skin.inverseBind)
            for (int c = 0; c < 4; ++c)
                for (int r = 0; r < 4; ++r)
                    matrices.push_back(mat.at(r, c));
        j.set("name", Json::string(skin.name));
        j.set("joints", joints);
        j.set("inverseBindMatrices", num(w.accessor(matrices, 16, "MAT4")));
        w.root.member("skins").push(j);
    }
    for (auto &clip : m.clips) {
        auto j = Json::object(), channels = Json::array(), samplers = Json::array();
        j.set("name", Json::string(clip.name));
        for (auto &c : clip.channels) {
            auto s = Json::object();
            s.set("input", num(w.accessor(c.times, 1, "SCALAR", true)));
            s.set("output", num(w.accessor(c.values, c.comps, c.comps == 4 ? "VEC4" : "VEC3")));
            s.set("interpolation", Json::string(c.interp == AnimChannel::Step          ? "STEP"
                                                : c.interp == AnimChannel::CubicSpline ? "CUBICSPLINE"
                                                                                       : "LINEAR"));
            auto ch = Json::object(), target = Json::object();
            ch.set("sampler", num(int64_t(samplers.size())));
            target.set("node", num(c.node));
            target.set("path", Json::string(c.path == AnimChannel::Translation ? "translation"
                                            : c.path == AnimChannel::Scale     ? "scale"
                                                                               : "rotation"));
            ch.set("target", target);
            channels.push(ch);
            samplers.push(s);
        }
        j.set("channels", channels);
        j.set("samplers", samplers);
        w.root.member("animations").push(j);
    }
    for (auto key : {"skins", "animations", "meshes", "materials", "accessors", "bufferViews"})
        if (w.root[key].size() == 0)
            w.root.erase(key);
    auto buffer = Json::object();
    buffer.set("byteLength", num(int64_t(w.bin.size())));
    w.root.member("buffers").push(buffer);
    auto js = w.root.dump(-1);
    while (js.size() % 4)
        js += ' ';
    while (w.bin.size() % 4)
        w.bin.push_back(0);
    std::string out = "glTF";
    u32(out, 2);
    u32(out, uint32_t(12 + 8 + js.size() + 8 + w.bin.size()));
    u32(out, uint32_t(js.size()));
    out += "JSON";
    out += js;
    u32(out, uint32_t(w.bin.size()));
    out.append("BIN\0", 4);
    out += w.bin;
    return out;
}
} // namespace aiverse::modeling
