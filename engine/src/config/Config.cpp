#include "engine/config/Config.h"

#include <iostream>
#include <sstream>
#include <vector>

namespace engine {

namespace {

// 把 "a.b.c" 拆成 ["a", "b"] + 末段 "c"。无点时 parts 为空。
void splitPath(const std::string& key,
               std::vector<std::string>& parts, std::string& leaf) {
    size_t start = 0;
    while (true) {
        size_t dot = key.find('.', start);
        if (dot == std::string::npos) {
            leaf = key.substr(start);
            break;
        }
        parts.push_back(key.substr(start, dot - start));
        start = dot + 1;
    }
}

} // anonymous namespace

const Config::Value* Config::find(const std::string& key) const {
    std::vector<std::string> parts;
    std::string leaf;
    splitPath(key, parts, leaf);

    const Config* node = this;
    for (const auto& p : parts) {
        auto it = node->sections_.find(p);
        if (it == node->sections_.end()) return nullptr;
        node = &it->second;
    }
    auto it = node->values_.find(leaf);
    return (it != node->values_.end()) ? &it->second : nullptr;
}

bool Config::has(const std::string& key) const {
    if (find(key) != nullptr) return true;
    // section 存在也算 has（供 "passParams" 这类整节消费判断）
    std::vector<std::string> parts;
    std::string leaf;
    splitPath(key, parts, leaf);
    const Config* node = this;
    for (const auto& p : parts) {
        auto it = node->sections_.find(p);
        if (it == node->sections_.end()) return false;
        node = &it->second;
    }
    return node->sections_.count(leaf) > 0;
}

const Config& Config::section(const std::string& key) const {
    static const Config kEmpty;
    std::vector<std::string> parts;
    std::string leaf;
    splitPath(key, parts, leaf);

    const Config* node = this;
    for (const auto& p : parts) {
        auto it = node->sections_.find(p);
        if (it == node->sections_.end()) return kEmpty;
        node = &it->second;
    }
    auto it = node->sections_.find(leaf);
    return (it != node->sections_.end()) ? it->second : kEmpty;
}

namespace {

template <typename T>
T convertOrDefault(const Config::Value* v, const std::string& key, T def) {
    if (!v) return def;
    if constexpr (std::is_same_v<T, bool> || std::is_same_v<T, double> ||
                  std::is_same_v<T, std::string>) {
        // variant 直接成员类型
        if (auto p = std::get_if<T>(v)) return *p;
    } else if constexpr (std::is_same_v<T, float> ||
                         std::is_same_v<T, int32_t>) {
        // double 存储值到 float / int32 的收窄转换
        if (auto d = std::get_if<double>(v)) return static_cast<T>(*d);
    }
    std::cerr << "[Config] key '" << key << "' type mismatch, using default.\n";
    return def;
}

} // anonymous namespace

bool Config::getBool(const std::string& key, bool def) const {
    return convertOrDefault(find(key), key, def);
}
double Config::getDouble(const std::string& key, double def) const {
    return convertOrDefault(find(key), key, def);
}
float Config::getFloat(const std::string& key, float def) const {
    return convertOrDefault(find(key), key, def);
}
int32_t Config::getInt(const std::string& key, int32_t def) const {
    return convertOrDefault(find(key), key, def);
}
std::string Config::getString(const std::string& key,
                              const std::string& def) const {
    return convertOrDefault(find(key), key, def);
}

void Config::set(const std::string& key, bool v)        { setImpl(key, Value{v}); }
void Config::set(const std::string& key, double v)      { setImpl(key, Value{v}); }
void Config::set(const std::string& key, const std::string& v) { setImpl(key, Value{v}); }

void Config::setImpl(const std::string& key, Value v) {
    std::vector<std::string> parts;
    std::string leaf;
    splitPath(key, parts, leaf);

    Config* node = this;
    for (const auto& p : parts) node = &node->sections_[p];
    node->values_[leaf] = std::move(v);
}

void Config::setSection(const std::string& key, Config sub) {
    std::vector<std::string> parts;
    std::string leaf;
    splitPath(key, parts, leaf);

    Config* node = this;
    for (const auto& p : parts) node = &node->sections_[p];
    node->sections_[leaf] = std::move(sub);
    // 同一个 key 是值或 section 二选一：挂 section 时清掉同名标量值
    node->values_.erase(leaf);
}

std::string Config::dump(const std::string& prefix) const {
    std::ostringstream os;
    for (const auto& [k, v] : values_) {
        os << prefix << k << " = ";
        if (auto b = std::get_if<bool>(&v))            os << (*b ? "true" : "false");
        else if (auto d = std::get_if<double>(&v))     os << *d;
        else                                           os << '"' << std::get<std::string>(v) << '"';
        os << '\n';
    }
    for (const auto& [k, sub] : sections_) {
        os << prefix << k << ":\n";
        os << sub.dump(prefix + "  ");
    }
    return os.str();
}

} // namespace engine
