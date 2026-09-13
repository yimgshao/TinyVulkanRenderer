#include "engine/scene/Material.h"
#include "engine/VulkanUtils.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace engine {
const ShaderProperty& Material::property(std::string_view name) const {
    if (!shader_) throw std::runtime_error("Material is not initialized");
    auto* result = shader_->findProperty(name);
    if (!result) throw std::runtime_error("Material " + shader_->name + ": unknown property " + std::string(name));
    return *result;
}
void Material::init(ShaderAsset& shader) {
    if (shader_ || !shader.descriptors) throw std::runtime_error("Material requires a prepared ShaderAsset");
    shader_ = &shader;
    keywords_ = shader.defaultKeywords;
    textures_ = shader.defaultTextures;
    for (const auto& binding : shader.materialInterface.bindings) {
        if (binding.descriptorType != VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) continue;
        if (!bytes_.empty()) throw std::runtime_error("Only one Material UBO is supported: " + shader.name);
        bytes_.resize(binding.blockSize, 0);
    }
    try {
        for (auto& frame : frames_) {
            frame.set = shader.descriptors->allocate(shader.materialLayoutId);
            if (!bytes_.empty()) {
                createBufferVMA(bytes_.size(), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                VMA_MEMORY_USAGE_CPU_TO_GPU, frame.buffer, frame.allocation);
                if (vmaMapMemory(g_vmaAllocator, frame.allocation, &frame.mapped) != VK_SUCCESS)
                    throw std::runtime_error("Cannot map material buffer");
            }
        }
        for (const auto& p : shader.properties) {
            if (p.type >= ShaderPropertyType::Texture2D) continue;
            if (p.type == ShaderPropertyType::Int || p.type == ShaderPropertyType::Bool) {
                int32_t value = p.defaultInteger;
                write(p.name, &value, sizeof(value), true);
            } else {
                size_t count = p.type == ShaderPropertyType::Float2 ? 2 : p.type == ShaderPropertyType::Float3 ? 3
                    : p.type == ShaderPropertyType::Float4 ? 4 : p.type == ShaderPropertyType::Matrix4 ? 16 : 1;
                write(p.name, p.defaultValue.data(), count * sizeof(float), false);
            }
        }
    } catch (...) { cleanup(); throw; }
}
void Material::cleanup() {
    for (auto& frame : frames_) {
        if (frame.mapped) vmaUnmapMemory(g_vmaAllocator, frame.allocation);
        if (frame.buffer) vmaDestroyBuffer(g_vmaAllocator, frame.buffer, frame.allocation);
        if (shader_ && frame.set.isValid()) shader_->descriptors->free(shader_->materialLayoutId, frame.set);
        frame = {};
    }
    shader_ = nullptr;
    textures_.clear(); bytes_.clear();
}
void Material::write(std::string_view name, const void* data, size_t size, bool integer) {
    property(name);
    for (const auto& binding : shader_->materialInterface.bindings)
        for (const auto& member : binding.members)
            if (member.name == name) {
                if (member.size != size || member.integer != integer || member.offset + size > bytes_.size())
                    throw std::runtime_error("Material uniform type/layout mismatch: " + std::string(name));
                std::memcpy(bytes_.data() + member.offset, data, size);
                ++version_;
                return;
            }
    throw std::runtime_error("Material uniform absent from reflection: " + std::string(name));
}
void Material::setFloat(std::string_view name, float value) {
    if (property(name).type != ShaderPropertyType::Float) throw std::runtime_error("Expected float: " + std::string(name));
    write(name, &value, sizeof(value), false);
}
void Material::setInt(std::string_view name, int32_t value) {
    if (property(name).type != ShaderPropertyType::Int) throw std::runtime_error("Expected int: " + std::string(name));
    write(name, &value, sizeof(value), true);
}
void Material::setBool(std::string_view name, bool value) {
    if (property(name).type != ShaderPropertyType::Bool) throw std::runtime_error("Expected bool: " + std::string(name));
    int32_t word = value ? 1 : 0;
    write(name, &word, sizeof(word), true);
}
void Material::setVector(std::string_view name, std::span<const float> values) {
    const auto type = property(name).type;
    if (type < ShaderPropertyType::Float2 || type > ShaderPropertyType::Matrix4)
        throw std::runtime_error("Expected vector/matrix: " + std::string(name));
    write(name, values.data(), values.size_bytes(), false);
}
void Material::setTexture(std::string_view name, Texture* value) {
    if (property(name).type < ShaderPropertyType::Texture2D || !value)
        throw std::runtime_error("Expected texture: " + std::string(name));
    const auto type = property(name).type;
    const auto expected = type == ShaderPropertyType::Texture3D ? VK_IMAGE_VIEW_TYPE_3D :
        type == ShaderPropertyType::TextureCube ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D;
    if (value->viewType() != expected) throw std::runtime_error("Texture dimension mismatch: " + std::string(name));
    textures_[std::string(name)] = value;
    ++version_;
}
void Material::setKeyword(const std::string& name, bool value) {
    if (std::find(shader_->keywordNames.begin(), shader_->keywordNames.end(), name) == shader_->keywordNames.end())
        throw std::runtime_error("Unknown material keyword: " + name);
    keywords_.set(name, value);
}
void Material::upload(uint32_t frameIndex) {
    auto& gpu = frames_.at(frameIndex);
    if (gpu.version == version_) return;
    if (gpu.mapped) {
        std::memcpy(gpu.mapped, bytes_.data(), bytes_.size());
        vmaFlushAllocation(g_vmaAllocator, gpu.allocation, 0, bytes_.size());
    }
    for (const auto& binding : shader_->materialInterface.bindings) {
        if (binding.descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) {
            VkDescriptorBufferInfo info{gpu.buffer, 0, bytes_.size()};
            shader_->descriptors->writeBuffer(shader_->materialLayoutId, gpu.set, binding.binding, info);
        } else {
            auto textureName = binding.name;
            if (binding.descriptorType == VK_DESCRIPTOR_TYPE_SAMPLER && textureName.ends_with("Sampler"))
                textureName.resize(textureName.size() - 7);
            auto found = textures_.find(textureName);
            if (found == textures_.end()) throw std::runtime_error("Material texture must be bound: " + binding.name);
            shader_->descriptors->writeImage(shader_->materialLayoutId, gpu.set, binding.binding,
                                            found->second->descriptorInfo(), binding.descriptorType);
        }
    }
    gpu.version = version_;
}
} // namespace engine
