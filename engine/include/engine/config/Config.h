#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <variant>

namespace engine {

/**
 * Config -- 树形键值配置容器（类 dict）。
 *
 * 引擎侧只持有容器，不关心配置来源（JSON / ImGui / 代码）。
 * 值类型收敛为 bool / double / string / 子树四种，JSON 数字统一存 double。
 * 所有查找支持 "a.b.c" 点路径；缺失或类型不符返回调用方给的默认值，
 * 即"缺任何 key 行为不变"。
 */
class Config {
public:
    using Value = std::variant<bool, double, std::string>;

    bool has(const std::string& key) const;

    /// 取子树（支持点路径）；缺失时返回静态空节点，可安全链式调用。
    const Config& section(const std::string& key) const;

    /// 取值。key 缺失或类型不符时返回默认值（类型不符打警告）。
    bool        getBool  (const std::string& key, bool def = false) const;
    double      getDouble(const std::string& key, double def = 0.0) const;
    float       getFloat (const std::string& key, float def = 0.0f) const;
    int32_t     getInt   (const std::string& key, int32_t def = 0) const;
    std::string getString(const std::string& key, const std::string& def = {}) const;

    /// 写值（支持点路径，中间 section 不存在时自动创建）。
    void set(const std::string& key, bool v);
    void set(const std::string& key, double v);
    void set(const std::string& key, const std::string& v);
    void set(const std::string& key, const char* v) { set(key, std::string(v)); }
    void setSection(const std::string& key, Config sub);

    /// 通用遍历（供 "passParams" 这类整节灌入的消费场景）。
    const std::map<std::string, Value>&  values() const { return values_; }
    const std::map<std::string, Config>& sections() const { return sections_; }

    /// 调试用全量打印。
    std::string dump(const std::string& prefix = "") const;

private:
    /// 按点路径查找叶子值，未命中返回 nullptr。
    const Value* find(const std::string& key) const;
    void setImpl(const std::string& key, Value v);

    std::map<std::string, Value>  values_;
    std::map<std::string, Config> sections_;
};

} // namespace engine
