# glslang 16.6.0 (KhronosGroup/glslang e1b562a8bed273a02f30b59b66a5d499793cede5), GLSL front end + SPIR-V
# generator only: no HLSL, optional SPIRV-Tools optimizer (third_party/spirv-tools), no C interface, no standalone tool. Used by the Vulkan
# RHI backend (engine/gfx/vulkan) to compile the engine's GLSL at run time. Licenses: LICENSE.txt.
# glslang's own CMake is not used: it generates build_info.h with Python; the generated header is in generated/.
set(_g ${CMAKE_CURRENT_SOURCE_DIR})
set(_mi ${_g}/glslang/MachineIndependent)
add_library(aiverse_glslang STATIC
    ${_g}/glslang/GenericCodeGen/CodeGen.cpp ${_g}/glslang/GenericCodeGen/Link.cpp
    ${_mi}/glslang_tab.cpp ${_mi}/attribute.cpp ${_mi}/Constant.cpp ${_mi}/iomapper.cpp ${_mi}/InfoSink.cpp
    ${_mi}/Initialize.cpp ${_mi}/IntermTraverse.cpp ${_mi}/Intermediate.cpp ${_mi}/ParseContextBase.cpp
    ${_mi}/ParseHelper.cpp ${_mi}/PoolAlloc.cpp ${_mi}/RemoveTree.cpp ${_mi}/Scan.cpp ${_mi}/ShaderLang.cpp
    ${_mi}/SpirvIntrinsics.cpp ${_mi}/SymbolTable.cpp ${_mi}/Versions.cpp ${_mi}/intermOut.cpp ${_mi}/limits.cpp
    ${_mi}/linkValidate.cpp ${_mi}/parseConst.cpp ${_mi}/reflection.cpp ${_mi}/propagateNoContraction.cpp
    ${_mi}/preprocessor/Pp.cpp ${_mi}/preprocessor/PpAtom.cpp ${_mi}/preprocessor/PpContext.cpp
    ${_mi}/preprocessor/PpScanner.cpp ${_mi}/preprocessor/PpTokens.cpp
    ${_g}/glslang/ResourceLimits/ResourceLimits.cpp
    ${_g}/SPIRV/GlslangToSpv.cpp ${_g}/SPIRV/InReadableOrder.cpp ${_g}/SPIRV/Logger.cpp ${_g}/SPIRV/SpvBuilder.cpp
    ${_g}/SPIRV/SpvPostProcess.cpp ${_g}/SPIRV/doc.cpp ${_g}/SPIRV/SpvTools.cpp ${_g}/SPIRV/disassemble.cpp)
if(WIN32)
    target_sources(aiverse_glslang PRIVATE ${_g}/glslang/OSDependent/Windows/ossource.cpp)
else()
    target_sources(aiverse_glslang PRIVATE ${_g}/glslang/OSDependent/Unix/ossource.cpp)
    find_package(Threads REQUIRED)
    target_link_libraries(aiverse_glslang PRIVATE Threads::Threads)
endif()
target_include_directories(aiverse_glslang PUBLIC ${_g} ${_g}/generated)
# With the vendored SPIRV-Tools (AIVERSE_SPIRV_OPT) GlslangToSpv can run the optimizer (SpvOptions).
if(TARGET SPIRV-Tools-opt)
    target_compile_definitions(aiverse_glslang PUBLIC ENABLE_OPT=1)
    target_link_libraries(aiverse_glslang PUBLIC SPIRV-Tools-opt)
else()
    target_compile_definitions(aiverse_glslang PUBLIC ENABLE_OPT=0)
endif()
target_compile_features(aiverse_glslang PUBLIC cxx_std_17)
if(MSVC)
    target_compile_options(aiverse_glslang PRIVATE /w)
else()
    target_compile_options(aiverse_glslang PRIVATE -w)
endif()
