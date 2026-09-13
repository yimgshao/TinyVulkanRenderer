#pragma once
#include "engine/shader/ShaderAsset.h"
#include "engine/scene/Texture.h"
#include <vk_mem_alloc.h>
#include <map>
#include <span>

namespace engine {
class Material {
public:
    Material() = default;
    Material(const Material&) = delete;
    Material& operator=(const Material&) = delete;
    void init(ShaderAsset& shader);
    void cleanup();
    void setFloat(std::string_view name, float value);
    void setInt(std::string_view name, int32_t value);
    void setBool(std::string_view name, bool value);
    void setVector(std::string_view name, std::span<const float> values);
    void setTexture(std::string_view name, Texture* value);
    void setKeyword(const std::string& name, bool value);
    void upload(uint32_t frameIndex = 0);
    ShaderAsset& getShader() const { return *shader_; }
    const ShaderParamSet& getKeywords() const { return keywords_; }
    DescriptorSetHandle getSet(uint32_t frameIndex = 0) const { return frames_.at(frameIndex).set; }
    std::optional<VkCullModeFlags> cullOverride;
private:
    ShaderAsset* shader_ = nullptr;
    ShaderParamSet keywords_;
    struct GpuFrame {
        DescriptorSetHandle set;
        VkBuffer buffer = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
        void* mapped = nullptr;
        uint64_t version = 0;
    };
    std::array<GpuFrame, 2> frames_;
    std::vector<uint8_t> bytes_;
    std::map<std::string, Texture*, std::less<>> textures_;
    uint64_t version_ = 1;
    const ShaderProperty& property(std::string_view name) const;
    void write(std::string_view name, const void* data, size_t bytes, bool integer);
};
} // namespace engine
