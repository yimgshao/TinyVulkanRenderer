#include "app/ConfigLoader.h"

#include "json.hpp" // nlohmann/json（third_party/tinygltf 自带）

#include <filesystem>
#include <fstream>
#include <iostream>

namespace app {

namespace {

std::filesystem::path projectRoot() {
    // <root>/app/src/ConfigLoader.cpp → 上三级为项目根
    std::filesystem::path srcFile = __FILE__;
    return srcFile.parent_path().parent_path().parent_path();
}

bool endsWith(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
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
        } else {
            std::cerr << "[Config] skip unsupported value at '" << ctx
                      << "." << key << "' (array/null not supported).\n";
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

engine::Config ConfigLoader::load(const std::string& mainJsonPath) {
    engine::Config root;

    std::filesystem::path entry(mainJsonPath);
    if (entry.is_relative()) entry = projectRoot() / entry;

    if (!parseJsonFile(entry, root, "main")) {
        std::cerr << "[Config] fall back to built-in defaults.\n";
        return engine::Config{};
    }

    const std::filesystem::path baseDir = entry.parent_path();

    // renderer / material：字符串 → 子配置文件或内联 type；已是 section（内联对象）则跳过
    for (const char* key : {"renderer", "material"}) {
        auto it = root.values().find(key);
        if (it == root.values().end()) continue;
        const std::string* v = std::get_if<std::string>(&it->second);
        if (!v) continue;

        engine::Config sub;
        if (endsWith(*v, ".json")) {
            if (!parseJsonFile(resolvePath(baseDir, *v), sub, key)) {
                std::cerr << "[Config] ignore '" << key << "' sub-config.\n";
                continue;
            }
        } else {
            sub.set("type", *v);
        }
        root.setSection(key, std::move(sub));
    }

    // scene / ibl：字符串值相对入口文件目录绝对化
    for (const char* key : {"scene", "ibl"}) {
        auto it = root.values().find(key);
        if (it == root.values().end()) continue;
        const std::string* v = std::get_if<std::string>(&it->second);
        if (!v) continue;
        root.set(key, resolvePath(baseDir, *v));
    }

    return root;
}

} // namespace app
