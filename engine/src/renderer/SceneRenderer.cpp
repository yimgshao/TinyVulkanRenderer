#include "engine/renderer/SceneRenderer.h"

#include "engine/VulkanUtils.h"
#include "engine/descriptor/DescriptorSetManager.h"
#include "engine/pso/PsoManager.h"
#include "engine/renderer/RenderPassContext.h"
#include "engine/scene/FrameUBO.h"
#include "engine/scene/Scene.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <map>
#include <stdexcept>

namespace engine {

SceneRenderer::~SceneRenderer() { cleanup(); }

void SceneRenderer::init(VkDevice device, DescriptorSetManager& descriptors,
                         PsoManager& pso, ShaderAssetManager& assets,
                         VkDescriptorSetLayout frameLayout) {
    cleanup();
    device_ = device;
    descriptors_ = &descriptors;
    pso_ = &pso;
    assets_ = &assets;
    frameLayout_ = frameLayout;

    // Descriptor buffers are bound once at frame recording start. Register every
    // potential Set 2 layout now so DrawScene never grows that binding table late.
    for (const auto& [assetName, asset] : assets_->assets()) {
        for (size_t passIndex = 0; passIndex < asset->compiledPasses.size(); ++passIndex) {
            const auto* set = asset->compiledPasses[passIndex]->reflection.findSet(2);
            if (!set) continue;
            std::vector<VkDescriptorSetLayoutBinding> bindings;
            bindings.reserve(set->bindings.size());
            for (const auto& binding : set->bindings)
                bindings.push_back({binding.binding, binding.descriptorType,
                                    binding.descriptorCount, binding.stageFlags, nullptr});
            descriptors_->getOrCreateLayout(bindings, 256);
        }
    }

    VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler.magFilter = sampler.minFilter = VK_FILTER_LINEAR;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler.addressModeU = sampler.addressModeV = sampler.addressModeW =
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(device_, &sampler, nullptr, &sampler_) != VK_SUCCESS)
        throw std::runtime_error("SceneRenderer: sampler creation failed");
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler.compareEnable = VK_TRUE;
    sampler.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    if (vkCreateSampler(device_, &sampler, nullptr, &comparisonSampler_) != VK_SUCCESS) {
        cleanup();
        throw std::runtime_error("SceneRenderer: comparison sampler creation failed");
    }
}

void SceneRenderer::destroyEntry(Entry& entry) {
    for (auto& frameSlots : entry.passSlots) {
        for (auto& slot : frameSlots) {
            if (slot.mapped) vmaUnmapMemory(g_vmaAllocator, slot.allocation);
            if (slot.buffer) vmaDestroyBuffer(g_vmaAllocator, slot.buffer, slot.allocation);
            if (descriptors_ && slot.set.isValid())
                descriptors_->free(entry.passLayoutId, slot.set);
        }
        frameSlots.clear();
    }
    if (entry.desc.layout && device_)
        vkDestroyPipelineLayout(device_, entry.desc.layout, nullptr);
    entry.desc.layout = VK_NULL_HANDLE;
}

void SceneRenderer::cleanup() {
    for (auto& pass : preparedPasses_)
        for (auto& entry : pass.entries) destroyEntry(entry);
    preparedPasses_.clear();
    if (comparisonSampler_ && device_)
        vkDestroySampler(device_, comparisonSampler_, nullptr);
    if (sampler_ && device_) vkDestroySampler(device_, sampler_, nullptr);
    comparisonSampler_ = sampler_ = VK_NULL_HANDLE;
    device_ = VK_NULL_HANDLE;
    descriptors_ = nullptr;
    pso_ = nullptr;
    assets_ = nullptr;
    frameLayout_ = VK_NULL_HANDLE;
}

SceneRenderer::PreparedPass& SceneRenderer::getOrPrepare(
    const SceneRenderPassInfo& passInfo, std::string_view tag) {
    auto cached = std::find_if(preparedPasses_.begin(), preparedPasses_.end(),
        [&](const PreparedPass& pass) {
            return pass.passIndex == passInfo.passIndex && pass.tag == tag;
        });
    if (cached != preparedPasses_.end()) return *cached;
    if (!assets_ || !descriptors_ || !pso_)
        throw std::runtime_error("SceneRenderer is not initialized");

    PreparedPass prepared;
    prepared.passIndex = passInfo.passIndex;
    prepared.tag = tag;
    try {
        for (const auto& [name, asset] : assets_->assets()) {
            const auto* shaderPass = asset->findPass(tag);
            if (!shaderPass) continue;
            if (shaderPass->vertexLayout.empty())
                throw std::runtime_error("Scene ShaderPass requires a vertex layout: " +
                                         name + "/" + shaderPass->name);

            prepared.entries.emplace_back();
            auto& entry = prepared.entries.back();
            entry.asset = asset.get();
            entry.pass = shaderPass;
            entry.desc = passInfo.targets;
            entry.desc.shaderConfig = shaderPass->shader;
            entry.desc.state = shaderPass->state;
            entry.desc.vertexLayoutName = shaderPass->vertexLayout;
            const auto shaderPassIndex = static_cast<size_t>(shaderPass - asset->passes.data());
            const auto& reflection = asset->compiledPasses.at(shaderPassIndex)->reflection;

            if (const auto* frame = reflection.findSet(0)) {
                if (frame->bindings.size() != 1 || frame->bindings.front().binding != 0 ||
                    frame->bindings.front().descriptorType != VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER ||
                    frame->bindings.front().descriptorCount != 1 ||
                    frame->bindings.front().blockSize > sizeof(FrameUBO))
                    throw std::runtime_error("Invalid FrameSet ABI in " + name + "/" + shaderPass->name);
                const std::map<std::string, size_t> offsets{
                    {"view", offsetof(FrameUBO, view)}, {"proj", offsetof(FrameUBO, proj)},
                    {"invViewProj", offsetof(FrameUBO, invViewProj)},
                    {"cameraPos", offsetof(FrameUBO, cameraPos)},
                    {"lightCount", offsetof(FrameUBO, lightCount)},
                    {"exposureEV", offsetof(FrameUBO, exposureEV)},
                    {"timeSeconds", offsetof(FrameUBO, timeSeconds)},
                    {"shadowPcfRadius", offsetof(FrameUBO, shadowPcfRadius)},
                    {"lights", offsetof(FrameUBO, lights)}};
                for (const auto& member : frame->bindings.front().members)
                    if (!offsets.contains(member.name) || offsets.at(member.name) != member.offset)
                        throw std::runtime_error("FrameSet member mismatch: " + name + "/" + member.name);
            }

            std::vector<VkDescriptorSetLayout> layouts{frameLayout_, asset->materialLayout};
            if (const auto* set = reflection.findSet(2)) {
                entry.passInterface = *set;
                std::vector<VkDescriptorSetLayoutBinding> bindings;
                for (const auto& binding : set->bindings) {
                    if (binding.descriptorCount != 1)
                        throw std::runtime_error("Pass descriptor arrays unsupported: " + binding.name);
                    const bool uniform = binding.descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                    const bool image = binding.descriptorType == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE ||
                                       binding.descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                    const bool samplerBinding = binding.descriptorType == VK_DESCRIPTOR_TYPE_SAMPLER;
                    if (!uniform && !image && !samplerBinding)
                        throw std::runtime_error("Unsupported pass resource: " + binding.name);
                    if (uniform) {
                        if (entry.passDataBinding != UINT32_MAX)
                            throw std::runtime_error("Only one PassData UBO is supported: " + name);
                        entry.passDataBinding = binding.binding;
                        entry.passDataSize = binding.blockSize;
                    } else if (image && (!passInfo.reads ||
                               std::none_of(passInfo.reads->begin(), passInfo.reads->end(),
                                   [&](const auto& read) { return read.resourceName == binding.name; }))) {
                        throw std::runtime_error("RenderPass " + passInfo.targets.passName +
                                                 " missing resource " + binding.name);
                    }
                    bindings.push_back({binding.binding, binding.descriptorType,
                                        binding.descriptorCount, binding.stageFlags, nullptr});
                }
                auto registered = descriptors_->getOrCreateLayout(bindings, 256);
                entry.passLayoutId = registered.id;
                layouts.push_back(registered.layout);
            }
            for (const auto& set : reflection.sets)
                if (set.setIndex > 2)
                    throw std::runtime_error("Scene ShaderPass supports sets 0, 1 and 2 only: " + name);

            VkPushConstantRange objectPush{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                           0, static_cast<uint32_t>(sizeof(glm::mat4))};
            if (reflection.pushConstant && reflection.pushConstant->size > objectPush.size)
                throw std::runtime_error("Scene ShaderPass object push constant exceeds model matrix: " + name);
            VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
            layoutInfo.setLayoutCount = static_cast<uint32_t>(layouts.size());
            layoutInfo.pSetLayouts = layouts.data();
            layoutInfo.pushConstantRangeCount = 1;
            layoutInfo.pPushConstantRanges = &objectPush;
            if (vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &entry.desc.layout) != VK_SUCCESS)
                throw std::runtime_error("SceneRenderer: pipeline layout creation failed: " + name);
            entry.desc.materialParams = asset->defaultKeywords;
            entry.desc.variantKey.materialParamHash = asset->defaultKeywords.hash();
            entry.desc.variantKey.passParamHash = passInfo.targets.passParams.hash();
            pso_->getOrCreate(entry.desc);
        }
    } catch (...) {
        for (auto& entry : prepared.entries) destroyEntry(entry);
        throw;
    }
    preparedPasses_.push_back(std::move(prepared));
    return preparedPasses_.back();
}

SceneRenderer::PassSlot& SceneRenderer::getSlot(Entry& entry, uint32_t frameIndex,
                                                uint32_t invocation) {
    auto& slots = entry.passSlots.at(frameIndex);
    while (slots.size() <= invocation) {
        PassSlot slot;
        slot.set = descriptors_->allocate(entry.passLayoutId);
        try {
            if (entry.passDataSize) {
                createBufferVMA(entry.passDataSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                VMA_MEMORY_USAGE_CPU_TO_GPU, slot.buffer, slot.allocation);
                if (vmaMapMemory(g_vmaAllocator, slot.allocation, &slot.mapped) != VK_SUCCESS)
                    throw std::runtime_error("SceneRenderer: cannot map PassData buffer");
                VkDescriptorBufferInfo buffer{slot.buffer, 0, entry.passDataSize};
                descriptors_->writeBuffer(entry.passLayoutId, slot.set,
                                          entry.passDataBinding, buffer);
            }
        } catch (...) {
            if (slot.mapped) vmaUnmapMemory(g_vmaAllocator, slot.allocation);
            if (slot.buffer) vmaDestroyBuffer(g_vmaAllocator, slot.buffer, slot.allocation);
            if (slot.set.isValid()) descriptors_->free(entry.passLayoutId, slot.set);
            throw;
        }
        slots.push_back(slot);
    }
    return slots[invocation];
}

void SceneRenderer::drawScene(RenderPassContext& context, std::string_view shaderPassTag) {
    if (!context.frame_->scene) {
        context.passData_.clear();
        ++context.drawInvocation_;
        return;
    }
    auto& prepared = getOrPrepare(*context.passInfo_, shaderPassTag);
    const uint32_t invocation = context.drawInvocation_++;
    const auto& reads = *context.passInfo_->reads;
    const auto& comparisonSamplers = *context.passInfo_->comparisonSamplers;

    for (auto& entry : prepared.entries) {
        PassSlot* slot = nullptr;
        if (!entry.passInterface.bindings.empty()) {
            slot = &getSlot(entry, context.frame_->frameIndex, invocation);
            if (entry.passDataSize) {
                if (context.passData_.size() > entry.passDataSize)
                    throw std::runtime_error("PassData is larger than shader Set 2 UBO in " +
                                             entry.asset->name + "/" + entry.pass->name);
                std::memset(slot->mapped, 0, entry.passDataSize);
                if (!context.passData_.empty())
                    std::memcpy(slot->mapped, context.passData_.data(), context.passData_.size());
                vmaFlushAllocation(g_vmaAllocator, slot->allocation, 0, entry.passDataSize);
            } else if (!context.passData_.empty()) {
                throw std::runtime_error("PassData supplied but ShaderPass has no Set 2 UBO: " +
                                         entry.asset->name + "/" + entry.pass->name);
            }
            for (const auto& binding : entry.passInterface.bindings) {
                if (binding.descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) continue;
                VkDescriptorImageInfo image{};
                image.sampler = std::find(comparisonSamplers.begin(), comparisonSamplers.end(), binding.name)
                    != comparisonSamplers.end() ? comparisonSampler_ : sampler_;
                if (binding.descriptorType != VK_DESCRIPTOR_TYPE_SAMPLER) {
                    const auto read = std::find_if(reads.begin(), reads.end(),
                        [&](const auto& candidate) { return candidate.resourceName == binding.name; });
                    image.imageView = context.resources_->GetImageView(read->handle);
                    const auto format = context.resources_->GetDesc(read->handle).format;
                    image.imageLayout = format == VK_FORMAT_D32_SFLOAT || format == VK_FORMAT_D16_UNORM ||
                        format == VK_FORMAT_D24_UNORM_S8_UINT || format == VK_FORMAT_D32_SFLOAT_S8_UINT
                        ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;
                }
                descriptors_->writeImage(entry.passLayoutId, slot->set, binding.binding,
                                         image, binding.descriptorType);
            }
        } else if (!context.passData_.empty()) {
            throw std::runtime_error("PassData supplied but ShaderPass has no Set 2: " +
                                     entry.asset->name + "/" + entry.pass->name);
        }
    }

    struct Draw { const RenderObject* object; Entry* entry; float depth; };
    std::vector<Draw> draws;
    const auto& camera = context.frame_->scene->getCamera();
    const auto forward = glm::normalize(camera.target - camera.position);
    for (const auto& object : context.frame_->scene->getRenderObjects()) {
        if (!object.material || !object.mesh) continue;
        for (auto& entry : prepared.entries) {
            if (&object.material->getShader() != entry.asset) continue;
            const float depth = glm::dot(glm::vec3(object.transform[3]) - camera.position, forward);
            draws.push_back({&object, &entry, depth});
        }
    }
    std::stable_sort(draws.begin(), draws.end(),
        [](const Draw& a, const Draw& b) { return a.depth < b.depth; });

    for (const auto& draw : draws) {
        const auto& object = *draw.object;
        auto& entry = *draw.entry;
        auto* material = object.material;
        material->upload(context.frame_->frameIndex);
        auto desc = entry.desc;
        if (material->cullOverride) desc.state.cullMode = *material->cullOverride;
        desc.materialParams = material->getKeywords();
        desc.variantKey.materialParamHash = material->getKeywords().hash();
        vkCmdBindPipeline(context.commandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          pso_->getOrCreate(desc));
        const auto materialSet = material->getSet(context.frame_->frameIndex);
        ext::vkCmdSetDescriptorBufferOffsetsEXT(context.commandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS,
            desc.layout, 0, 1, &context.frame_->frameSet.bufferIndex, &context.frame_->frameSet.offset);
        ext::vkCmdSetDescriptorBufferOffsetsEXT(context.commandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS,
            desc.layout, 1, 1, &materialSet.bufferIndex, &materialSet.offset);
        if (!entry.passInterface.bindings.empty()) {
            const auto& slot = entry.passSlots.at(context.frame_->frameIndex).at(invocation);
            ext::vkCmdSetDescriptorBufferOffsetsEXT(context.commandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS,
                desc.layout, 2, 1, &slot.set.bufferIndex, &slot.set.offset);
        }
        vkCmdPushConstants(context.commandBuffer_, desc.layout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(glm::mat4), &object.transform);
        object.mesh->bind(context.commandBuffer_);
        object.mesh->drawIndexed(context.commandBuffer_);
    }
    context.passData_.clear();
}

void RenderPassContext::DrawScene(std::string_view shaderPassTag) {
    sceneRenderer_->drawScene(*this, shaderPassTag);
}

} // namespace engine
