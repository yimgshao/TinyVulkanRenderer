file(GLOB_RECURSE ENGINE_SOURCES CONFIGURE_DEPENDS "src/*.cpp")
file(GLOB_RECURSE ENGINE_HEADERS CONFIGURE_DEPENDS "include/*.h")

add_library(engine STATIC ${ENGINE_SOURCES} ${ENGINE_HEADERS})

set(VULKAN_SDK_DIR $ENV{VULKAN_SDK})

target_include_directories(engine
    PUBLIC
        include
    PRIVATE
        "${VULKAN_SDK_DIR}/Include")

target_link_libraries(engine
    PUBLIC
        Vulkan::Vulkan
        glm
    PRIVATE
        stb
        tinyobjloader
        "${VULKAN_SDK_DIR}/Lib/dxcompiler.lib"
        "${VULKAN_SDK_DIR}/Lib/spirv-cross-c-shared.lib"
    PUBLIC
        GPUOpen::VulkanMemoryAllocator
)

target_include_directories(engine PRIVATE "${CMAKE_SOURCE_DIR}/third_party/tinygltf")

# ShaderVariantManager 使用 C++20 特性
target_compile_features(engine PUBLIC cxx_std_20)

target_compile_definitions(engine PUBLIC
    ENGINE_DEFAULT_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/shaders"
)

# tinyobjloader 内嵌的 fast_float 在新版 MSVC + C++20 下有 constexpr 兼容性问题。
# Mesh.cpp 单独使用 C++17 标准编译，避免触发 fast_float 的 constexpr 路径。
if(MSVC)
    set_source_files_properties(src/scene/Mesh.cpp PROPERTIES COMPILE_FLAGS "/std:c++17")
endif()
