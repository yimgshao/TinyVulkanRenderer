#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <variant>

namespace engine {

using ShaderParamValue = std::variant<bool, int32_t, float>;

class ShaderParamSet {
public:
    void set(const std::string& key, bool v);
    void set(const std::string& key, int32_t v);
    void set(const std::string& key, float v);

    bool    getBool (const std::string& key, bool defaultVal = false) const;
    int32_t getInt  (const std::string& key, int32_t defaultVal = 0) const;
    float   getFloat(const std::string& key, float defaultVal = 0.0f) const;
    bool    has   (const std::string& key) const;
    bool    empty () const;

    uint64_t hash() const;

    bool operator==(const ShaderParamSet& other) const;
    bool operator!=(const ShaderParamSet& other) const { return !(*this == other); }

private:
    void invalidateCache();

    std::map<std::string, ShaderParamValue> params_;
    mutable std::optional<uint64_t>         cachedHash_;
};

} // namespace engine
