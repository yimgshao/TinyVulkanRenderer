#pragma once

#include "engine/scene/MaterialTemplate.h"
#include "engine/scene/Texture.h"
#include "engine/descriptor/DescriptorSetManager.h"

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>

namespace engine {

// AlphaMode defined in MaterialTemplate.h

struct MaterialParams {
    alignas(16) glm::vec4 baseColorFactor{1.0f, 1.0f, 1.0f, 1.0f};
    alignas(16) glm::vec4 emissiveFactor{0.0f, 0.0f, 0.0f, 0.0f};
    float metallicFactor      = 1.0f;
    float roughnessFactor     = 1.0f;
    float normalScale         = 1.0f;
    float occlusionStrength   = 1.0f;
    float alphaCutoff         = 0.5f;
    AlphaMode alphaMode       = AlphaMode::Opaque;
    uint32_t  doubleSided     = 0;
};

/**
 * MaterialInstance holds per-instance material parameters, texture bindings,
 * and the descriptor set used for rendering.
 *
 * Analogous to Unreal's UMaterialInstance: it references a MaterialTemplate
 * (which provides the pipeline) and overrides parameters / textures.
 */
class MaterialInstance {
public:
    void init(MaterialTemplate* tmpl);
    void cleanup(VkDevice device);

    void setBaseColor(Texture* tex) { baseColor = tex; }
    void setOrm(Texture* tex)       { orm       = tex; }
    void setNormal(Texture* tex)    { normal    = tex; }
    void setEmissive(Texture* tex)  { emissive  = tex; }

    void setParams(const MaterialParams& p);
    const MaterialParams& getParams() const { return params; }

    void setShaderParams(const ShaderParamSet& p) { shaderParams = p; }
    const ShaderParamSet& getShaderParams() const { return shaderParams; }

    void writeDescriptorSet();

    DescriptorSetHandle getSet() const { return descriptorSet; }
    MaterialTemplate* getTemplate() const { return template_; }

    const std::string& getMaterialType() const {
        static const std::string kUnknown = "Unknown";
        return template_ ? template_->getMaterialType() : kUnknown;
    }

private:
    MaterialTemplate* template_ = nullptr;

    Texture* baseColor = nullptr;
    Texture* orm       = nullptr;
    Texture* normal    = nullptr;
    Texture* emissive  = nullptr;

    MaterialParams params;
    ShaderParamSet shaderParams;

    DescriptorSetHandle descriptorSet;
    VkBuffer uboBuffer = VK_NULL_HANDLE;
    VmaAllocation uboAllocation = VK_NULL_HANDLE;
    void* uboMapped = nullptr;
};

} // namespace engine
