#pragma once

#include "engine/descriptor/DescriptorSetManager.h"
#include "engine/renderer/PipelineStateDesc.h"
#include "engine/shader/ShaderVariantManager.h"
#include "engine/shader/ShaderParam.h"
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>
#include <vector>
#include <string>

namespace engine {

class ShaderVariantManager;
class PsoManager;
struct ShaderReflection;

enum class AlphaMode : uint32_t { Opaque = 0, Mask = 1, Blend = 2 };

/**
 * MaterialTemplate 初始化配置（ShaderParamSet 驱动版本）。
 */
struct MaterialTemplateCreateInfo {
    ShaderVariantManager* variantManager = nullptr;
    PsoManager*           psoManager     = nullptr;
    std::string           materialType;
    AlphaMode             alphaMode      = AlphaMode::Opaque;

    /// 材质实现头文件（相对 shader 搜索目录，如 "materials/blinn_phong.hlsl"）。
    /// 编译变体时由 DxcCompiler 注入到 pass 模块之前，pass 与材质互不感知。
    std::string           materialHeader;

    /// 全局 frame set layout（Set 0），由 RenderModule 拥有。
    VkDescriptorSetLayout frameSetLayout = VK_NULL_HANDLE;

    /// renderer 级全局 set layout（如阴影 atlas、G-Buffer、IBL），
    /// 追加在 material set 之后：set index = materialSetIndex + 1 + i。
    /// 使用本模板创建的所有 PSO 的 pipeline layout 都会包含它们。
    std::vector<VkDescriptorSetLayout> extraSetLayouts;

    /// Descriptor manager（MaterialInstance 通过它分配 material set slot）。
    DescriptorSetManager* descManager    = nullptr;
};

/**
 * MaterialTemplate -- 单一材质着色方案（descriptor layout + PSO 请求入口）。
 *
 * 不绑定任何 pass 的 shader 配置：pass 在请求 PSO 时传入自己的
 * ShaderModuleConfig，材质头文件由本模板注入，编译器在编译期配对。
 *
 * PSO 的组装与缓存已下沉到引擎级 PsoManager，本类只负责把材质特有
 * 信息（materialType / materialHeader / alphaMode 状态修正）填进
 * GraphicsPSODesc 并转发。
 */
class MaterialTemplate {
public:
    void init(VkDevice device, VkPhysicalDevice physicalDevice,
              const MaterialTemplateCreateInfo& createInfo);
    void cleanup(VkDevice device);

    VkPipeline getOrCreatePipeline(VkDevice device,
                                   const ShaderModuleConfig& shaderConfig,
                                   const std::string&     passName,
                                   const PipelineStateDesc& state,
                                   const ShaderVariantKey& shaderVariant,
                                   const ShaderParamSet&   materialParams,
                                   const ShaderParamSet&   passParams,
                                   uint32_t colorAttachmentCount,
                                   const VkFormat* pColorFormats,
                                   VkFormat depthFormat,
                                   VkSampleCountFlagBits msaaSamples);

    VkDescriptorSetLayout getSetLayout()        const { return setLayout; }
    VkPipelineLayout      getPipelineLayout()   const { return pipelineLayout; }
    AlphaMode             getAlphaMode()        const { return alphaMode; }
    uint32_t              getMaterialSetIndex() const { return materialSetIndex; }
    LayoutId              getMaterialLayoutId() const { return materialLayoutId; }
    const std::string&    getMaterialType()   const { return materialType; }
    const std::string&    getMaterialHeader() const { return materialHeader; }

    VkDevice              getDevice()          const { return device; }
    VkPhysicalDevice      getPhysicalDevice()  const { return physicalDevice; }
    DescriptorSetManager* getDescManager()     const { return descManager; }

    const ShaderReflection* getReflection() const { return defaultReflection; }

private:
    VkDevice              device          = VK_NULL_HANDLE;
    VkPhysicalDevice      physicalDevice  = VK_NULL_HANDLE;
    DescriptorSetManager* descManager     = nullptr;
    PsoManager*           psoManager      = nullptr;

    VkDescriptorSetLayout setLayout        = VK_NULL_HANDLE;
    VkDescriptorSetLayout dummySetLayout   = VK_NULL_HANDLE;
    VkPipelineLayout      pipelineLayout   = VK_NULL_HANDLE;
    uint32_t              materialSetIndex = 0;
    LayoutId              materialLayoutId = kInvalidLayoutId;

    AlphaMode             alphaMode       = AlphaMode::Opaque;
    ShaderVariantManager* variantManager  = nullptr;
    std::string           materialType;
    std::string           materialHeader;

    const ShaderReflection* defaultReflection = nullptr;
};

} // namespace engine
