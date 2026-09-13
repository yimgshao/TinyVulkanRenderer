#include "engine/shader/ShaderVariantManager.h"
#include "engine/shader/DxcCompiler.h"

#include <iostream>
#include <string>

namespace engine {

// =============================================================================
// ShaderVariantManager 实现
// =============================================================================

ShaderVariantManager::ShaderVariantManager()
    : compiler_(std::make_unique<DxcCompiler>()) {
}

ShaderVariantManager::~ShaderVariantManager() = default;
ShaderVariantManager::ShaderVariantManager(ShaderVariantManager&&) noexcept = default;
ShaderVariantManager& ShaderVariantManager::operator=(ShaderVariantManager&&) noexcept = default;

// -------------------------------------------------------------------------
// 初始化
// -------------------------------------------------------------------------

bool ShaderVariantManager::Init(
    const std::vector<std::filesystem::path>& shaderSearchDirs,
    bool enableDebugInfo) {
    return compiler_->Init(shaderSearchDirs, enableDebugInfo);
}

bool ShaderVariantManager::Init(const std::filesystem::path& shaderSearchDir,
                                bool enableDebugInfo) {
    return Init(std::vector<std::filesystem::path>{shaderSearchDir},
                enableDebugInfo);
}

void ShaderVariantManager::Cleanup() {
    programCaches_.clear();
    compiler_->Cleanup();
}

// -------------------------------------------------------------------------
// 变体获取
// -------------------------------------------------------------------------

std::shared_ptr<const ShaderVariantBytecode> ShaderVariantManager::GetOrCreateVariant(
    const ShaderModuleConfig& config,
    const ShaderVariantKey&   key,
    const ShaderParamSet&     materialParams,
    const ShaderParamSet&     passParams) {

    // Program identity includes source, stages, and keyword declarations.
    std::string compilationKey = config.moduleName;
    compilationKey += config.preserveBindings ? "\npreserve" : "\noptimize";
    for (auto stage : config.stages)
        compilationKey += "\nstage:" + std::to_string(static_cast<uint32_t>(stage));
    for (const auto& name : config.genericValueParams)
        compilationKey += "\nkeyword:" + name;
    auto& variantCache = programCaches_[compilationKey];

    ShaderParamSet effective;
    for (const auto& name : config.genericValueParams)
        effective.set(name, passParams.has(name) ? passParams.getBool(name) : materialParams.getBool(name));
    ShaderVariantKey effectiveKey;
    effectiveKey.materialParamHash = effective.hash();
    // Only compile-affecting values enter the cache, never runtime properties.
    auto it = variantCache.find(effectiveKey);
    if (it != variantCache.end()) {
        return it->second;
    }

    // 2. 委托编译
    auto bytecode = compiler_->CompileVariant(config, key, materialParams, passParams);
    if (!bytecode) {
        return nullptr;
    }

    // 3. 写入缓存
    variantCache[effectiveKey] = bytecode;
    return bytecode;
}

// -------------------------------------------------------------------------
// 缓存管理
// -------------------------------------------------------------------------

void ShaderVariantManager::ClearCache() {
    programCaches_.clear();
    compiler_->ClearCaches();
}

size_t ShaderVariantManager::GetCachedVariantCount() const {
    size_t total = 0;
    for (const auto& [_, cache] : programCaches_) {
        total += cache.size();
    }
    return total;
}

// =============================================================================
// ShaderReflection helpers
// =============================================================================

const DescriptorBindingDesc* ShaderReflection::findBinding(uint32_t setIndex,
                                                           std::string_view name) const {
    for (const auto& s : sets) {
        if (s.setIndex != setIndex) continue;
        for (const auto& b : s.bindings) {
            if (b.name == name) return &b;
        }
    }
    return nullptr;
}

const DescriptorSetLayoutDesc* ShaderReflection::findSet(uint32_t setIndex) const {
    for (const auto& s : sets) {
        if (s.setIndex == setIndex) return &s;
    }
    return nullptr;
}

} // namespace engine
