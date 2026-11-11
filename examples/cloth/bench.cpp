#include "Workload.h"
#include <cstdlib>
int main(int argc,char** argv){try{
    if(argc<4){std::cerr<<"usage: aiverse_cloth_bench objects cells workers [frames=180] [warmup=60] [iterations=4] [share=0] [windGroups=17] [lod=0:off,1:mixed,2:far]\n";return 2;}
    int count=std::stoi(argv[1]),cells=std::stoi(argv[2]),workers=std::stoi(argv[3]);
    int frames=argc>4?std::stoi(argv[4]):180,warmup=argc>5?std::stoi(argv[5]):60,iterations=argc>6?std::stoi(argv[6]):4;
    if(frames<1||frames>100000||warmup<0||warmup>100000)throw std::invalid_argument("frame count");
    bool share=argc>7?std::stoi(argv[7])!=0:false;int windGroups=argc>8?std::stoi(argv[8]):17;
    int lodMode=argc>9?std::stoi(argv[9]):0;
    clothdemo::Workload w(count,cells,workers,iterations,share,windGroups);std::vector<double> times;times.reserve(size_t(frames));
    aiverse::cloth::Vec camera=lodMode==2?aiverse::cloth::Vec{0,100,-100}:aiverse::cloth::Vec{float(w.columns)*2,5,-8};
    double solves=0,deferred=0;
    for(int i=0;i<warmup+frames;++i){auto a=clothdemo::Clock::now();w.step(i,camera,lodMode!=0);auto b=clothdemo::Clock::now();if(i>=warmup){times.push_back(clothdemo::ms(a,b));solves+=w.batch.stats().simulatedObjects;deferred+=w.batch.stats().deferredObjects;}}
    std::cout<<std::setprecision(6)<<"{\"objects\":"<<count<<",\"physical_cells\":"<<cells<<",\"particles_per_object\":"<<(cells+1)*(cells+1)<<",\"render_triangles_per_object\":10082,\"rendered\":false,\"workers\":"<<w.batch.workers()<<",\"substeps\":2,\"iterations\":"<<iterations<<",\"frames\":"<<frames<<",\"warmup\":"<<warmup<<",";
    std::cout<<"\"sharing\":"<<(share?"true":"false")<<",\"wind_groups\":"<<windGroups<<",\"simulated_objects\":"<<w.batch.stats().simulatedObjects<<",\"copied_objects\":"<<w.batch.stats().copiedObjects<<",";
    std::cout<<"\"lod_mode\":"<<lodMode<<",\"mean_solves_per_tick\":"<<solves/frames<<",\"mean_deferred_per_tick\":"<<deferred/frames<<",\"lod_counts\":[";auto counts=w.lod.counts();for(size_t i=0;i<4;++i){if(i)std::cout<<',';std::cout<<counts[i];}std::cout<<"],";
    clothdemo::metric(std::cout,"simulation_ms",times);std::cout<<",\"max_stretch_ratio\":"<<w.stretch()<<",\"healthy\":"<<(w.healthy()?"true":"false")<<",\"samples_ms\":[";for(size_t i=0;i<times.size();++i){if(i)std::cout<<',';std::cout<<times[i];}std::cout<<"]}\n";
    return w.healthy()?0:1;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
