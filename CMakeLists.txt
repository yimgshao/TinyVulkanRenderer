cmake_minimum_required(VERSION 3.22)
project(TinyVulkanRenderer)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 让所有 add_subdirectory(...) 进来的 target（tinyobjloader / ...）也用
# /utf-8 处理源码编码 + /wd4819 静默无法转换为本地代码页的字符警告。
# 必须在第一个 add_subdirectory 之前调用，否则只对之后的 target 生效。
if(MSVC)
    add_compile_options(/utf-8 /wd4819)
endif()

# Find Vulkan SDK
find_package(Vulkan REQUIRED)
get_filename_component(VULKAN_SDK_DIR "${Vulkan_INCLUDE_DIR}" DIRECTORY)

# GLFW
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/glfw)

# tinyobjloader
add_subdirectory(third_party/tinyobjloader)

# Header-only libraries as INTERFACE targets
add_library(glm INTERFACE)
target_include_directories(glm INTERFACE third_party/glm)

add_library(stb INTERFACE)
target_include_directories(stb INTERFACE third_party/stb)

# Vulkan Memory Allocator
add_subdirectory(third_party/VulkanMemoryAllocator)

# Dear ImGui
add_library(imgui STATIC
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_demo.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_widgets.cpp
    third_party/imgui/backends/imgui_impl_glfw.cpp
    third_party/imgui/backends/imgui_impl_vulkan.cpp
)
target_include_directories(imgui
    PUBLIC
        third_party/imgui
        third_party/imgui/backends
)
target_link_libraries(imgui
    PUBLIC
        glfw
        Vulkan::Vulkan
)

# Submodules
add_subdirectory(engine)
add_subdirectory(app)

# Main executable
add_executable(${PROJECT_NAME} src/main.cpp)
target_link_libraries(${PROJECT_NAME} PRIVATE app)

# Copy runtime shader-compiler DLLs (DXC + spirv-cross C API) from Vulkan SDK
if(WIN32)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${VULKAN_SDK_DIR}/Bin/dxcompiler.dll"
        "${VULKAN_SDK_DIR}/Bin/spirv-cross-c-shared.dll"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
        COMMENT "Copying DXC / spirv-cross DLLs to output directory..."
    )
endif()