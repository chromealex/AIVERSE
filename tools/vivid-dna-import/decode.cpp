// Offline adapter only. Epic's reader is supplied externally, never vendored or
// linked into VIVID runtime. Emits neutral LOD geometry + one exact named target;
// this is NOT a RigLogic evaluator or a claim of 52-channel semantic equivalence.
#include <dnacalib/DNACalib.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

static void number(std::ostream& out,float n) {
    if(!std::isfinite(n))throw std::runtime_error("Nonfinite DNA geometry");
    out<<n;
}
template<class V> void xyz(std::ostream& out,const V& v) {
    out<<'[';number(out,v.x);out<<',';number(out,v.y);out<<',';number(out,v.z);out<<']';
}
template<class V> std::string text(V v) {
    std::string value(v.data(),v.size());
    for(unsigned char c:value)if(c<32)throw std::runtime_error("Control character in DNA name");
    return value;
}
int main(int argc,char** argv) try {
    if(argc<3||argc>5)throw std::runtime_error("Usage: vivid_dna_decode input.dna output.json [lod=0] [exact-target-name]");
    if(std::filesystem::exists(argv[2]))throw std::runtime_error("Output already exists");
    const auto bytes=std::filesystem::file_size(argv[1]);
    if(bytes<16||bytes>512ull*1024*1024)throw std::runtime_error("DNA file outside 16B..512MiB budget");
    auto input=dnac::makeScoped<dnac::FileStream>(argv[1],dnac::FileStream::AccessMode::Read,dnac::FileStream::OpenMode::Binary);
    auto dna=dnac::makeScoped<dnac::BinaryStreamReader>(input.get());
    dna->read();
    if(!dnac::Status::isOk())throw std::runtime_error("Official DNA reader rejected file");
    std::size_t consumed=0;
    int lod=argc>3?std::stoi(argv[3],&consumed):0;
    if(argc>3 && consumed!=std::string(argv[3]).size())throw std::runtime_error("Invalid LOD");
    if(lod<0||lod>=dna->getLODCount())throw std::runtime_error("LOD out of range");
    const std::string target=argc>4?argv[4]:"";
    const auto temporary=std::string(argv[2])+".partial";
    if(std::filesystem::exists(temporary))throw std::runtime_error("Partial output already exists");
    std::ofstream out(temporary,std::ios::binary);out.exceptions(std::ios::failbit|std::ios::badbit);
    out<<std::setprecision(9)<<"{\"version\":1,\"name\":"<<std::quoted(text(dna->getName()))<<",\"lod\":"<<lod<<",\"meshes\":[";
    bool firstMesh=true,found=target.empty();std::size_t total=0;
    for(auto mesh:dna->getMeshIndicesForLOD(static_cast<std::uint16_t>(lod))) {
        const auto layouts=dna->getVertexLayoutCount(mesh);
        total+=layouts;if(total>1000000)throw std::runtime_error("LOD exceeds vertex budget");
        if(!firstMesh)out<<',';firstMesh=false;
        out<<"{\"name\":"<<std::quoted(text(dna->getMeshName(mesh)))<<",\"positions\":[";
        for(std::uint32_t i=0;i<dna->getVertexPositionCount(mesh);++i){if(i)out<<',';xyz(out,dna->getVertexPosition(mesh,i));}
        out<<"],\"normals\":[";
        for(std::uint32_t i=0;i<dna->getVertexNormalCount(mesh);++i){if(i)out<<',';xyz(out,dna->getVertexNormal(mesh,i));}
        out<<"],\"uv\":[";
        for(std::uint32_t i=0;i<dna->getVertexTextureCoordinateCount(mesh);++i){if(i)out<<',';auto uv=dna->getVertexTextureCoordinate(mesh,i);out<<'[';number(out,uv.u);out<<',';number(out,uv.v);out<<']';}
        out<<"],\"layouts\":[";
        for(std::uint32_t i=0;i<layouts;++i){if(i)out<<',';auto v=dna->getVertexLayout(mesh,i);out<<'['<<v.position<<','<<v.normal<<','<<v.textureCoordinate<<']';}
        out<<"],\"faces\":[";
        for(std::uint32_t i=0;i<dna->getFaceCount(mesh);++i){if(i)out<<',';out<<'[';bool first=true;for(auto v:dna->getFaceVertexLayoutIndices(mesh,i)){if(v>=layouts)throw std::runtime_error("Invalid face index");if(!first)out<<',';first=false;out<<v;}out<<']';}
        out<<"],\"targets\":[";
        for(std::uint16_t i=0;i<dna->getBlendShapeTargetCount(mesh);++i){if(i)out<<',';const auto name=text(dna->getBlendShapeChannelName(dna->getBlendShapeChannelIndex(mesh,i)));out<<std::quoted(name);}
        out<<"],\"deltas\":[";bool firstDelta=true;
        for(std::uint16_t i=0;i<dna->getBlendShapeTargetCount(mesh);++i) {
            const auto name=text(dna->getBlendShapeChannelName(dna->getBlendShapeChannelIndex(mesh,i)));
            if(target.empty()||name!=target)continue;found=true;
            auto ids=dna->getBlendShapeTargetVertexIndices(mesh,i);
            if(ids.size()!=dna->getBlendShapeTargetDeltaCount(mesh,i))throw std::runtime_error("Delta count mismatch");
            for(std::size_t k=0;k<ids.size();++k){if(ids[k]>=dna->getVertexPositionCount(mesh))throw std::runtime_error("Invalid delta index");if(!firstDelta)out<<',';firstDelta=false;out<<'['<<ids[k]<<',';xyz(out,dna->getBlendShapeTargetDelta(mesh,i,k));out<<']';}
        }
        out<<"]}";
    }
    out<<"],\"target\":"<<std::quoted(target)<<",\"rigLogicEvaluated\":false}";out.close();
    if(!found)throw std::runtime_error("Requested target absent; inspect neutral export targets first");
    std::filesystem::rename(temporary,argv[2]);
    std::cout<<"Decoded "<<total<<" layout vertices at LOD "<<lod<<"\n";
    return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
