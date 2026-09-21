#pragma once

#include "engine/config/Config.h"

#include <filesystem>
#include <string>
#include <vector>

namespace engine { class ShaderAssetManager; }

namespace app {

/**
 * ConfigLoader -- 管理 app 层配置输入。
 *
 * JSON 配置和 custom Shader 搜索目录属于宿主策略；
 * 引擎只消费 Config、规范化后的搜索路径和解析完成的 ShaderAsset。
 *
 * 入口 main.json 语义：
 *   - "renderer"：值为 ".json" 结尾 → 相对入口文件目录加载子配置，
 *     挂为同名 section
 *   - "interactor"：相机交互器类型（"trackball" 或 "first_person"）
 *   - "scene"：字符串值相对入口文件目录转绝对路径
 *   - "ibl.path"：IBL 环境目录，相对入口文件目录转绝对路径；
 *     "ibl.enabled"：是否启用 IBL
 *   - 其余 key 原样转换（object→section，bool→bool，number→double，
 *     string→string，字符串数组→StringList，其余数组/null 跳过并警告）
 * 文件不存在或解析失败：警告 + 返回空 Config（全默认，行为与无配置一致）。
 */
class ConfigLoader {
public:
    /// @param configFile 配置文件；相对路径统一锚定项目 configs/ 目录。
    static engine::Config load(const std::string& configFile);

    /// 注册宿主提供的自定义 Shader/HLSL 根目录。内置 Shader 目录由引擎提供。
    void addCustomShaderSearchPath(std::filesystem::path path);

    const std::vector<std::filesystem::path>& customShaderSearchPaths() const {
        return customShaderSearchPaths_;
    }

    /// 递归发现并注册搜索路径下的所有 *.shader.json。
    void registerShaderAssets(engine::ShaderAssetManager& manager) const;

private:
    std::vector<std::filesystem::path> customShaderSearchPaths_;
};

} // namespace app
