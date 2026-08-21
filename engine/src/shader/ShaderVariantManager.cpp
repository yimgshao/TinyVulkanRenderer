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

bool ShaderVariantManager::Init(const std::filesystem::path& shaderSearchDir,
                                 bool enableDebugInfo) {
    return compiler_->Init(shaderSearchDir, enableDebugInfo);
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
    const ShaderParamSet&     passParams,
    const std::string&        materialHeader) {

    // 第一级缓存键：模块路径（同一模块的 stage 集合固定，见 ShaderModuleConfig::stages）
    // 注：materialHeader 与 key.materialType 一一对应（由 MaterialTemplate 注册保证），
    // 因此缓存键中 materialType 已唯一标识注入的材质头文件。
    auto& variantCache = programCaches_[config.moduleName];

    // 1. 查缓存
    auto it = variantCache.find(key);
    if (it != variantCache.end()) {
        return it->second;
    }

    // 2. 委托编译
    auto bytecode = compiler_->CompileVariant(config, key, materialParams, passParams,
                                              materialHeader);
    if (!bytecode) {
        return nullptr;
    }

    // 3. 写入缓存
    variantCache[key] = bytecode;
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
