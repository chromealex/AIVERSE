#pragma once
#include "StudioPanelRenderTarget.h"
#include "StudioOverlayImages.h"
#include "apps/editor/core/OverlayPreviewGrid.h"
#include "engine/view/game/GameView.h"
#include "engine/view/game/GameStaticPreviewModel.h"
#include "engine/view/GeometryBounds.h"

namespace aiverse::studio {

struct StudioPlacementGhost {
    std::string project, type;
    Json rules, manifest;
    game::MapGrid grid;
    render::float3 center;
    bool buildable;

    StudioPlacementGhost(const editor::Workspace& ws, const Json& placement, const std::string& typeId)
        : project(ws.project.dir), type(typeId), rules(ws.project.rulesJson), manifest(ws.project.manifest),
          buildable(placement.getBool("ok")) {
        const auto& map=ws.map.data;
        const float x=float(placement["x"].toDouble()), z=float(placement["z"].toDouble());
        const int x0=int(std::floor(x-placement.getInt("footprintW")*.5+.01))-int(placement.getInt("left"));
        const int z0=int(std::floor(z-placement.getInt("footprintH")*.5+.01))-int(placement.getInt("top"));
        const auto& rows=placement["rows"];
        grid.init(int(rows[0].str().size()),int(rows.size()));
        for(int j=0;j<grid.h;++j)for(int i=0;i<grid.w;++i)
            editor::copyPreviewCell(map,ws.project.rules,map.idx(x0+i,z0+j),grid,grid.index(i,j));
        if(!map.corners.empty()) {
            grid.corner.resize(size_t(grid.w+1)*(grid.h+1));
            for(int j=0;j<=grid.h;++j)for(int i=0;i<=grid.w;++i)
                grid.corner[grid.cornerIndex(i,j)]=map.corners[size_t(z0+j)*(map.w+1)+x0+i];
        }
        center={x-x0,0,z-z0};
    }

    std::vector<uint8_t> frame() const {
        const auto definitions=game::loadRules(rules);
        const int index=definitions.findType(type);
        if(index<0)throw std::runtime_error("Structure type is unavailable");
        StudioPanelRenderTarget target;
        game::GameView view;
        view.setGpuDeviceLease(target.lease());
        view.setDeferredLoading(true);
        view.setContentBase(project+"/");
        const auto read=[&](const std::string& path,std::string& bytes){
            if(path=="game.json")bytes=manifest.dump();
            else if(path==manifest.getStr("rules","rules.json"))bytes=rules.dump();
            else return readStudioOverlayAsset(project,path,bytes);
            return true;
        };
        std::vector<uint8_t> pixels;
        try {
            if(!view.init(target.device(),definitions,read))throw std::runtime_error("Placement model initialization failed");
            view.loadType(uint16_t(index));
            const auto& structure=definitions.types[size_t(index)];
            const float footprint=float(std::max(structure.footprintW,structure.footprintH));
            const float height=float(structure.view["height"].toDouble(footprint));
            render::Camera camera;
            camera.pitch=.48f;camera.yaw=.68f;
            camera.target={center.x,view.heightAt(grid,center.x,center.z)+height*.45f,center.z};
            camera.distance=std::max({2.f,footprint,height,view.selectionRadius(uint16_t(index))*2})*2.4f;
            game::view_adapter::StaticPreviewModel spec;
            aiverse::view::Object packet;
            if(view.resolveStatic(uint16_t(index),1,spec) &&
               game::view_adapter::buildStaticGhostPacket(spec,center,view.heightAt(grid,center.x,center.z),{1,1,1,1},packet)) {
                const auto bounds=aiverse::view::transformAabb(spec.model->boundsMin,spec.model->boundsMax,packet.draw.world);
                if(bounds.valid) {
                    const float radius=std::max(.05f,render::length(bounds.max-bounds.min)*.5f);
                    camera.target=(bounds.min+bounds.max)*.5f;
                    camera.distance=radius/std::sin(camera.fovY*.5f)*1.03f;
                }
            }
            camera.zNear=std::min(.5f,camera.distance*.1f);
            camera.zFar=camera.distance*4;
            pixels=target.captureFrame(camera,[&](render::Renderer3D& renderer){
                view.drawGhost(renderer,uint16_t(index),center,buildable,grid,1.0);
            });
        } catch(...) {
            view.shutdown(target.device());
            throw;
        }
        view.shutdown(target.device());
        return pixels;
    }
};

}
