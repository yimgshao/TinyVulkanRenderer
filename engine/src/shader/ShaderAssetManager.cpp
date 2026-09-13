#include "engine/shader/ShaderAssetManager.h"
#include <stdexcept>
namespace engine {
void ShaderAssetManager::createDefaults(VulkanContext& context) {
    device_ = context.device;
    const std::pair<const char*, uint32_t> colors[] = {
        {"white", 0xffffffffu}, {"black", 0xff000000u}, {"normal", 0xffff8080u}, {"transparent", 0u}};
    for (const auto& [name, rgba] : colors) {
        auto texture = std::make_unique<Texture>();
        texture->createSolid(rgba, context.device, context.physicalDevice, context.commandPool, context.graphicsQueue);
        defaults_.emplace(name, std::move(texture));
        for (auto type : {VK_IMAGE_VIEW_TYPE_3D, VK_IMAGE_VIEW_TYPE_CUBE}) {
            std::array<uint32_t, 6> pixels; pixels.fill(rgba);
            auto dimensional = std::make_unique<Texture>();
            dimensional->createRgba8(type, {1,1,1},
                {reinterpret_cast<const uint8_t*>(pixels.data()), type == VK_IMAGE_VIEW_TYPE_CUBE ? 24u : 4u},
                context.device, context.physicalDevice, context.commandPool, context.graphicsQueue);
            defaults_.emplace(std::string(name) + (type == VK_IMAGE_VIEW_TYPE_3D ? ":3D" : ":Cube"), std::move(dimensional));
        }
    }
}
void ShaderAssetManager::cleanup() {
    assets_.clear();
    for (auto& [name, texture] : defaults_) texture->cleanup(device_);
    defaults_.clear(); compiler_ = nullptr; descriptors_ = nullptr;
}
ShaderAsset& ShaderAssetManager::registerAsset(ShaderAsset description) {
    if (!compiler_ || !descriptors_) throw std::runtime_error("ShaderAssetManager is not initialized");
    if (assets_.contains(description.name)) throw std::runtime_error("Duplicate ShaderAsset: " + description.name);
    auto asset = std::make_unique<ShaderAsset>(std::move(description));
    for (const auto& property : asset->properties) {
        if (property.type < ShaderPropertyType::Texture2D) continue;
        const auto found = defaults_.find(property.defaultTexture + (property.type == ShaderPropertyType::Texture3D ? ":3D" : property.type == ShaderPropertyType::TextureCube ? ":Cube" : ""));
        if (found == defaults_.end()) throw std::runtime_error("Unknown default texture: " + property.defaultTexture);
        asset->defaultTextures[property.name] = found->second.get();
    }
    asset->prepare(*compiler_, *descriptors_);
    auto& result = *asset;
    const auto name = asset->name;
    assets_.emplace(name, std::move(asset));
    return result;
}
ShaderAsset* ShaderAssetManager::find(std::string_view name) const {
    auto found = assets_.find(name);
    return found == assets_.end() ? nullptr : found->second.get();
}
} // namespace engine
