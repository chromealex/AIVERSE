#ifndef AIVERSE_RENDER_SDK_H
#define AIVERSE_RENDER_SDK_H
#include <stddef.h>
#include <stdint.h>
#ifdef _WIN32
#ifdef AIVERSE_RENDER_EXPORTS
#define AIVERSE_RENDER_API __declspec(dllexport)
#else
#define AIVERSE_RENDER_API __declspec(dllimport)
#endif
#else
#define AIVERSE_RENDER_API __attribute__((visibility("default")))
#endif
#ifdef __cplusplus
extern "C" {
#endif
typedef struct aiverse_render_model aiverse_render_model;
AIVERSE_RENDER_API uint32_t aiverse_render_abi_version(void);
// Error message for the last failed call on this thread.
AIVERSE_RENDER_API const char* aiverse_render_last_error(void);
// Path is UTF-8. On failure returns NULL. Release the immutable model after all calls finish.
AIVERSE_RENDER_API aiverse_render_model* aiverse_render_open(const char* path);
AIVERSE_RENDER_API void aiverse_render_close(aiverse_render_model* model);
// Returns 0 on success. Writes width*height*4 bytes of top-down RGBA8, only on success.
AIVERSE_RENDER_API int aiverse_render_rgba8(const aiverse_render_model* model,const char* backend,
    int width,int height,float yaw,float pitch,uint8_t* pixels,size_t capacity);
#ifdef __cplusplus
}
#endif
#endif
