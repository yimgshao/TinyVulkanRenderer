file(GLOB_RECURSE GUI_SOURCES CONFIGURE_DEPENDS "src/*.cpp")
file(GLOB_RECURSE GUI_HEADERS CONFIGURE_DEPENDS "include/*.h")

add_library(app STATIC ${GUI_SOURCES} ${GUI_HEADERS})

target_include_directories(app PUBLIC include)

# nlohmann/json（third_party/tinygltf/json.hpp，header-only）供 ConfigLoader 使用
target_include_directories(app PRIVATE "${CMAKE_SOURCE_DIR}/third_party/tinygltf")

target_link_libraries(app
    PUBLIC
        engine
        imgui
    PRIVATE
        glfw
)
