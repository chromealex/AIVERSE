// Deterministic benchmark asset: a tapered, leaning trunk and fourteen
// arching fronds with individual leaflets. Bake both variants from this mesh.
void makePalmFixture(uint32_t, render::Model& model) {
    model = render::Model{};
    model.materials.resize(2);
    model.materials[0].baseColor = {0.42f,0.26f,0.13f,1};
    model.materials[1].baseColor = {0.12f,0.45f,0.06f,1};
    model.meshes.resize(1);
    model.nodes.resize(1);
    model.nodes[0].mesh = 0;
    const auto triangle = [&](render::float3 a, render::float3 b, render::float3 c) {
        const auto normal = render::normalize(render::cross(b-a,c-a));
        for (auto pos : {a,b,c}) {
            render::ModelVertex v{};
            v.pos = pos;
            v.nrm = normal;
            model.indices.push_back(uint32_t(model.vertices.size()));
            model.vertices.push_back(v);
        }
    };
    constexpr float tau = 6.2831853f;
    const auto trunk = [&](int ring,int side) -> render::float3 {
        const float t = ring/8.f, r = 0.26f - 0.12f*t, a = tau*side/10;
        return {0.9f*t*t + r*std::cos(a), 9*t, r*std::sin(a)};
    };
    for (int ring=0; ring<8; ++ring) for (int side=0; side<10; ++side) {
        auto a=trunk(ring,side), b=trunk(ring+1,side);
        auto c=trunk(ring+1,side+1), d=trunk(ring,side+1);
        triangle(a,b,c);
        triangle(a,c,d);
    }
    render::Primitive bark{};
    bark.indexCount = bark.vertexCount = uint32_t(model.indices.size());
    bark.material = 0;
    model.meshes[0].primitives.push_back(bark);
    for (int frond=0; frond<14; ++frond) {
        const float angle = tau*frond/14, length = 3.8f+0.6f*(frond%3);
        const render::float3 axis{std::cos(angle),0,std::sin(angle)};
        const render::float3 side{-axis.z,0,axis.x};
        const auto stem = [&](float t) {
            return render::float3{0.9f,9+2.3f*std::sin(3.14f*t)-(frond%3)*0.8f*t,0}
                + axis*(length*t);
        };
        for (int j=0; j<16; ++j) {
            float t=j/16.f;
            auto a=stem(t), b=stem(t+1.f/16);
            triangle(a-side*0.05f,b-side*0.05f,b+side*0.05f);
            triangle(a-side*0.05f,b+side*0.05f,a+side*0.05f);
            const float spread=0.12f+0.68f*std::sin(3.14f*t);
            for (int sign : {-1,1}) {
                auto tip=b+side*(sign*spread)+axis*0.30f;
                tip.y -= spread;
                triangle(a,b,tip);
            }
        }
    }
    render::Primitive leaves{};
    leaves.material=1;
    leaves.firstIndex=leaves.firstVertex=bark.indexCount;
    leaves.vertexCount=leaves.indexCount=uint32_t(model.indices.size())-bark.indexCount;
    model.meshes[0].primitives.push_back(leaves);
    model.finalize();
}

bool comparePalmBounds(id<MTLDevice> metal, gfx::Device& device, render::Renderer3D& renderer,
                       const terra::TreeProfile& profile, render::Camera camera) {
    terra::ArborForestAdapter forests[2];
    auto grove = comparisonGrove(profile);
    Target target = makeTarget(device,1024,640);
    if (!valid(target)) return false;
    camera.fovY=0.78f;
    camera.pitch=0.30f;
    camera.zFar=500;
    for (int mode=0; mode<2; ++mode) {
        auto& forest=forests[mode];
        forest.setTreeGeometry(terra::ArborTreeGeometry::Merged);
        forest.setPrototypeDecorator(makePalmFixture);
        if (mode==0) forest.setImpostorContourBands(0); // compare forced full to automatic
        forest.addPrototype(profile,0x517eeda1u);
        const auto* model=forest.billboardPrototype(profile.speciesId);
        if (!model || model->indices.size()!=12 || model->vertices.size()!=8) return false;
    }
    constexpr int views=8;
    const int distances[]={80,140,220};
    uint64_t fragments[3][2]{};
    // Camera-specific transforms require the CPU geometry retained before upload.
    for (int d=0; d<3; ++d) for (int v=0; v<views; ++v) {
        camera.distance=distances[d];
        camera.yaw=0.13f+v*6.2831853f/views;
        renderer.beginScene(camera,target.width,target.height);
        for (int mode=0; mode<2; ++mode) {
            auto n=countCoverage(metal,forests[mode],grove,renderer,target);
            if (!n) return false;
            fragments[d][mode]+=n;
        }
    }
    for (auto& forest:forests) if (!forest.uploadModels(device)) return false;
    bool ok=true;
    for (int d=0; d<3; ++d) {
        camera.distance=distances[d];
        std::vector<double> timings[2];
        int worstMask=0,worstRgb=0;
        double worstMaskFraction=0;
        for (int v=0; v<views; ++v) {
            camera.yaw=0.13f+v*6.2831853f/views;
            std::vector<uint8_t> image[2],background;
            uint32_t calls[2]{};
            if (!device.beginOffscreenFrame()) return false;
            renderer.beginScene(camera,target.width,target.height);
            renderer.render(pass(target));
            device.endPass();
            device.endFrame();
            if (!device.readColorRGBA8(target.color,background)) return false;
            for (int pair=0; pair<10; ++pair) for (int order=0; order<2; ++order) {
                const int mode=(pair+v+order)%2;
                if (!device.beginOffscreenFrame()) return false;
                renderer.beginScene(camera,target.width,target.height);
                forests[mode].submit(renderer,grove);
                if (!device.timeGpuFrame(1+mode)) return false;
                renderer.render(pass(target));
                calls[mode]=renderer.drawCalls();
                device.endPass();
                device.endFrame();
                if (!device.waitForResourceIdle()) return false;
                for (const auto& t:device.takeGpuTimings())
                    if (pair>=2 && t.valid && t.tag==uint64_t(mode+1)) timings[mode].push_back(t.milliseconds);
                if (pair==1 && !device.readColorRGBA8(target.color,image[mode])) return false;
            }
            ok &= calls[0]>0 && calls[0]==calls[1];
            int changed=0,visible=0;
            for (size_t i=0; i<background.size(); i+=4) {
                bool covered[2]{};
                for (int mode=0; mode<2; ++mode) for (int c=0; c<3; ++c)
                    covered[mode] |= std::abs(int(image[mode][i+c])-int(background[i+c]))>16;
                visible+=covered[0];
                changed+=covered[0]!=covered[1];
            }
            const int rgb=diff(image[0],image[1],target).changed;
            worstRgb=std::max(worstRgb,rgb);
            worstMask=std::max(worstMask,changed);
            worstMaskFraction=std::max(worstMaskFraction,double(changed)/std::max(1,visible));
            ok &= visible>100 && rgb<=std::max(4,visible/1000) && changed<=std::max(4,visible/1000);
            if (d==0 && v==0 && (!capture("palm-full",target,image[0]) ||
                                  !capture("palm-trim",target,image[1]))) return false;
        }
        for (int mode=0; mode<2; ++mode) {
            auto& times=timings[mode];
            if (times.size()!=64) return false;
            std::sort(times.begin(),times.end());
            std::printf("palm distance=%d mode=%s diagnosticFragments=%llu medianMs=%.6f samples=64 views=8\n",
                distances[d],mode?"trim":"full",(unsigned long long)fragments[d][mode],(times[31]+times[32])/2);
        }
        std::printf("palm distance=%d maxMaskDelta=%d maxMaskFraction=%.6f maxRgbDelta=%d\n",
                    distances[d],worstMask,worstMaskFraction,worstRgb);
        ok &= fragments[d][1]<fragments[d][0];
    }
    for (auto& forest:forests) forest.releaseModels(device);
    destroyTarget(device,target);
    return ok;
}
