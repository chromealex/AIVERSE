#include "aiverse_render.h"
#include <jni.h>
#include <string>
#include <exception>
namespace {
void fail(JNIEnv* env,const char* message) {
    if(env->ExceptionCheck())return;
    auto cls=env->FindClass("java/lang/IllegalArgumentException");
    if(cls)env->ThrowNew(cls,message);
}
}
extern "C" JNIEXPORT jlong JNICALL Java_org_aiverse_render_RenderModel_openNative(JNIEnv* env,jclass,jbyteArray input) {
    if(aiverse_render_abi_version()!=1 || !input){fail(env,"Invalid renderer ABI or path");return 0;}
    try {
        std::string path(env->GetArrayLength(input),'\0');
        env->GetByteArrayRegion(input,0,jsize(path.size()),reinterpret_cast<jbyte*>(path.data()));
        if(env->ExceptionCheck())return 0;
        if(path.find('\0')!=std::string::npos){fail(env,"Invalid model path");return 0;}
        auto* model=aiverse_render_open(path.c_str());
        if(!model)fail(env,aiverse_render_last_error());
        return reinterpret_cast<jlong>(model);
    } catch(const std::exception& e){fail(env,e.what());return 0;}
}
extern "C" JNIEXPORT void JNICALL Java_org_aiverse_render_RenderModel_closeNative(JNIEnv*,jclass,jlong model) {
    aiverse_render_close(reinterpret_cast<aiverse_render_model*>(model));
}
extern "C" JNIEXPORT jbyteArray JNICALL Java_org_aiverse_render_RenderModel_renderNative(
    JNIEnv* env,jclass,jlong model,jstring backend,jint w,jint h,jfloat yaw,jfloat pitch) {
    if(!model || !backend || w<1 || h<1 || w>4096 || h>4096){fail(env,"Invalid model or dimensions");return nullptr;}
    const jsize size=w*h*4;
    auto result=env->NewByteArray(size);
    if(!result)return nullptr;
    const char* name=env->GetStringUTFChars(backend,nullptr);
    if(!name)return nullptr;
    auto* bytes=env->GetByteArrayElements(result,nullptr);
    if(!bytes){env->ReleaseStringUTFChars(backend,name);return nullptr;}
    const int status=aiverse_render_rgba8(reinterpret_cast<aiverse_render_model*>(model),name,w,h,yaw,pitch,
                                          reinterpret_cast<uint8_t*>(bytes),size_t(size));
    env->ReleaseByteArrayElements(result,bytes,status?JNI_ABORT:0);
    env->ReleaseStringUTFChars(backend,name);
    if(status){fail(env,aiverse_render_last_error());return nullptr;}
    return result;
}
