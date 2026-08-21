#include "engine/shader/ShaderParam.h"

#include <algorithm>
#include <cstring>

namespace engine {

// ---------------------------------------------------------------------------
// Setters
// ---------------------------------------------------------------------------

void ShaderParamSet::set(const std::string& key, bool v) {
    invalidateCache();
    params_[key] = v;
}

void ShaderParamSet::set(const std::string& key, int32_t v) {
    invalidateCache();
    params_[key] = v;
}

void ShaderParamSet::set(const std::string& key, float v) {
    invalidateCache();
    params_[key] = v;
}

// ---------------------------------------------------------------------------
// Getters
// ---------------------------------------------------------------------------

bool ShaderParamSet::getBool(const std::string& key, bool defaultVal) const {
    auto it = params_.find(key);
    if (it == params_.end()) return defaultVal;
    if (auto* p = std::get_if<bool>(&it->second)) return *p;
    return defaultVal;
}

int32_t ShaderParamSet::getInt(const std::string& key, int32_t defaultVal) const {
    auto it = params_.find(key);
    if (it == params_.end()) return defaultVal;
    if (auto* p = std::get_if<int32_t>(&it->second)) return *p;
    return defaultVal;
}

float ShaderParamSet::getFloat(const std::string& key, float defaultVal) const {
    auto it = params_.find(key);
    if (it == params_.end()) return defaultVal;
    if (auto* p = std::get_if<float>(&it->second)) return *p;
    return defaultVal;
}

bool ShaderParamSet::has(const std::string& key) const {
    return params_.find(key) != params_.end();
}

bool ShaderParamSet::empty() const {
    return params_.empty();
}

// ---------------------------------------------------------------------------
// Hash
// ---------------------------------------------------------------------------

uint64_t ShaderParamSet::hash() const {
    if (cachedHash_) return *cachedHash_;

    uint64_t h = 0x9e3779b97f4a7c15ULL;
    auto mix = [&h](uint64_t v) {
        h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    };

    for (const auto& [key, val] : params_) {
        // Hash key
        for (char c : key) mix(static_cast<uint64_t>(c));
        // Hash type tag + value
        if (auto* p = std::get_if<bool>(&val)) {
            mix(0);  // type tag: bool
            mix(*p ? 1ULL : 0ULL);
        } else if (auto* p = std::get_if<int32_t>(&val)) {
            mix(1);  // type tag: int32
            mix(static_cast<uint64_t>(*p));
        } else if (auto* p = std::get_if<float>(&val)) {
            mix(2);  // type tag: float
            uint32_t bits;
            std::memcpy(&bits, p, sizeof(bits));
            mix(static_cast<uint64_t>(bits));
        }
    }

    cachedHash_ = h;
    return h;
}

// ---------------------------------------------------------------------------
// Comparison
// ---------------------------------------------------------------------------

bool ShaderParamSet::operator==(const ShaderParamSet& other) const {
    return params_ == other.params_;
}

// ---------------------------------------------------------------------------
// Cache invalidation
// ---------------------------------------------------------------------------

void ShaderParamSet::invalidateCache() {
    cachedHash_.reset();
}

} // namespace engine
