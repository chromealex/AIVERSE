#include "ClothGpu.h"
#include "ClothComputeShaders.h"
#include <algorithm>
#include <cmath>
#include <cstring>
namespace aiverse::cloth {
bool GpuSolver::init(gfx::Device& d,Batch& batch,std::span<Cloth> cloths){
    shutdown();error_.clear();memoryBytes_=0;
    if(batch.hasExecutor()){error_="batch already has an external solver";return false;}
    if(!d.supportsCompute()){error_="compute is unavailable on this RHI backend";return false;}
    if(cloths.empty()||cloths.size()>2048){error_="GPU cloth count must be 1..2048";return false;}
    for(auto& c:cloths)if(c.grid_!=cloths[0].grid_||c.externalState_){error_="GPU cloth requires one shared grid and CPU-owned initial states";return false;}
    auto& grid=cloths[0].grid();ColoredGrid colors(grid);offsets_=colors.offsets;n_=uint32_t(grid.settings.cells+1);
    columns_=uint32_t(std::ceil(std::sqrt(float(cloths.size()))));edgeCount_=uint32_t(colors.edges.size());
    size_t count=cloths.size(),particles=size_t(n_)*n_,total=count*particles;
    memoryBytes_=total*48+size_t(columns_)*((count+columns_-1)/columns_)*particles*16+count*edgeCount_*4+count*(sizeof(Input)+32*sizeof(CapsuleData)+8)*3+edgeCount_*16+count*40;
    if(memoryBytes_>512u*1024u*1024u||columns_*n_>4096){error_="GPU cloth exceeds the 512 MiB/4096 texture budget";return false;}
    device_=&d;
    shader_=d.createCompute({kClothComputeMetal,kClothComputeGLSL,"XPBD colored cloth"});
    if(!shader_.valid()){error_=d.lastError();shutdown();return false;}
    std::vector<render::float4> values(total);
    for(size_t i=0;i<count;++i)for(size_t j=0;j<particles;++j){auto p=cloths[i].p_[j];values[i*particles+j]={p.x,p.y,p.z,cloths[i].weights_[j]};}
    buffers_[0]=d.createBuffer({gfx::BufferType::Storage,gfx::Usage::Immutable,total*16,values.data(),"cloth GPU positions"});
    buffers_[1]=d.createBuffer({gfx::BufferType::Storage,gfx::Usage::Immutable,total*16,nullptr,"cloth GPU previous"});
    for(size_t i=0;i<count;++i)for(size_t j=0;j<particles;++j){auto v=cloths[i].velocity_[j];values[i*particles+j]={v.x,v.y,v.z,0};}
    buffers_[2]=d.createBuffer({gfx::BufferType::Storage,gfx::Usage::Immutable,total*16,values.data(),"cloth GPU velocities"});
    struct PackedEdge {uint32_t a,b;float rest,compliance;};std::vector<PackedEdge> edges;
    for(auto e:colors.edges)edges.push_back({e.a,e.b,e.rest,e.compliance});
    buffers_[3]=d.createBuffer({gfx::BufferType::Storage,gfx::Usage::Immutable,edges.size()*16,edges.data(),"cloth GPU colored edges"});
    buffers_[4]=d.createBuffer({gfx::BufferType::Storage,gfx::Usage::Immutable,count*edgeCount_*4,nullptr,"cloth GPU lambdas"});
    buffers_[5]=d.createBuffer({gfx::BufferType::Vertex,gfx::Usage::Dynamic,count*4,nullptr,"cloth GPU jobs",true});
    buffers_[6]=d.createBuffer({gfx::BufferType::Vertex,gfx::Usage::Dynamic,count*4,nullptr,"cloth GPU sources",true});
    buffers_[7]=d.createBuffer({gfx::BufferType::Vertex,gfx::Usage::Dynamic,count*sizeof(Input),nullptr,"cloth GPU inputs",true});
    buffers_[9]=d.createBuffer({gfx::BufferType::Vertex,gfx::Usage::Dynamic,count*32*sizeof(CapsuleData),nullptr,"cloth GPU capsules",true});
    std::vector<SleepState> sleeping(count);
    buffers_[10]=d.createBuffer({gfx::BufferType::Storage,gfx::Usage::Immutable,count*8,nullptr,"cloth GPU motion flags"});
    buffers_[11]=d.createBuffer({gfx::BufferType::Storage,gfx::Usage::Immutable,count*sizeof(SleepState),sleeping.data(),"cloth GPU sleep states"});
    gfx::TextureDesc td;td.width=int(columns_*n_);td.height=int((count+columns_-1)/columns_*n_);td.format=gfx::PixelFormat::RGBA32F;td.filter=gfx::Filter::Nearest;td.storage=true;td.label="cloth GPU render positions";
    texture_=d.createTexture(td);
    bool valid=texture_.valid();for(size_t i=0;i<buffers_.size();++i)if(i!=8)valid=valid&&buffers_[i].valid();
    if(!valid){error_="GPU cloth allocation failed: "+d.lastError();shutdown();return false;}
    inputs_.resize(count);jobs_.reserve(count);sources_.resize(count);capsules_.reserve(count*32);wasExternal_.assign(count,false);uploaded_.resize(count);scratch_.resize(particles);
    for(size_t i=0;i<count;++i){uploaded_[i]=cloths[i].fingerprint();bool force=cloths[i].forceNext_;cloths[i].wake();cloths[i].forceNext_=force;}
    exported_=false;
    batch_=&batch;cloths_=cloths;healthy_=true;
    batch.beforeStep_=[this]{pollSleep();};
    batch.setExecutor([this](auto c,auto j,auto s,auto dt,const auto& env,auto inputs){return execute(c,j,s,dt,env,inputs);});
    return true;
}
bool GpuSolver::execute(std::span<Cloth> cloths,std::span<const size_t> jobs,std::span<const size_t> sources,std::span<const float> dt,const Environment& env,std::span<const Environment> inputs){
    if(!healthy_||cloths.data()!=cloths_.data()||cloths.size()!=cloths_.size()||dt.size()!=cloths.size()){error_="GPU cloth storage/order changed";healthy_=false;return false;}
    auto& s=cloths[0].grid().settings;size_t particles=size_t(n_)*n_;unsigned maxSub=0,maxIter=0;capsules_.clear();jobs_.clear();dispatches_=0;uploadBytes_=0;
    bool changed=!exported_;
    for(size_t i=0;i<cloths.size();++i)changed=changed||sources_[i]!=sources[i]||(!cloths[i].externalState_&&(wasExternal_[i]||uploaded_[i]!=cloths[i].fingerprint()));
    if(jobs.empty()&&!changed)return true;
    for(auto job:jobs)jobs_.push_back(uint32_t(job));
    for(size_t i=0;i<cloths.size();++i){auto& c=cloths[i];const auto& e=inputs.empty()?env:inputs[i];
        auto finite=[](Vec v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);};
        if(!finite(e.gravity)||!finite(e.wind)||!std::isfinite(e.floor)||!std::isfinite(c.windScale)){error_="non-finite GPU cloth input";healthy_=false;return false;}
        if(c.grid_!=cloths[0].grid_||e.capsules.size()>32){error_="GPU cloth grid/collider limit";healthy_=false;return false;}
        if(!c.externalState_&&(wasExternal_[i]||uploaded_[i]!=c.fingerprint())){
            for(size_t j=0;j<particles;++j){auto p=c.p_[j];scratch_[j]={p.x,p.y,p.z,c.weights_[j]};}
            device_->updateBuffer(buffers_[0],scratch_.data(),particles*16,i*particles*16);
            for(size_t j=0;j<particles;++j){auto v=c.velocity_[j];scratch_[j]={v.x,v.y,v.z,0};}
            device_->updateBuffer(buffers_[2],scratch_.data(),particles*16,i*particles*16);uploaded_[i]=c.fingerprint();uploadBytes_+=particles*32;
        }
        wasExternal_[i]=c.externalState_||dt[i]>0;sources_[i]=uint32_t(sources[i]);
        unsigned ticks=unsigned(std::clamp(int(std::ceil(dt[i]*60-1e-5f)),1,6));unsigned sub=unsigned(s.substeps)*ticks;
        unsigned iterations=unsigned(ticks>1?std::min(s.iterations,std::max(2,s.iterations/2)):s.iterations);
        float h=dt[i]/float(sub);auto force=e.gravity+e.wind*c.windScale;auto pin=c.pinOffset_;
        auto& p=inputs_[i];p.forceH={force.x,force.y,force.z,h};p.pinFloor={pin.x,pin.y,pin.z,e.floor};p.solver={h>0?1/(h*h):0,std::exp(-s.damping*h),h>0?1/h:0,s.thickness};
        p.meta={sub,iterations,uint32_t(capsules_.size()),0};
        auto sleep=c.sleep_;p.sleep={sleep.enabled?sleep.speedThreshold*sleep.speedThreshold:-1,sleep.displacementThreshold*sleep.displacementThreshold,sleep.quietSeconds,dt[i]};
        p.epoch={uint32_t(c.sleepRevision_),uint32_t(c.sleepRevision_>>32),0,0};
        for(auto capsule:e.capsules){auto a=capsule.a,b=capsule.b;float r=capsule.radius;
            if(!std::isfinite(a.x)||!std::isfinite(a.y)||!std::isfinite(a.z)||!std::isfinite(b.x)||!std::isfinite(b.y)||!std::isfinite(b.z)||!std::isfinite(r)||r<=0)continue;
            capsules_.push_back({{a.x,a.y,a.z,r},{b.x,b.y,b.z,0}});++p.meta[3];
        }
    }
    for(auto job:jobs){maxSub=std::max(maxSub,inputs_[job].meta[0]);maxIter=std::max(maxIter,inputs_[job].meta[1]);}
    uploadBytes_+=jobs_.size()*4+sources_.size()*4+inputs_.size()*sizeof(Input)+capsules_.size()*sizeof(CapsuleData);
    if(!jobs_.empty())device_->updateBuffer(buffers_[5],jobs_.data(),jobs_.size()*4);
    device_->updateBuffer(buffers_[6],sources_.data(),sources_.size()*4);
    device_->updateBuffer(buffers_[7],inputs_.data(),inputs_.size()*sizeof(Input));
    if(!capsules_.empty())device_->updateBuffer(buffers_[9],capsules_.data(),capsules_.size()*sizeof(CapsuleData));
    if(!device_->beginCompute(shader_)){healthy_=false;error_="GPU cloth must step within a frame and outside render passes";return false;}
    for(int slot=0;slot<int(buffers_.size());++slot)if(slot!=8)device_->setComputeBuffer(slot,buffers_[size_t(slot)]);
    device_->setComputeTexture(0,texture_);
    struct Uniforms {uint32_t mode,n,edges,jobs,start,count,sub,iter,objects,columns,pad0,pad1;render::float4 physical;};
    Uniforms u{0,n_,edgeCount_,uint32_t(jobs.size()),0,0,0,0,uint32_t(cloths.size()),columns_,0,0,{s.width,s.height,0,0}};
    auto dispatch=[&](unsigned mode,uint32_t x,uint32_t y){u.mode=mode;device_->setComputeUniforms(&u,sizeof(u));device_->dispatch(x,y);++dispatches_;};
    dispatch(5,1,u.objects);
    for(u.sub=0;u.sub<maxSub;++u.sub){dispatch(0,uint32_t(particles),u.jobs);
        for(u.iter=0;u.iter<maxIter;++u.iter){for(size_t color=0;color+1<offsets_.size();++color){u.start=uint32_t(offsets_[color]);u.count=uint32_t(offsets_[color+1]-offsets_[color]);dispatch(1,u.count,u.jobs);}dispatch(2,uint32_t(particles),u.jobs);}
        dispatch(3,uint32_t(particles),u.jobs);
    }
    dispatch(4,uint32_t(particles),u.objects);
    dispatch(6,1,u.objects);dispatch(7,1,u.objects);
    device_->endCompute();exported_=true;
    if(!jobs.empty()&&readbacks_.size()<8){
        auto packet=std::make_shared<Readback>();for(auto& c:cloths)packet->revisions.push_back(c.sleepRevision_);
        if(device_->readBufferAsync(buffers_[11],cloths.size()*sizeof(SleepState),[packet](auto bytes){packet->bytes=std::move(bytes);packet->ready=true;}))readbacks_.push_back(std::move(packet));
    }
    return true;
}
void GpuSolver::pollSleep(){
    for(auto it=readbacks_.begin();it!=readbacks_.end();){auto& packet=**it;
        if(!packet.ready){++it;continue;}
        if(packet.bytes.size()==cloths_.size()*sizeof(SleepState))for(size_t i=0;i<cloths_.size();++i){
            SleepState state;std::memcpy(&state,packet.bytes.data()+i*sizeof(state),sizeof(state));auto& c=cloths_[i];
            uint64_t epoch=uint64_t(state.epochLo)|(uint64_t(state.epochHi)<<32);
            if(state.sleeping&&epoch==c.sleepRevision_&&packet.revisions[i]==epoch){c.sleeping_=true;c.forceNext_=false;}
        }
        it=readbacks_.erase(it);
    }
}
bool GpuSolver::download(){
    if(!device_||!batch_)return false;
    size_t particles=size_t(n_)*n_;
    std::vector<render::float4> positions(cloths_.size()*particles),velocities(positions.size());
    if(!device_->readBuffer(buffers_[0],positions.data(),positions.size()*16)||!device_->readBuffer(buffers_[2],velocities.data(),velocities.size()*16))return false;
    for(size_t i=0;i<cloths_.size();++i){auto& c=cloths_[i];for(size_t j=0;j<particles;++j){auto p=positions[i*particles+j],v=velocities[i*particles+j];c.p_[j]={p.x,p.y,p.z};c.velocity_[j]={v.x,v.y,v.z};}
        c.wake();c.externalState_=false;c.fingerprintValid_=false;c.proofOwner_.reset();
    }
    return true;
}
void GpuSolver::shutdown(){
    if(batch_){batch_->beforeStep_={};batch_->setExecutor({});for(auto& c:cloths_)if(c.externalState_)c.reset();}
    batch_=nullptr;cloths_={};readbacks_.clear();exported_=false;
    if(device_){device_->destroyCompute(shader_);device_->destroyTexture(texture_);for(auto b:buffers_)if(b.valid())device_->destroyBuffer(b);}
    device_=nullptr;shader_={};texture_={};buffers_={};healthy_=false;
}
}
