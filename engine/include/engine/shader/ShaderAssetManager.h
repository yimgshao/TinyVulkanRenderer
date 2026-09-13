#pragma once
#include "engine/shader/ShaderAsset.h"
#include <map>
#include "engine/scene/Texture.h"
#include "engine/VulkanContext.h"

namespace engine {
class ShaderAssetManager {
public:
    void init(ShaderVariantManager& compiler, DescriptorSetManager& descriptors) {
        compiler_ = &compiler; descriptors_ = &descriptors;
    }
    ShaderAsset& registerAsset(ShaderAsset description);
    void createDefaults(VulkanContext& context);
    ShaderAsset* find(std::string_view name) const;
    const auto& assets() const { return assets_; }
    void cleanup();
private:
    ShaderVariantManager* compiler_ = nullptr;
    DescriptorSetManager* descriptors_ = nullptr;
    std::map<std::string, std::unique_ptr<ShaderAsset>, std::less<>> assets_;
    std::map<std::string, std::unique_ptr<Texture>, std::less<>> defaults_;
    VkDevice device_ = VK_NULL_HANDLE;
};
} // namespace engine
