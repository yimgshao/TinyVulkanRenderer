#pragma once

#include "engine/shader/ShaderParam.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace engine {

class DxcCompiler;

// =============================================================================
// Shader Stage 枚举
// =============================================================================

enum class ShaderStage : uint32_t {
    Vertex   = 0,
    Fragment = 1,
    Compute  = 2,
    Geometry = 3,
    Hull     = 4,
    Domain   = 5,
};

// =============================================================================
// 一、Shader 变体键（全部用字符串/hash，无枚举）
// =============================================================================

struct ShaderVariantKey {
    std::string materialType;  // 材质类型名（HLSL 宏名），如 "BlinnPhongMaterial"
    uint64_t    materialParamHash = 0;
    uint64_t    passParamHash     = 0;

    bool operator==(const ShaderVariantKey& other) const = default;
};

// =============================================================================
// 三、Shader 模块配置
// =============================================================================

struct ShaderModuleConfig {
    std::string moduleName;  // 相对 shader 目录的路径（无扩展名），同时作为变体缓存键

    /// 布尔变体参数名，按声明顺序排列。
    /// 每个名字在编译变体时从 pass params 或 material params 中查找，
    /// 并以 -D 宏形式注入 HLSL（camelCase 转 UPPER_SNAKE，如 useFog -> USE_FOG）。
    /// 例如：["useFog", "useNormalMap"]
    std::vector<std::string> genericValueParams;

    /// 本模块包含的 stage。入口函数名为固定约定：
    ///   Vertex -> "vertexMain"，Fragment -> "fragmentMain"。
    /// 默认 {Vertex, Fragment}；VS-only 模块（如阴影深度）传 {Vertex}。
    /// 同一模块不支持两套不同的 stage 集合。
    std::vector<ShaderStage> stages = {ShaderStage::Vertex, ShaderStage::Fragment};
};

// =============================================================================
// 四、Shader Reflection
// =============================================================================

struct DescriptorBindingDesc {
    uint32_t           setIndex        = 0;
    uint32_t           binding         = 0;
    VkDescriptorType   descriptorType  = VK_DESCRIPTOR_TYPE_MAX_ENUM;
    uint32_t           descriptorCount = 1;
    VkShaderStageFlags stageFlags      = 0;
    std::string        name;
};

struct DescriptorSetLayoutDesc {
    uint32_t setIndex = 0;
    std::vector<DescriptorBindingDesc> bindings;
};

struct ShaderReflection {
    std::vector<DescriptorSetLayoutDesc>            sets;
    std::optional<VkPushConstantRange>              pushConstant;
    VkVertexInputBindingDescription                 vertexBinding{};
    std::vector<VkVertexInputAttributeDescription>  vertexAttrs;
    bool                                            hasVertexInput = false;

    const DescriptorBindingDesc* findBinding(uint32_t setIndex,
                                             std::string_view name) const;
    const DescriptorSetLayoutDesc* findSet(uint32_t setIndex) const;
};

// =============================================================================
// 五、单个变体的编译结果
// =============================================================================

struct ShaderVariantBytecode {
    std::vector<uint32_t> vertexSpirv;
    std::vector<uint32_t> fragmentSpirv;
    ShaderReflection      reflection;
};

// =============================================================================
// 六、Shader 变体管理器
// =============================================================================

class ShaderVariantManager {
public:
    ShaderVariantManager();
    ~ShaderVariantManager();

    ShaderVariantManager(const ShaderVariantManager&) = delete;
    ShaderVariantManager& operator=(const ShaderVariantManager&) = delete;
    ShaderVariantManager(ShaderVariantManager&&) noexcept;
    ShaderVariantManager& operator=(ShaderVariantManager&&) noexcept;

    // -------------------------------------------------------------------------
    // 初始化
    // -------------------------------------------------------------------------

    bool Init(const std::filesystem::path& shaderSearchDir,
              bool enableDebugInfo = false);
    void Cleanup();

    // -------------------------------------------------------------------------
    // 变体获取（核心接口）
    // -------------------------------------------------------------------------

    std::shared_ptr<const ShaderVariantBytecode> GetOrCreateVariant(
        const ShaderModuleConfig& config,
        const ShaderVariantKey&   key,
        const ShaderParamSet&     materialParams,
        const ShaderParamSet&     passParams,
        const std::string&        materialHeader = "");

    // -------------------------------------------------------------------------
    // 缓存管理
    // -------------------------------------------------------------------------

    void ClearCache();
    size_t GetCachedVariantCount() const;

private:
    std::unique_ptr<DxcCompiler> compiler_;

    // moduleName -> { variantKey -> bytecode }
    std::unordered_map<std::string,
        std::unordered_map<ShaderVariantKey,
            std::shared_ptr<ShaderVariantBytecode>>> programCaches_;
};

} // namespace engine

namespace std {
template<>
struct hash<engine::ShaderVariantKey> {
    std::size_t operator()(const engine::ShaderVariantKey& key) const {
        std::size_t h1 = std::hash<std::string>{}(key.materialType);
        std::size_t h2 = std::hash<uint64_t>{}(key.materialParamHash);
        std::size_t h3 = std::hash<uint64_t>{}(key.passParamHash);
        return h1 ^ (h2 << 1) ^ (h3 << 2);
    }
};
} // namespace std
