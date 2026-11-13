#pragma once
#include "../../platform/Platform.h"
#import <Metal/Metal.h>
#import <Foundation/Foundation.h>
#include <CommonCrypto/CommonDigest.h>
#include <cstdlib>
#include <atomic>
#include <memory>
#include <cstdio>
#include <map>
#include <string>

namespace aiverse::gfx {
// Build-time capture and packaged Metal IR / GPU binaries. No render-thread pool mutation here.
class MetalShaderAssets {
    id<MTLDevice> device_;
    id<MTLBinaryArchive> archive_;
    std::map<std::string,id<MTLLibrary>> libraries_;
    std::string capture_, archiveOutput_;
    bool enabled_=true, strict_=false;
    size_t hits_=0, misses_=0, pipelines_=0;
    std::shared_ptr<std::atomic<size_t>> failures_=std::make_shared<std::atomic<size_t>>(0);
    static NSString* ns(const std::string& s) { return [NSString stringWithUTF8String:s.c_str()]; }
public:
    [[noreturn]] static void requiredFailure(const char* message) {
        std::fprintf(stderr,"[metal-assets] required artifact failed: %s\n",message);
        std::fflush(stderr);
        std::_Exit(3); // build verification only: never render a partly invalid pipeline set
    }
    static std::string digest(const std::string& bytes) {
        unsigned char hash[CC_SHA256_DIGEST_LENGTH];
        CC_SHA256(bytes.data(),CC_LONG(bytes.size()),hash);
        std::string key; key.reserve(64);
        for(auto c:hash){key+="0123456789abcdef"[c>>4];key+="0123456789abcdef"[c&15];}
        return key;
    }
    explicit MetalShaderAssets(id<MTLDevice> device):device_(device) {
        if(auto* p=std::getenv("AIVERSE_METAL_SOURCE_CAPTURE"))capture_=p;
        if(auto* p=std::getenv("AIVERSE_METAL_ARCHIVE_CAPTURE"))archiveOutput_=p;
        if(auto* p=std::getenv("AIVERSE_METAL_PRECOMPILED"))enabled_=std::string(p)!="off";
        strict_=std::getenv("AIVERSE_METAL_REQUIRE_PRECOMPILED")!=nullptr;
        if(@available(macOS 11.0,iOS 14.0,*)) {
            MTLBinaryArchiveDescriptor* desc=[MTLBinaryArchiveDescriptor new];
            bool load=!archiveOutput_.empty();
            if(!load && enabled_) {
                std::string bytes;
                if(platform::readContentFile("engine/metal/pipelines.metallib",bytes)) {
                    // Archive API requires a native URL; content may come from a resource pack.
                    NSString* path=[NSTemporaryDirectory() stringByAppendingPathComponent:ns("aiverse-metal-"+digest(bytes)+".metallib")];
                    if(![[NSFileManager defaultManager] fileExistsAtPath:path])
                        [[NSData dataWithBytes:bytes.data() length:bytes.size()] writeToFile:path atomically:YES];
                    desc.url=[NSURL fileURLWithPath:path];load=true;
                }
            }
            if(load) {
                NSError* error=nil;archive_=[device_ newBinaryArchiveWithDescriptor:desc error:&error];
                if(!archive_)std::fprintf(stderr,"[metal-assets] archive unavailable: %s\n",error.localizedDescription.UTF8String);
            }
        }
        if(strict_ && !archive_) requiredFailure("pipeline archive missing or incompatible");
    }
    ~MetalShaderAssets() {
        if(@available(macOS 11.0,iOS 14.0,*)) if(archive_ && !archiveOutput_.empty()) {
            NSError* error=nil;
            if(![archive_ serializeToURL:[NSURL fileURLWithPath:ns(archiveOutput_)] error:&error])
                std::fprintf(stderr,"[metal-assets] archive export failed: %s\n",error.localizedDescription.UTF8String);
        }
        std::fprintf(stderr,"[metal-assets] libraries=%zu sourceMisses=%zu pipelines=%zu pipelineFailures=%zu archive=%d strict=%d\n",hits_,misses_,pipelines_,failures_->load(),archive_?1:0,strict_?1:0);
    }
    auto failureCounter() const { return failures_; }
    bool strict() const { return strict_; }
    MTLPipelineOption options() const { return strict_ && archiveOutput_.empty() ? MTLPipelineOptionFailOnBinaryArchiveMiss : MTLPipelineOptionNone; }
    id<MTLLibrary> library(const std::string& source) {
        const auto key=digest(source);
        if(!capture_.empty()) {
            const auto path=capture_+"/"+key+".metal";
            [[NSData dataWithBytes:source.data() length:source.size()] writeToFile:ns(path) atomically:YES];
        }
        auto found=libraries_.find(key);if(found!=libraries_.end())return found->second;
        id<MTLLibrary> library=nil;
        std::string bytes;
        if(enabled_ && platform::readContentFile("engine/metal/"+key+".metallib",bytes)) {
            // DEFAULT copies the bytes; the content reader's buffer can be released afterwards.
            dispatch_data_t data=dispatch_data_create(bytes.data(),bytes.size(),nullptr,DISPATCH_DATA_DESTRUCTOR_DEFAULT);
            NSError* error=nil;library=[device_ newLibraryWithData:data error:&error];
            if(!library)std::fprintf(stderr,"[metal-assets] invalid library %s: %s\n",key.c_str(),error.localizedDescription.UTF8String);
        }
        if(library)++hits_;
        else {++misses_;if(strict_)std::fprintf(stderr,"[metal-assets] required library missing: %s\n",key.c_str());}
        if(!library && strict_)requiredFailure("shader library missing or invalid");
        libraries_[key]=library;return library;
    }
    void prepare(MTLRenderPipelineDescriptor* desc) {
        ++pipelines_;
        if(@available(macOS 11.0,iOS 14.0,*)) if(archive_) {
            if(!archiveOutput_.empty()) {
                NSError* error=nil;
                if(![archive_ addRenderPipelineFunctionsWithDescriptor:desc error:&error])
                    std::fprintf(stderr,"[metal-assets] archive capture failed: %s\n",error.localizedDescription.UTF8String);
            }
            desc.binaryArchives=@[archive_];
        }
    }
};
}
