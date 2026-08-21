#include "engine/scene/VertexLayout.h"
#include "engine/scene/Mesh.h"

#include <cstddef>
#include <mutex>
#include <unordered_map>

namespace engine {

namespace {

std::vector<VertexLayoutDesc>            g_layouts;
std::unordered_map<std::string, uint32_t> g_nameToId;
std::once_flag                            g_initFlag;

void RegisterBuiltins() {
    g_layouts.resize(static_cast<size_t>(VertexLayoutId::Count));

    // Static: pos / normal / uv，与 Mesh::Vertex CPU 结构对齐
    VertexLayoutDesc s;
    s.name              = "StaticMesh";
    s.binding.binding   = 0;
    s.binding.stride    = static_cast<uint32_t>(sizeof(Vertex));
    s.binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    s.attributes.resize(3);
    s.attributes[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, pos)};
    s.attributes[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)};
    s.attributes[2] = {2, 0, VK_FORMAT_R32G32_SFLOAT,    offsetof(Vertex, texCoord)};

    g_layouts[static_cast<size_t>(VertexLayoutId::Static)] = s;
    g_nameToId[s.name] = static_cast<uint32_t>(VertexLayoutId::Static);
}

void EnsureInitialized() {
    std::call_once(g_initFlag, RegisterBuiltins);
}

} // namespace

void VertexLayoutRegistry::Register(VertexLayoutId id, VertexLayoutDesc desc) {
    EnsureInitialized();
    auto i = static_cast<size_t>(id);
    if (i >= g_layouts.size()) g_layouts.resize(i + 1);
    g_nameToId[desc.name] = static_cast<uint32_t>(id);
    g_layouts[i] = std::move(desc);
}

const VertexLayoutDesc* VertexLayoutRegistry::Get(VertexLayoutId id) {
    EnsureInitialized();
    auto i = static_cast<size_t>(id);
    if (i >= g_layouts.size()) return nullptr;
    const auto& d = g_layouts[i];
    if (d.attributes.empty()) return nullptr;
    return &d;
}

const VertexLayoutDesc* VertexLayoutRegistry::GetByName(const std::string& name) {
    EnsureInitialized();
    auto it = g_nameToId.find(name);
    if (it == g_nameToId.end()) return nullptr;
    return Get(static_cast<VertexLayoutId>(it->second));
}

} // namespace engine
