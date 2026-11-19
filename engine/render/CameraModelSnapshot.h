// AIVERSE LENS — one-frame owned snapshot of finalized ModelDraw calls.
// Record after a single animation/View update. Secondary cameras replay
// models and attachments but NEVER advance animation or redispatch events.
#pragma once
#include "Renderer3D.h"
#include "engine/view/View.h"
#include <cstddef>
#include <utility>
#include <vector>
#include <unordered_set>

namespace aiverse::render {
class CameraModelSnapshot {
    struct Entry {
        ModelDraw draw;
        std::vector<mat4> nodeWorld;
        std::vector<Material> materials;
        std::vector<int> nodeMaterial;
        std::vector<NodePose> pose;
        BakedSample baked{};
        bool hasNodeWorld=false,hasMaterials=false,hasNodeMaterial=false;
        bool hasPose=false,hasBaked=false;
    };
    struct PreparedEntry {
        Entry source;
        view::Object object;
        const view::View* viewSource=nullptr;
    };
    std::vector<Entry> entries_;
    std::vector<CameraGeometryCommand> geometry_;
    std::vector<PreparedEntry> prepared_;
    std::unordered_set<uint64_t> preparedDrawIds_;
    size_t dropped_=0,maximum_=100000;
public:
    explicit CameraModelSnapshot(size_t maxPackets=100000):maximum_(maxPackets){}
    void clear(){
        entries_.clear();prepared_.clear();geometry_.clear();
        preparedDrawIds_.clear();dropped_=0;
    }
    size_t size()const{return entries_.size();}
    size_t dropped()const{return dropped_;}
    size_t preparedCount()const{return prepared_.size();}
    size_t geometryCount()const{return geometry_.size();}
    void reserve(size_t count){entries_.reserve(count);}
    // RTS (or any native presentation adapter) may publish a BEFORE-LOD
    // object while its View owns the evaluated animation pose. The packet
    // itself remains borrowed only during this frame's render submissions.
    void recordPrepared(const view::View& source,const view::Object& packet) {
        if(!packet.id || !packet.draw.model)return;
        if(prepared_.size()>=maximum_){++dropped_;return;}
        PreparedEntry prepared;
        prepared.source=copyDraw(packet.draw);
        prepared.object=packet;
        prepared.object.draw=prepared.source.draw;
        prepared.viewSource=&source;
        const uint64_t drawId=packet.draw.persistentId?
            packet.draw.persistentId:packet.id;
        prepared_.push_back(std::move(prepared));
        preparedDrawIds_.insert(drawId);
    }
    static Entry copyDraw(const ModelDraw& source) {
        Entry e;
        e.draw=source;
        e.draw.staticCache=nullptr;
        if(source.nodeWorld){e.nodeWorld=*source.nodeWorld;e.hasNodeWorld=true;}
        if(source.materials){e.materials=*source.materials;e.hasMaterials=true;}
        if(source.nodeMaterial){e.nodeMaterial=*source.nodeMaterial;e.hasNodeMaterial=true;}
        if(source.pose){e.pose=*source.pose;e.hasPose=true;}
        if(source.baked){e.baked=*source.baked;e.hasBaked=true;}
        e.draw.nodeWorld=nullptr;e.draw.materials=nullptr;
        e.draw.nodeMaterial=nullptr;e.draw.pose=nullptr;e.draw.baked=nullptr;
        return e;
    }
    static ModelDraw ownedDraw(const Entry& e) {
        ModelDraw d=e.draw;
        if(e.hasNodeWorld)d.nodeWorld=&e.nodeWorld;
        if(e.hasMaterials)d.materials=&e.materials;
        if(e.hasNodeMaterial)d.nodeMaterial=&e.nodeMaterial;
        if(e.hasPose)d.pose=&e.pose;
        if(e.hasBaked)d.baked=&e.baked;
        return d;
    }
    void record(const ModelDraw& source) {
        if(!source.model)return;
        if(entries_.size()>=maximum_){++dropped_;return;}
        entries_.push_back(copyDraw(source));
    }
    void recordGeometry(const CameraGeometryCommand& command) {
        if(geometry_.size()>=maximum_){++dropped_;return;}
        geometry_.push_back(command);
    }
    void beginCapture(Renderer3D& renderer) {
        clear();
        renderer.setModelPacketTap([this](const ModelDraw& draw){record(draw);});
        renderer.setGeometryPacketTap([this](const CameraGeometryCommand& packet){
            recordGeometry(packet);
        });
    }
    void endCapture(Renderer3D& renderer) {
        renderer.setModelPacketTap({});
        renderer.setGeometryPacketTap({});
    }
    class CaptureGuard {
        CameraModelSnapshot* snapshot_=nullptr;
        Renderer3D* renderer_=nullptr;
    public:
        CaptureGuard()=default;
        CaptureGuard(CameraModelSnapshot& snapshot,Renderer3D& renderer) {
            begin(snapshot,renderer);
        }
        void begin(CameraModelSnapshot& snapshot,Renderer3D& renderer) {
            end();
            snapshot_=&snapshot;renderer_=&renderer;
            snapshot_->beginCapture(renderer);
        }
        CaptureGuard(const CaptureGuard&)=delete;
        CaptureGuard& operator=(const CaptureGuard&)=delete;
        ~CaptureGuard(){end();}
        void end() {
            if(snapshot_&&renderer_)snapshot_->endCapture(*renderer_);
            snapshot_=nullptr;renderer_=nullptr;
        }
    };
    CaptureGuard capture(Renderer3D& renderer) {
        return CaptureGuard(*this,renderer);
    }
    bool isPreparedDraw(uint64_t persistentId)const {
        return persistentId!=0 && preparedDrawIds_.count(persistentId)!=0;
    }
    template<class Fn>
    void forEachPrepared(Fn&& fn)const {
        for(const auto& entry:prepared_) {
            if(!entry.viewSource)continue;
            view::Object packet=entry.object;
            packet.draw=ownedDraw(entry.source);
            fn(*entry.viewSource,packet);
        }
    }
    template<class Fn>
    void forEachFallback(Fn&& fn)const {
        for(const auto& entry:entries_) {
            const ModelDraw draw=ownedDraw(entry);
            if(isPreparedDraw(draw.persistentId))continue;
            fn(draw);
        }
    }
    template<class Fn>
    void forEachGeometry(Fn&& fn)const {
        for(const auto& packet:geometry_)fn(packet);
    }
    // The caller must first endCapture(), then beginScene() with each
    // secondary camera. Model/texture resources are borrowed from the host;
    // do not invalidate them until all camera submissions are complete.
    template<class Submit>
    void forEach(Submit&& submit)const {
        for(const auto& e:entries_)submit(ownedDraw(e));
    }
    // Requires a valid source View and model resource epoch for this frame.
    // Prepared base objects select per-camera LOD, while legacy packets and
    // attachments from the main view remain in the finalized fallback stream.
    void submit(Renderer3D& renderer,uint64_t cameraScope=0)const {
        forEachGeometry([&](const CameraGeometryCommand& packet) {
            switch(packet.kind) {
                case CameraGeometryCommand::Kind::Mesh:
                    if(packet.mesh)renderer.drawMesh(*packet.mesh,packet.world,
                        packet.tint,packet.flags,packet.specular,packet.emissive);
                    break;
                case CameraGeometryCommand::Kind::Line:
                    renderer.line(packet.a,packet.b,packet.rgba);
                    break;
                case CameraGeometryCommand::Kind::Triangle:
                    renderer.triangle(packet.a,packet.b,packet.c,packet.rgba);
                    break;
            }
        });
        forEachFallback([&](const ModelDraw& d){renderer.drawModel(d);});
        forEachPrepared([&](const view::View& source,const view::Object& packet) {
            source.submitOne(renderer,packet,cameraScope);
        });
    }
};
} // namespace aiverse::render
