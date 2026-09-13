#include "engine/shader/ShaderAssetLoader.h"
#include "json.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>

namespace engine {
namespace {
using Json = nlohmann::json;
namespace fs = std::filesystem;

void keys(const Json& object, std::initializer_list<const char*> allowed) {
    if (!object.is_object()) throw std::runtime_error("expected object");
    for (auto it = object.begin(); it != object.end(); ++it)
        if (std::find(allowed.begin(), allowed.end(), it.key()) == allowed.end())
            throw std::runtime_error("unknown field: " + it.key());
}

template<class T> T choice(const std::string& value, std::initializer_list<std::pair<const char*, T>> choices) {
    for (const auto& [name, result] : choices) if (value == name) return result;
    throw std::runtime_error("invalid enum value: " + value);
}

fs::path resolve(const fs::path& file, const std::vector<fs::path>& roots) {
    if (file.is_absolute()) {
        if (fs::is_regular_file(file)) return fs::weakly_canonical(file);
    } else {
        for (const auto& root : roots)
            if (fs::is_regular_file(root / file)) return fs::weakly_canonical(root / file);
    }
    throw std::runtime_error("file not found: " + file.string());
}
}

ShaderAssetLoader::ShaderAssetLoader(std::vector<fs::path> customShaderRoots)
    : customAssetRoots(std::move(customShaderRoots)), assetRoots(customAssetRoots),
      sourceRoots(customAssetRoots) {
#ifdef ENGINE_DEFAULT_SHADER_DIR
    const fs::path engineShaderRoot = ENGINE_DEFAULT_SHADER_DIR;
#else
    const fs::path engineShaderRoot = "engine/shaders";
#endif
    engineAssetRoot = engineShaderRoot / "assets";
    assetRoots.push_back(engineAssetRoot);
    sourceRoots.push_back(engineShaderRoot);
}

std::vector<fs::path> ShaderAssetLoader::discoverAssets(
    const std::vector<fs::path>& roots) {
    std::vector<fs::path> discovered;
    std::set<fs::path> uniquePaths;
    for (const auto& root : roots) {
        if (!fs::exists(root))
            throw std::runtime_error("Shader search path does not exist: " + root.string());
        if (!fs::is_directory(root))
            throw std::runtime_error("Shader search path is not a directory: " + root.string());

        std::vector<fs::path> rootAssets;
        for (const auto& entry : fs::recursive_directory_iterator(root)) {
            if (!entry.is_regular_file()) continue;
            const auto filename = entry.path().filename().string();
            if (!filename.ends_with(".shader.json")) continue;
            rootAssets.push_back(fs::weakly_canonical(entry.path()));
        }
        std::sort(rootAssets.begin(), rootAssets.end());
        for (auto& asset : rootAssets)
            if (uniquePaths.insert(asset).second) discovered.push_back(std::move(asset));
    }
    return discovered;
}

std::vector<fs::path> ShaderAssetLoader::discoverCustomAssets() const {
    return discoverAssets(customAssetRoots);
}

std::vector<fs::path> ShaderAssetLoader::discoverEngineAssets() const {
    return discoverAssets({engineAssetRoot});
}

ShaderAsset ShaderAssetLoader::load(const fs::path& requested) const {
    const auto source = resolve(requested, assetRoots);
    try {
        std::ifstream stream(source);
        const Json root = Json::parse(stream, nullptr, true, true);
        keys(root, {"name", "properties", "keywords", "passes"});
        for (const char* field : {"properties", "keywords", "passes"})
            if (root.contains(field) && !root.at(field).is_array())
                throw std::runtime_error(std::string(field) + " must be an array");
        ShaderAsset asset;
        asset.sourcePath = source;
        asset.name = root.at("name").get<std::string>();
        for (const auto& item : root.value("properties", Json::array())) {
            keys(item, {"name", "type", "default"});
            ShaderProperty property;
            property.name = item.at("name").get<std::string>();
            using T = ShaderPropertyType;
            property.type = choice<T>(item.at("type").get<std::string>(), {
                {"bool", T::Bool}, {"int", T::Int}, {"float", T::Float},
                {"float2", T::Float2}, {"float3", T::Float3}, {"float4", T::Float4},
                {"color", T::Float4}, {"matrix4x4", T::Matrix4},
                {"texture2D", T::Texture2D}, {"texture3D", T::Texture3D}, {"textureCube", T::TextureCube}});
            if (property.type >= T::Texture2D) {
                property.defaultTexture = item.value("default", std::string("white"));
            } else if (item.contains("default")) {
                const auto& value = item.at("default");
                size_t count = property.type == T::Float2 ? 2 : property.type == T::Float3 ? 3
                    : property.type == T::Float4 ? 4 : property.type == T::Matrix4 ? 16 : 1;
                if (property.type == T::Bool) {
                    if (!value.is_boolean()) throw std::runtime_error("bool default required: " + property.name);
                    property.defaultInteger = value.get<bool>() ? 1 : 0;
                } else if (property.type == T::Int) {
                    if (!value.is_number_integer() || value.get<double>() < -2147483648.0 || value.get<double>() > 2147483647.0)
                        throw std::runtime_error("int32 default required: " + property.name);
                    property.defaultInteger = value.get<int32_t>();
                } else if (count == 1) property.defaultValue[0] = value.get<float>();
                else {
                    if (!value.is_array() || value.size() != count)
                        throw std::runtime_error("wrong default element count for " + property.name);
                    for (size_t i = 0; i < count; ++i) property.defaultValue[i] = value.at(i).get<float>();
                }
            }
            asset.properties.push_back(std::move(property));
        }
        for (const auto& item : root.value("keywords", Json::array())) {
            keys(item, {"name", "type", "default"});
            if (item.value("type", std::string("boolean")) != "boolean")
                throw std::runtime_error("only boolean keywords are supported");
            auto name = item.at("name").get<std::string>();
            asset.keywordNames.push_back(name);
            asset.defaultKeywords.set(name, item.value("default", false));
        }
        auto moduleRoots = sourceRoots;
        moduleRoots.insert(moduleRoots.begin(), source.parent_path());
        for (const auto& item : root.at("passes")) {
            keys(item, {"name", "lightMode", "module", "stages", "vertexLayout", "state", "keywords"});
            ShaderPass pass;
            pass.name = item.at("name").get<std::string>();
            pass.lightMode = item.at("lightMode").get<std::string>();
            auto module = fs::path(item.at("module").get<std::string>());
            if (module.extension() != ".hlsl") module += ".hlsl";
            auto resolvedModule = resolve(module, moduleRoots);
            resolvedModule.replace_extension();
            pass.shader.moduleName = resolvedModule.generic_string();
            pass.vertexLayout = item.value("vertexLayout", std::string("StaticMesh"));
            pass.shader.genericValueParams = item.value("keywords", asset.keywordNames);
            for (const auto& keyword : pass.shader.genericValueParams)
                if (std::find(asset.keywordNames.begin(), asset.keywordNames.end(), keyword) == asset.keywordNames.end())
                    throw std::runtime_error("undeclared keyword: " + keyword);
            if (item.contains("stages")) {
                if (!item.at("stages").is_array()) throw std::runtime_error("stages must be an array");
                pass.shader.stages.clear();
                for (const auto& stage : item.at("stages"))
                    pass.shader.stages.push_back(choice<ShaderStage>(stage.get<std::string>(), {
                        {"vertex", ShaderStage::Vertex}, {"fragment", ShaderStage::Fragment}}));
            }
            if (item.contains("state")) {
                const auto& state = item.at("state");
                keys(state, {"cull", "depthTest", "depthWrite", "depthBias"});
                pass.state.cullMode = choice<VkCullModeFlags>(state.value("cull", std::string("back")), {
                    {"back", VK_CULL_MODE_BACK_BIT}, {"front", VK_CULL_MODE_FRONT_BIT}, {"none", VK_CULL_MODE_NONE}});
                const auto depth = state.value("depthTest", std::string("less"));
                pass.state.depthTestEnable = depth != "off";
                pass.state.depthCompareOp = choice<VkCompareOp>(depth, {
                    {"off", VK_COMPARE_OP_ALWAYS}, {"always", VK_COMPARE_OP_ALWAYS}, {"never", VK_COMPARE_OP_NEVER},
                    {"less", VK_COMPARE_OP_LESS}, {"lessEqual", VK_COMPARE_OP_LESS_OR_EQUAL}, {"equal", VK_COMPARE_OP_EQUAL},
                    {"greater", VK_COMPARE_OP_GREATER}, {"greaterEqual", VK_COMPARE_OP_GREATER_OR_EQUAL}, {"notEqual", VK_COMPARE_OP_NOT_EQUAL}});
                pass.state.depthWriteEnable = state.value("depthWrite", true);
                pass.state.depthBiasEnable = state.value("depthBias", false);
            }
            asset.passes.push_back(std::move(pass));
        }
        asset.validate();
        std::cout << "[ShaderAsset] " << asset.name << " <- " << source.string() << '\n';
        return asset;
    } catch (const std::exception& error) {
        throw std::runtime_error("ShaderAsset " + source.string() + ": " + error.what());
    }
}
} // namespace engine
