#pragma once

#include "engine/shader/ShaderVariantManager.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace engine {

// =============================================================================
// DxcCompiler — DXC (HLSL) 编译 + spirv-cross 反射，所有重逻辑在此。
// =============================================================================
//
// 使用 PIMPL 隐藏 DXC / spirv-cross 头文件，避免向 includer 泄漏第三方类型。
//
// 变体机制：Slang 泛型特化被替换为主流的预处理器宏排列组合：
//   - 材质类型（ShaderVariantKey::materialType） -> -D<MaterialType>
//   - 布尔参数（ShaderModuleConfig::genericValueParams）-> -DUPPER_SNAKE=0/1
//
// 编译结果（SPIR-V + ShaderReflection）由调用方缓存，此类不负责。
//
class DxcCompiler {
public:
    DxcCompiler();
    ~DxcCompiler();

    DxcCompiler(const DxcCompiler&) = delete;
    DxcCompiler& operator=(const DxcCompiler&) = delete;
    DxcCompiler(DxcCompiler&&) noexcept;
    DxcCompiler& operator=(DxcCompiler&&) noexcept;

    // -------------------------------------------------------------------------
    // 生命周期
    // -------------------------------------------------------------------------

    /// 初始化 DXC 编译器实例与 include handler（不加载任何 shader）。
    bool Init(const std::filesystem::path& shaderSearchDir,
              bool enableDebugInfo = false);

    /// 释放所有 DXC 资源。
    void Cleanup();

    // -------------------------------------------------------------------------
    // 编译入口
    // -------------------------------------------------------------------------

    /// 编译一个 shader 变体。
    /// materialParams / passParams 仅在缓存缺失时用于构建 -D 宏定义。
    /// materialHeader 非空时，编译器会合成一个根编译单元：
    /// 先 #include 材质头文件（提供 evaluateMaterial / evaluateVertexOffset），
    /// 再 #include pass 模块——pass 与材质互不感知，由编译期注入配对。
    /// 返回完整的 SPIR-V + ShaderReflection，失败返回 nullptr。
    std::shared_ptr<ShaderVariantBytecode> CompileVariant(
        const ShaderModuleConfig& config,
        const ShaderVariantKey&   key,
        const ShaderParamSet&     materialParams,
        const ShaderParamSet&     passParams,
        const std::string&        materialHeader = "");

    // -------------------------------------------------------------------------
    // 缓存管理
    // -------------------------------------------------------------------------

    /// 清除内部缓存（当前无内部缓存，接口保留以对齐调用方）。
    void ClearCaches();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace engine
