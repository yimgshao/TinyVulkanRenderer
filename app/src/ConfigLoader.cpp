#include "app/ConfigLoader.h"

#include "engine/shader/ShaderAssetLoader.h"
#include "engine/shader/ShaderAssetManager.h"

#include "json.hpp" // nlohmann/json（third_party/tinygltf 自带）

#include <filesystem>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <stdexcept>

namespace app {

namespace {

std::filesystem::path configRoot() {
    // <root>/app/src/ConfigLoader.cpp → 上三级为项目根
    std::filesystem::path srcFile = __FILE__;
    return srcFile.parent_path().parent_path().parent_path() / "configs";
}

engine::Config jsonToConfig(const nlohmann::json& j, const std::string& ctx) {
    engine::Config cfg;
    for (const auto& [key, val] : j.items()) {
        if (val.is_object()) {
            cfg.setSection(key, jsonToConfig(val, ctx + "." + key));
        } else if (val.is_boolean()) {
            cfg.set(key, val.get<bool>());
        } else if (val.is_number()) {
            cfg.set(key, val.get<double>());
        } else if (val.is_string()) {
            cfg.set(key, val.get<std::string>());
        } else if (val.is_array()) {
            engine::Config::StringList values;
            bool allStrings = true;
            for (const auto& item : val) {
                if (!item.is_string()) {
                    allStrings = false;
                    break;
                }
                values.push_back(item.get<std::string>());
            }
            if (allStrings) {
                cfg.set(key, values);
            } else {
                std::cerr << "[Config] skip non-string array at '" << ctx
                          << "." << key << "'.\n";
            }
        } else {
            std::cerr << "[Config] skip unsupported value at '" << ctx
                      << "." << key << "'.\n";
        }
    }
    return cfg;
}

// 解析一个 JSON 文件为 Config；失败返回 false 并给空 Config。
bool parseJsonFile(const std::filesystem::path& path, engine::Config& out,
                   const std::string& ctx) {
    std::ifstream ifs(path);
    if (!ifs) {
        std::cerr << "[Config] cannot open " << path << "\n";
        return false;
    }
    try {
        // ignore_comments：容忍配置文件中的 // 注释
        nlohmann::json j = nlohmann::json::parse(ifs, nullptr, true, true);
        if (!j.is_object()) {
            std::cerr << "[Config] root of " << path << " is not an object.\n";
            return false;
        }
        out = jsonToConfig(j, ctx);
        return true;
    } catch (const nlohmann::json::exception& e) {
        std::cerr << "[Config] parse error in " << path << ": " << e.what() << "\n";
        return false;
    }
}

// 相对路径基于 baseDir 绝对化；绝对路径原样返回。
std::string resolvePath(const std::filesystem::path& baseDir,
                        const std::string& p) {
    std::filesystem::path path(p);
    if (path.is_relative()) path = baseDir / path;
    return path.lexically_normal().string();
}

} // anonymous namespace

void ConfigLoader::addCustomShaderSearchPath(std::filesystem::path path) {
    if (path.empty()) throw std::invalid_argument("Custom shader search path must not be empty");
    if (path.is_relative()) path = std::filesystem::absolute(path);
    path = path.lexically_normal();
    if (std::find(customShaderSearchPaths_.begin(), customShaderSearchPaths_.end(), path) ==
        customShaderSearchPaths_.end()) {
        customShaderSearchPaths_.push_back(std::move(path));
    }
}

void ConfigLoader::registerShaderAssets(engine::ShaderAssetManager& manager) const {
    engine::ShaderAssetLoader loader(customShaderSearchPaths_);
    for (const auto& asset : loader.discoverCustomAssets())
        manager.registerAsset(loader.load(asset));
}

engine::Config ConfigLoader::load(const std::string& configFile) {
    engine::Config root;

    std::filesystem::path entry(configFile);
    if (entry.is_relative()) entry = configRoot() / entry;

    if (!parseJsonFile(entry, root, "main")) {
        std::cerr << "[Config] fall back to built-in defaults.\n";
        return engine::Config{};
    }

    const std::filesystem::path baseDir = entry.parent_path();

    // renderer：字符串 → 子配置文件；已是 section（内联对象）则跳过
    for (const char* key : {"renderer"}) {
        auto it = root.values().find(key);
        if (it == root.values().end()) continue;
        const std::string* v = std::get_if<std::string>(&it->second);
        if (!v) continue;

        engine::Config sub;
        if (!parseJsonFile(resolvePath(baseDir, *v), sub, key)) {
            std::cerr << "[Config] ignore '" << key << "' sub-config.\n";
            continue;
        }
        root.setSection(key, std::move(sub));
    }

    // scene：字符串值相对入口文件目录绝对化
    if (auto it = root.values().find("scene"); it != root.values().end()) {
        if (const std::string* v = std::get_if<std::string>(&it->second)) {
            root.set("scene", resolvePath(baseDir, *v));
        }
    }

    // ibl.path：字符串值相对入口文件目录绝对化
    const std::string iblPath = root.getString("ibl.path", "");
    if (!iblPath.empty()) {
        root.set("ibl.path", resolvePath(baseDir, iblPath));
    }

    return root;
}

} // namespace app
