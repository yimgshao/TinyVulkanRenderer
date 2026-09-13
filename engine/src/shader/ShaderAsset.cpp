#include "engine/shader/ShaderAsset.h"
#include <set>
#include <stdexcept>
#include <algorithm>

namespace engine {
const ShaderPass* ShaderAsset::findPass(std::string_view tag) const {
    for (const auto& pass : passes) if (pass.lightMode == tag) return &pass;
    return nullptr;
}
const ShaderProperty* ShaderAsset::findProperty(std::string_view key) const {
    for (const auto& property : properties) if (property.name == key) return &property;
    return nullptr;
}
void ShaderAsset::validate() const {
    auto fail = [&](const std::string& message) {
        throw std::runtime_error("ShaderAsset '" + name + "' (" + sourcePath.string() + "): " + message);
    };
    if (name.empty() || passes.empty()) fail("name and at least one pass are required");
    std::set<std::string> names, tags, propertyNames, keywords;
    for (const auto& pass : passes) {
        if (pass.name.empty() || pass.lightMode.empty() || pass.shader.moduleName.empty())
            fail("pass name, lightMode and module are required");
        if (!names.insert(pass.name).second || !tags.insert(pass.lightMode).second)
            fail("duplicate pass name or lightMode: " + pass.name);
        bool vertex = false;
        std::set<ShaderStage> stages;
        for (auto stage : pass.shader.stages) {
            if (stage != ShaderStage::Vertex && stage != ShaderStage::Fragment)
                fail("only vertex and fragment stages are supported");
            if (!stages.insert(stage).second) fail("duplicate stage in " + pass.name);
            vertex |= stage == ShaderStage::Vertex;
        }
        if (!vertex) fail("graphics pass requires a vertex stage: " + pass.name);
    }
    for (const auto& property : properties)
        if (property.name.empty() || !propertyNames.insert(property.name).second)
            fail("empty or duplicate property: " + property.name);
    for (const auto& keyword : keywordNames)
        if (keyword.empty() || !keywords.insert(keyword).second)
            fail("empty or duplicate keyword: " + keyword);
}

void ShaderAsset::prepare(ShaderVariantManager& compiler, DescriptorSetManager& manager) {
    validate();
    if (descriptors) throw std::runtime_error("ShaderAsset already prepared: " + name);
    if (keywordNames.size() > 8) throw std::runtime_error("ShaderAsset supports at most 8 boolean keywords: " + name);
    DescriptorSetLayoutDesc merged;
    merged.setIndex = 1;
    std::vector<std::shared_ptr<const ShaderVariantBytecode>> bytecodes;
    for (auto& pass : passes) {
        pass.shader.preserveBindings = true;
        ShaderVariantKey key;
        key.materialParamHash = defaultKeywords.hash();
        auto bytecode = compiler.GetOrCreateVariant(pass.shader, key, defaultKeywords, {});
        if (!bytecode) throw std::runtime_error("ShaderAsset " + name + ": failed pass " + pass.name);
        // Compile the declared boolean domain before allocating descriptor layouts.
        // Optimizers may remove an unused texture even with preserve-bindings.
        auto interfaceCode = std::make_shared<ShaderVariantBytecode>(*bytecode);
        for (uint32_t mask = 0; mask < (1u << keywordNames.size()); ++mask) {
            auto keywords = defaultKeywords;
            for (size_t k = 0; k < keywordNames.size(); ++k)
                keywords.set(keywordNames[k], (mask & (1u << k)) != 0);
            key.materialParamHash = keywords.hash();
            auto variant = compiler.GetOrCreateVariant(pass.shader, key, keywords, {});
            if (!variant) throw std::runtime_error("ShaderAsset " + name + ": invalid keyword variant in " + pass.name);
            for (const auto& set : variant->reflection.sets) {
                auto dest = std::find_if(interfaceCode->reflection.sets.begin(), interfaceCode->reflection.sets.end(),
                    [&](const auto& s) { return s.setIndex == set.setIndex; });
                if (dest == interfaceCode->reflection.sets.end()) {
                    interfaceCode->reflection.sets.push_back(set);
                    continue;
                }
                for (const auto& binding : set.bindings) {
                    auto found = std::find_if(dest->bindings.begin(), dest->bindings.end(),
                        [&](const auto& b) { return b.binding == binding.binding; });
                    if (found == dest->bindings.end()) dest->bindings.push_back(binding);
                    else {
                        if (found->name != binding.name || found->descriptorType != binding.descriptorType ||
                            found->descriptorCount != binding.descriptorCount || found->blockSize != binding.blockSize ||
                            found->members != binding.members || found->imageViewType != binding.imageViewType)
                            throw std::runtime_error("ShaderAsset " + name + ": keyword changes resource ABI in " + pass.name);
                        found->stageFlags |= binding.stageFlags;
                    }
                }
            }
            if (variant->reflection.pushConstant && (!interfaceCode->reflection.pushConstant ||
                variant->reflection.pushConstant->size > interfaceCode->reflection.pushConstant->size))
                interfaceCode->reflection.pushConstant = variant->reflection.pushConstant;
        }
        bytecode = interfaceCode;
        bytecodes.push_back(bytecode);
        if (const auto* set = bytecode->reflection.findSet(1)) {
            for (const auto& binding : set->bindings) {
                auto found = std::find_if(merged.bindings.begin(), merged.bindings.end(), [&](const auto& b) {
                    return b.binding == binding.binding;
                });
                if (found == merged.bindings.end()) merged.bindings.push_back(binding);
                else {
                    if (found->name != binding.name || found->descriptorType != binding.descriptorType ||
                        found->blockSize != binding.blockSize || found->descriptorCount != binding.descriptorCount ||
                        found->members != binding.members || found->imageViewType != binding.imageViewType)
                        throw std::runtime_error("ShaderAsset " + name + ": inconsistent material binding in " + pass.name);
                    found->stageFlags |= binding.stageFlags;
                }
            }
        }
    }
    std::sort(merged.bindings.begin(), merged.bindings.end(), [](const auto& a, const auto& b) { return a.binding < b.binding; });
    auto fail = [&](const std::string& reason) { throw std::runtime_error("ShaderAsset " + name + ": " + reason); };
    unsigned uniformBlocks = 0;
    std::set<std::string> reflected;
    for (const auto& b : merged.bindings) {
        if (b.descriptorCount != 1) fail("descriptor arrays are not material properties: " + b.name);
        if (b.descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) {
            if (++uniformBlocks > 1) fail("one material uniform block is supported");
            for (const auto& m : b.members) {
                // Explicit padding fields need no serialized property.
                if (m.name.starts_with("_pad")) continue;
                const auto* property = findProperty(m.name);
                if (!property || property->type >= ShaderPropertyType::Texture2D) fail("missing uniform property: " + m.name);
                const auto type = property->type;
                const bool integer = type == ShaderPropertyType::Int || type == ShaderPropertyType::Bool;
                const uint32_t columns = type == ShaderPropertyType::Matrix4 ? 4 : 1;
                const uint32_t components = type == ShaderPropertyType::Float2 ? 2 : type == ShaderPropertyType::Float3 ? 3 :
                    (type == ShaderPropertyType::Float4 || type == ShaderPropertyType::Matrix4) ? 4 : 1;
                if (m.array || m.integer != integer || m.columns != columns || m.components != components ||
                    m.size != columns * components * 4) fail("uniform type/layout mismatch: " + m.name);
                reflected.insert(m.name);
            }
        } else if (b.descriptorType == VK_DESCRIPTOR_TYPE_SAMPLER) {
            if (!b.name.ends_with("Sampler") || !findProperty(b.name.substr(0,b.name.size()-7)))
                fail("material samplers must be named <textureProperty>Sampler: " + b.name);
        } else {
            if (b.descriptorType != VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER && b.descriptorType != VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE)
                fail("unsupported material resource: " + b.name);
            const auto* property = findProperty(b.name);
            if (!property || property->type < ShaderPropertyType::Texture2D) fail("missing texture property: " + b.name);
            const auto expected = property->type == ShaderPropertyType::Texture3D ? VK_IMAGE_VIEW_TYPE_3D :
                property->type == ShaderPropertyType::TextureCube ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D;
            if (b.imageViewType != expected) fail("texture dimension mismatch: " + b.name);
            reflected.insert(b.name);
        }
    }
    for (const auto& property : properties)
        if (!reflected.contains(property.name)) fail("property is absent from every pass/keyword variant: " + property.name);
    std::vector<VkDescriptorSetLayoutBinding> bindings;
    for (const auto& b : merged.bindings)
        bindings.push_back({b.binding, b.descriptorType, b.descriptorCount,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr});
    const auto layout = manager.getOrCreateLayout(bindings, 1000);
    materialLayoutId = layout.id;
    materialLayout = layout.layout;
    materialInterface = std::move(merged);
    compiledPasses = std::move(bytecodes);
    descriptors = &manager;
}
} // namespace engine
