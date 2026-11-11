#include "FakeDevice.h"
#include <iostream>
void check(bool b,const char* s){if(!b)throw std::runtime_error(s);}
int main(){try{
    FakeDevice d;cloth::Settings settings;settings.cells=2;auto grid=std::make_shared<cloth::Grid>(settings);std::vector<cloth::Cloth> objects;
    for(int i=0;i<4;++i)objects.emplace_back(grid,cloth::Vec{float(i)*4,0,0});
    cloth::Renderer r;check(r.init(d,4,2,4),"renderer init");size_t sources[]={0,0,2,2};uint32_t handles[4]{};
    for(int f=0;f<4;++f){r.upload(objects,sources);r.draw({});handles[f]=d.bound[0].id;
        check(d.instances==4&&d.indices==96,"instanced triangle count");
        auto& metadata=d.textures.at(d.bound[1].id).data;for(int i=0;i<4;++i){check(metadata[size_t(i)].x==i*4,"lost world origin");check(metadata[size_t(i)].w==i/2,"wrong source tile");}
        check(r.uploadBytes()==(6*3+4)*16,"shared upload budget");
    }
    check(handles[0]!=handles[1]&&handles[1]!=handles[2]&&handles[0]!=handles[2]&&handles[0]==handles[3],"texture ring reuse");
    r.upload(objects);r.draw({});check(r.uploadBytes()==(6*6+4)*16,"split texture did not grow");
    auto& metadata=d.textures.at(d.bound[1].id).data;for(int i=0;i<4;++i)check(metadata[size_t(i)].w==i,"split instance mapping");
    gfx::TextureDesc externalDesc;externalDesc.width=6;externalDesc.height=6;externalDesc.format=gfx::PixelFormat::RGBA32F;
    auto external=d.createTexture(externalDesc);r.uploadGpu(objects,sources,external);r.draw({});
    check(d.bound[0]==external&&r.uploadBytes()==4*16,"GPU renderer uploaded CPU particles");
    for(int i=0;i<4;++i)check(d.textures.at(d.bound[1].id).data[size_t(i)].w==sources[i],"GPU source tile compacted incorrectly");
    r.upload(objects);r.draw({});check(d.bound[0]!=external,"CPU upload retained GPU texture");
    r.shutdown();check(d.textures.size()==1,"renderer destroyed borrowed GPU texture");d.destroyTexture(external);check(d.textures.empty(),"texture leak");std::cout<<"cloth render: group atlas, transforms, split growth and frame texture ring passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
