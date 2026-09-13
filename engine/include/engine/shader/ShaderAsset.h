#pragma once

#include "engine/renderer/PipelineStateDesc.h"
#include "engine/shader/ShaderVariantManager.h"
#include "engine/descriptor/DescriptorSetManager.h"
#include <array>
#include <filesystem>
#include <string_view>
#include <map>

namespace engine {
class Texture;

enum class ShaderPropertyType { Bool, Int, Float, Float2, Float3, Float4, Matrix4,
                                Texture2D, Texture3D, TextureCube };

struct ShaderProperty {
    std::string name;
    ShaderPropertyType type = ShaderPropertyType::Float;
    std::array<float, 16> defaultValue{};
    int32_t defaultInteger = 0;
    std::string defaultTexture;
};

struct ShaderPass {
    std::string name;
    std::string lightMode;
    ShaderModuleConfig shader;
    PipelineStateDesc state;
    std::string vertexLayout = "StaticMesh";
};

// Serializable, device-independent description. Hosts can construct this directly;
// JSON parsing is a host policy, not a dependency of the renderer.
struct ShaderAsset {
    std::string name;
    std::filesystem::path sourcePath;
    std::vector<ShaderProperty> properties;
    std::vector<ShaderPass> passes;
    ShaderParamSet defaultKeywords;
    std::vector<std::string> keywordNames;
    std::map<std::string, Texture*, std::less<>> defaultTextures;

    // Prepared once for a device. The asset owns bytecode references; descriptor
    // layouts are shared and owned by DescriptorSetManager.
    DescriptorSetManager* descriptors = nullptr;
    LayoutId materialLayoutId = kInvalidLayoutId;
    VkDescriptorSetLayout materialLayout = VK_NULL_HANDLE;
    DescriptorSetLayoutDesc materialInterface;
    std::vector<std::shared_ptr<const ShaderVariantBytecode>> compiledPasses;
    void prepare(ShaderVariantManager& compiler, DescriptorSetManager& manager);

    const ShaderPass* findPass(std::string_view lightMode) const;
    const ShaderProperty* findProperty(std::string_view name) const;
    void validate() const;
};

} // namespace engine
