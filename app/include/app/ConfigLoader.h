#pragma once

#include "engine/config/Config.h"

#include <string>

namespace app {

/**
 * ConfigLoader -- 把 JSON 配置文件解析为 engine::Config。
 *
 * "配置来自 JSON" 是 app 层策略，引擎只消费 Config 容器、不碰 JSON。
 *
 * 入口 main.json 语义：
 *   - "renderer"/"material"：值为 ".json" 结尾 → 相对入口文件目录加载子配置，
 *     挂为同名 section；值为 "forward"/"deferred"/"pbr" 等 → 视为内联 type
 *   - "scene"/"ibl"：字符串值相对入口文件目录转绝对路径
 *   - 其余 key 原样转换（object→section，bool→bool，number→double，
 *     string→string，array/null 跳过并警告）
 * 文件不存在或解析失败：警告 + 返回空 Config（全默认，行为与无配置一致）。
 */
class ConfigLoader {
public:
    /// @param mainJsonPath 入口配置路径；相对路径锚定项目根（__FILE__ 方式）。
    static engine::Config load(const std::string& mainJsonPath);
};

} // namespace app
