// engine/src/renderer/rendergraph/RenderGraph.cpp
#include "engine/renderer/rendergraph/RenderGraph.h"
#include "engine/VulkanUtils.h"
#include "engine/pso/PsoManager.h"

#include <algorithm>
#include <array>
#include <iostream>
#include <optional>
#include <stdexcept>

namespace engine {

// ------------------------------------------------------------------
// RenderGraphBuilder method implementations (tightly coupled with RenderGraph)
// ------------------------------------------------------------------

RGTextureHandle RenderGraphBuilder::CreateTexture(const std::string& name, const RGTextureDesc& desc) {
    if (!owner) return kInvalidRGTextureHandle;
    RGTextureHandle handle = owner->AllocTextureHandle();
    owner->RegisterTexture(handle, name, desc);
    createdTextures.push_back(handle);
    return handle;
}

void RenderGraphBuilder::WriteColor(RGTextureHandle handle, const AttachmentDesc& desc) {
    colorOutputs.push_back({handle, desc});
}

void RenderGraphBuilder::WriteColorPreserve(RGTextureHandle handle) {
    AttachmentDesc desc{};
    desc.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    WriteColor(handle, desc);
}

void RenderGraphBuilder::WriteDepth(RGTextureHandle handle, const AttachmentDesc& desc) {
    depthOutputs.push_back({handle, desc});
}

RGTextureHandle RenderGraphBuilder::ReadTexture(const std::string& resourceName) {
    RGTextureHandle handle = FindTexture(resourceName);
    if (handle == kInvalidRGTextureHandle) {
        throw std::runtime_error("RenderGraphBuilder::ReadTexture: resource '" +
                                 resourceName + "' does not exist.");
    }
    reads.push_back({handle, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                     VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, resourceName});
    return handle;
}

void RenderGraphBuilder::SetShader(const std::string& moduleName) {
    shaderDesc.shaderConfig.moduleName = moduleName;
    shaderDesc.declared = true;
}

void RenderGraphBuilder::SetShader(const ShaderModuleConfig& config) {
    shaderDesc.shaderConfig = config;
    shaderDesc.declared = true;
}

void RenderGraphBuilder::SetPassParams(const ShaderParamSet& params) {
    shaderDesc.passParams = params;
}

void RenderGraphBuilder::SetPipelineState(const PipelineStateDesc& state) {
    shaderDesc.pipelineState = state;
}

void RenderGraphBuilder::SetVertexLayout(const std::string& layoutName) {
    shaderDesc.vertexLayout = layoutName;
}

void RenderGraphBuilder::SetPipelineLayout(VkPipelineLayout layout) {
    shaderDesc.pipelineLayout = layout;
}

void RenderGraphBuilder::SetMaterialHeader(const std::string& header) {
    shaderDesc.materialHeader = header;
}

void RenderGraphBuilder::SetMsaaSamples(VkSampleCountFlagBits samples) {
    shaderDesc.msaaSamples = samples;
}

void RenderGraphBuilder::SetAutoBindShader(bool enabled) {
    shaderDesc.autoBind = enabled;
}

RGTextureHandle RenderGraphBuilder::FindTexture(const std::string& name) const {
    if (!owner) return kInvalidRGTextureHandle;
    auto it = owner->nameToHandle.find(name);
    if (it != owner->nameToHandle.end()) {
        return it->second;
    }
    return kInvalidRGTextureHandle;
}

// ------------------------------------------------------------------
// Internal helpers
// ------------------------------------------------------------------

static VkImageAspectFlags GetAspectFlags(VkFormat format) {
    switch (format) {
        case VK_FORMAT_D16_UNORM:
        case VK_FORMAT_X8_D24_UNORM_PACK32:
        case VK_FORMAT_D32_SFLOAT:
            return VK_IMAGE_ASPECT_DEPTH_BIT;
        case VK_FORMAT_S8_UINT:
            return VK_IMAGE_ASPECT_STENCIL_BIT;
        case VK_FORMAT_D16_UNORM_S8_UINT:
        case VK_FORMAT_D24_UNORM_S8_UINT:
        case VK_FORMAT_D32_SFLOAT_S8_UINT:
            return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
        default:
            return VK_IMAGE_ASPECT_COLOR_BIT;
    }
}

// ------------------------------------------------------------------
// RenderGraph
// ------------------------------------------------------------------

void RenderGraph::Init(VulkanContext* ctx, PsoManager* manager,
                       ShaderVariantManager* variants,
                       DescriptorSetManager* descriptors,
                       VkDescriptorSetLayout frameLayout) {
    context = ctx;
    psoManager = manager;
    variantManager = variants;
    descManager = descriptors;
    frameSetLayout = frameLayout;
    if (context && defaultPassSampler == VK_NULL_HANDLE) {
        VkSamplerCreateInfo info{};
        info.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        info.magFilter    = VK_FILTER_LINEAR;
        info.minFilter    = VK_FILTER_LINEAR;
        info.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        if (vkCreateSampler(context->device, &info, nullptr,
                            &defaultPassSampler) != VK_SUCCESS) {
            throw std::runtime_error("RenderGraph: failed to create default pass sampler.");
        }

        info.mipmapMode    = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        info.compareEnable = VK_TRUE;
        info.compareOp     = VK_COMPARE_OP_LESS_OR_EQUAL;
        if (vkCreateSampler(context->device, &info, nullptr,
                            &defaultComparisonSampler) != VK_SUCCESS) {
            throw std::runtime_error(
                "RenderGraph: failed to create default comparison sampler.");
        }
    }
    // 预留 [0] 作为 sentinel
    if (textureInfos.empty()) {
        textureInfos.emplace_back();
        physicalResources.emplace_back();
        resourceStates.emplace_back();
    }
}

void RenderGraph::Cleanup() {
    FreePhysicalResources();
    if (context) {
        for (auto& node : passes) {
            if (descManager) {
                for (auto& resourceSet : node.autoResourceSets) {
                    if (resourceSet.set.isValid() &&
                        resourceSet.layoutId != kInvalidLayoutId) {
                        descManager->free(resourceSet.layoutId, resourceSet.set);
                    }
                }
            }
            if (node.pass) node.pass->pipelineRuntime_.reset();
            if (node.ownsPipelineLayout && node.pipelineRuntime &&
                node.pipelineRuntime->baseDesc.layout != VK_NULL_HANDLE) {
                vkDestroyPipelineLayout(context->device,
                    node.pipelineRuntime->baseDesc.layout, nullptr);
            }
        }
        if (defaultPassSampler != VK_NULL_HANDLE) {
            vkDestroySampler(context->device, defaultPassSampler, nullptr);
            defaultPassSampler = VK_NULL_HANDLE;
        }
        if (defaultComparisonSampler != VK_NULL_HANDLE) {
            vkDestroySampler(context->device, defaultComparisonSampler, nullptr);
            defaultComparisonSampler = VK_NULL_HANDLE;
        }
    }
    passes.clear();
    explicitDependencies_.clear();
    executionOrder_.clear();
    adjacency_.clear();
    inDegree_.clear();
    textureInfos.clear();
    physicalResources.clear();
    resourceStates.clear();
    nameToHandle.clear();
    nextHandle = 1;
    barrierScratch.clear();
    colorAttachmentScratch.clear();
    presentBarrierScratch.clear();
    context = nullptr;
    psoManager = nullptr;
    variantManager = nullptr;
    descManager = nullptr;
    frameSetLayout = VK_NULL_HANDLE;
}

RGTextureHandle RenderGraph::AllocTextureHandle() {
    return nextHandle++;
}

void RenderGraph::EnsureCapacity(RGTextureHandle handle) {
    size_t needed = static_cast<size_t>(handle) + 1;
    if (textureInfos.size() < needed) {
        textureInfos.resize(needed);
        physicalResources.resize(needed);
        resourceStates.resize(needed);
    }
}

void RenderGraph::RegisterTexture(RGTextureHandle handle, const std::string& name, const RGTextureDesc& desc) {
    EnsureCapacity(handle);
    nameToHandle[name] = handle;
    textureInfos[handle] = {name, desc};
}

RGTextureHandle RenderGraph::ImportTexture(const std::string& name, VkImage image, VkImageView view,
                                           const RGTextureDesc& desc, RGImportPolicy policy) {
    auto it = nameToHandle.find(name);
    if (it != nameToHandle.end()) {
        return it->second;
    }

    RGTextureHandle handle = AllocTextureHandle();
    RegisterTexture(handle, name, desc);

    PhysicalResource res;
    res.image        = image;
    res.imageView    = view;
    res.imported     = true;
    res.importPolicy = policy;
    physicalResources[handle] = res;

    return handle;
}

void RenderGraph::UpdateImportedTexture(RGTextureHandle handle, VkImage image, VkImageView view) {
    if (handle == kInvalidRGTextureHandle || handle >= physicalResources.size()) return;
    auto& res = physicalResources[handle];
    if (!res.imported) return;
    res.image = image;
    res.imageView = view;
}

VkImageView RenderGraph::GetPhysicalImageView(RGTextureHandle handle) const {
    if (handle == kInvalidRGTextureHandle || handle >= physicalResources.size()) {
        return VK_NULL_HANDLE;
    }
    return physicalResources[handle].imageView;
}

VkImage RenderGraph::GetPhysicalImage(RGTextureHandle handle) const {
    if (handle == kInvalidRGTextureHandle || handle >= physicalResources.size()) {
        return VK_NULL_HANDLE;
    }
    return physicalResources[handle].image;
}

const RGTextureDesc& RenderGraph::GetTextureDesc(RGTextureHandle handle) const {
    static const RGTextureDesc kInvalidDesc{};
    if (handle == kInvalidRGTextureHandle || handle >= textureInfos.size()) {
        return kInvalidDesc;
    }
    return textureInfos[handle].desc;
}

// ------------------------------------------------------------------
// RGResources：Execute 阶段物理资源只读访问器（代理 RenderGraph 查询）
// ------------------------------------------------------------------

VkImageView RGResources::GetImageView(RGTextureHandle handle) const {
    return graph ? graph->GetPhysicalImageView(handle) : VK_NULL_HANDLE;
}

VkImage RGResources::GetImage(RGTextureHandle handle) const {
    return graph ? graph->GetPhysicalImage(handle) : VK_NULL_HANDLE;
}

const RGTextureDesc& RGResources::GetDesc(RGTextureHandle handle) const {
    static const RGTextureDesc kInvalidDesc{};
    return graph ? graph->GetTextureDesc(handle) : kInvalidDesc;
}

void RenderGraph::AddPass(IRenderPass* pass,
                          const RenderGraphBuildContext& ctx) {
    if (!pass || !pass->enabled) return;
    if (pass->passName.empty()) {
        throw std::runtime_error("RenderGraph: pass name cannot be empty.");
    }
    const auto duplicate = std::find_if(
        passes.begin(), passes.end(), [&](const RGPassNode& node) {
            return node.pass && node.pass->passName == pass->passName;
        });
    if (duplicate != passes.end()) {
        throw std::runtime_error("RenderGraph: duplicate pass name '" +
                                 pass->passName + "'.");
    }

    RGPassNode node;
    node.pass = pass;
    node.builder.SetOwner(this);
    node.pass->Setup(node.builder, ctx);
    passes.push_back(std::move(node));
}

void RenderGraph::AddExecutionDependency(IRenderPass* before,
                                         IRenderPass* after) {
    if (!before || !after || before == after) {
        throw std::runtime_error(
            "RenderGraph: invalid explicit pass execution dependency.");
    }

    const auto isRegistered = [&](IRenderPass* pass) {
        return std::any_of(passes.begin(), passes.end(),
                           [&](const RGPassNode& node) {
                               return node.pass == pass;
                           });
    };
    if (!isRegistered(before) || !isRegistered(after)) {
        throw std::runtime_error(
            "RenderGraph: explicit execution dependency references an "
            "unregistered or disabled pass.");
    }

    const auto duplicate = std::any_of(
        explicitDependencies_.begin(), explicitDependencies_.end(),
        [&](const ExplicitPassDependency& dependency) {
            return dependency.before == before && dependency.after == after;
        });
    if (!duplicate) {
        explicitDependencies_.push_back({before, after});
    }
}

void RenderGraph::Compile() {
    FreePhysicalResources();
    AllocatePhysicalResources();
    BuildPassPipelines();
    BuildDependencyGraph();
}

void RenderGraph::BuildPassPipelines() {
    if (!context || !psoManager) return;

    for (auto& node : passes) {
        const PassShaderDesc& shader = node.builder.GetShaderDesc();
        if (!node.pass || !shader.declared) continue;
        if (shader.shaderConfig.moduleName.empty()) {
            throw std::runtime_error("RenderGraph: pass '" + node.pass->passName +
                                     "' declared an empty shader module.");
        }

        auto runtime = std::make_shared<PassPipelineRuntime>();
        runtime->psoManager = psoManager;

        GraphicsPSODesc& desc = runtime->baseDesc;
        desc.shaderConfig     = shader.shaderConfig;
        desc.passParams       = shader.passParams;
        desc.materialHeader   = shader.materialHeader;
        desc.vertexLayoutName = shader.vertexLayout;
        desc.state            = shader.pipelineState;
        desc.msaaSamples      = shader.msaaSamples;
        desc.passName         = node.pass->passName;

        for (const auto& output : node.builder.GetColorOutputs()) {
            if (desc.colorCount >= GraphicsPSODesc::kMaxColorAttachments) break;
            desc.colorFormats[desc.colorCount++] = GetTextureDesc(output.handle).format;
        }
        const auto& depths = node.builder.GetDepthOutputs();
        if (!depths.empty()) {
            desc.depthFormat = GetTextureDesc(depths.front().handle).format;
        }

        desc.layout = shader.pipelineLayout;
        std::shared_ptr<const ShaderVariantBytecode> compiled;
        if (desc.layout == VK_NULL_HANDLE || !node.builder.GetReads().empty()) {
            ShaderVariantKey key{};
            ShaderParamSet emptyMaterialParams;
            compiled = variantManager->GetOrCreateVariant(
                shader.shaderConfig, key, emptyMaterialParams,
                shader.passParams, shader.materialHeader);
            if (!compiled) {
                throw std::runtime_error("RenderGraph: failed to compile shader for pass '" +
                                         node.pass->passName + "'.");
            }

        }

        if (compiled && !node.builder.GetReads().empty()) {
            for (const auto& reflectedSet : compiled->reflection.sets) {
                const bool setHasDeclaredRead = std::any_of(
                    reflectedSet.bindings.begin(), reflectedSet.bindings.end(),
                    [&](const DescriptorBindingDesc& reflected) {
                        return std::any_of(
                            node.builder.GetReads().begin(),
                            node.builder.GetReads().end(),
                            [&](const PassResourceRead& read) {
                                return read.resourceName == reflected.name;
                            });
                    });
                if (!setHasDeclaredRead) continue;

                RGPassNode::AutoResourceSet resourceSet{};
                resourceSet.setIndex = reflectedSet.setIndex;
                std::vector<VkDescriptorSetLayoutBinding> layoutBindings;
                layoutBindings.reserve(reflectedSet.bindings.size());

                for (const auto& reflected : reflectedSet.bindings) {
                    const bool isTexture =
                        reflected.descriptorType == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE ||
                        reflected.descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                    const bool isSampler =
                        reflected.descriptorType == VK_DESCRIPTOR_TYPE_SAMPLER;
                    if (!isTexture && !isSampler) {
                        throw std::runtime_error(
                            "RenderGraph: descriptor set " +
                            std::to_string(reflectedSet.setIndex) + " in pass '" +
                            node.pass->passName + "' mixes graph textures with unsupported "
                            "descriptor types.");
                    }

                    VkDescriptorSetLayoutBinding binding{};
                    binding.binding         = reflected.binding;
                    binding.descriptorType  = reflected.descriptorType;
                    binding.descriptorCount = reflected.descriptorCount;
                    binding.stageFlags      = reflected.stageFlags;
                    layoutBindings.push_back(binding);

                    if (isTexture) {
                        auto read = std::find_if(
                            node.builder.GetReads().begin(),
                            node.builder.GetReads().end(),
                            [&](const PassResourceRead& item) {
                                return item.resourceName == reflected.name;
                            });
                        if (read == node.builder.GetReads().end()) {
                            throw std::runtime_error(
                                "RenderGraph: shader resource '" + reflected.name +
                                "' shares descriptor set " +
                                std::to_string(reflectedSet.setIndex) +
                                " with graph resources in pass '" +
                                node.pass->passName +
                                "', but has no matching ReadTexture declaration.");
                        }
                        resourceSet.textureBindings.push_back(
                            {read->handle, reflected.binding,
                             reflected.descriptorType});
                    } else {
                        resourceSet.samplerBindings.push_back(reflected.binding);
                    }
                }

                resourceSet.usesComparisonSampler =
                    !resourceSet.textureBindings.empty() &&
                    std::all_of(resourceSet.textureBindings.begin(),
                                resourceSet.textureBindings.end(),
                                [&](const RGPassNode::AutoTextureBinding& item) {
                                    return (GetAspectFlags(GetTextureDesc(item.handle).format) &
                                            VK_IMAGE_ASPECT_DEPTH_BIT) != 0;
                                });

                auto registered = descManager->getOrCreateLayout(layoutBindings);
                resourceSet.layoutId = registered.id;
                resourceSet.set = descManager->allocate(registered.id);
                const VkSampler samplerHandle = resourceSet.usesComparisonSampler
                    ? defaultComparisonSampler
                    : defaultPassSampler;
                for (uint32_t samplerBinding : resourceSet.samplerBindings) {
                    VkDescriptorImageInfo sampler{};
                    sampler.sampler = samplerHandle;
                    descManager->writeImage(resourceSet.layoutId, resourceSet.set,
                                            samplerBinding, sampler,
                                            VK_DESCRIPTOR_TYPE_SAMPLER);
                }
                node.autoResourceSets.push_back(std::move(resourceSet));
            }

            for (const auto& read : node.builder.GetReads()) {
                const bool matched = std::any_of(
                    node.autoResourceSets.begin(), node.autoResourceSets.end(),
                    [&](const RGPassNode::AutoResourceSet& resourceSet) {
                        return std::any_of(
                            resourceSet.textureBindings.begin(),
                            resourceSet.textureBindings.end(),
                            [&](const RGPassNode::AutoTextureBinding& item) {
                                return item.handle == read.handle;
                            });
                    });
                if (!matched) {
                    throw std::runtime_error(
                        "RenderGraph: pass '" + node.pass->passName +
                        "' reads texture '" + read.resourceName +
                        "', but its shader does not declare a sampled "
                        "texture with the same name.");
                }
            }
        }

        if (desc.layout == VK_NULL_HANDLE) {
            std::vector<VkDescriptorSetLayout> setLayouts;
            uint32_t maxSetIndex = 0;
            for (const auto& reflectedSet : compiled->reflection.sets) {
                maxSetIndex = std::max(maxSetIndex, reflectedSet.setIndex);
            }
            setLayouts.resize(maxSetIndex + 1, VK_NULL_HANDLE);
            if (!setLayouts.empty() && frameSetLayout != VK_NULL_HANDLE) {
                setLayouts[0] = frameSetLayout;
            }
            for (const auto& reflectedSet : compiled->reflection.sets) {
                if (reflectedSet.setIndex == 0 && frameSetLayout != VK_NULL_HANDLE) continue;
                std::vector<VkDescriptorSetLayoutBinding> bindings;
                bindings.reserve(reflectedSet.bindings.size());
                for (const auto& reflected : reflectedSet.bindings) {
                    bindings.push_back({reflected.binding, reflected.descriptorType,
                                        reflected.descriptorCount,
                                        reflected.stageFlags, nullptr});
                }
                setLayouts[reflectedSet.setIndex] =
                    descManager->getOrCreateLayout(bindings).layout;
            }
            for (auto& layout : setLayouts) {
                if (layout == VK_NULL_HANDLE) {
                    layout = descManager->getOrCreateLayout({}).layout;
                }
            }

            VkPipelineLayoutCreateInfo info{};
            info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            info.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
            info.pSetLayouts    = setLayouts.data();
            VkPushConstantRange pushConstant{};
            if (compiled->reflection.pushConstant) {
                pushConstant = *compiled->reflection.pushConstant;
                info.pushConstantRangeCount = 1;
                info.pPushConstantRanges    = &pushConstant;
            }
            if (vkCreatePipelineLayout(context->device, &info, nullptr,
                                       &desc.layout) != VK_SUCCESS) {
                throw std::runtime_error("RenderGraph: failed to create pipeline layout for pass '" +
                                         node.pass->passName + "'.");
            }
            node.ownsPipelineLayout = true;
        }

        node.pipelineRuntime = runtime;
        node.pass->pipelineRuntime_ = runtime;

        // 材质 pass 在 Execute 内通过同一个 Runtime 按材质选择变体。
        // 其余 pass 在这里预热默认变体，错误会在 Compile 阶段直接报告。
        if (shader.autoBind) {
            psoManager->getOrCreate(desc);
        }
    }
}

void RenderGraph::AllocatePhysicalResources() {
    for (RGTextureHandle h = 1; h < textureInfos.size(); ++h) {
        const RGTextureInfo& info = textureInfos[h];
        if (info.name.empty()) continue;  // 已被释放或未使用

        PhysicalResource& res = physicalResources[h];
        if (res.imported) continue;
        if (res.image != VK_NULL_HANDLE) continue;  // 已分配

        VkImage image = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
        createImageVMA(info.desc.width, info.desc.height, info.desc.arrayLayers,
                       info.desc.mipLevels, info.desc.sampleCount, info.desc.format,
                       VK_IMAGE_TILING_OPTIMAL, info.desc.usage,
                       VMA_MEMORY_USAGE_GPU_ONLY, image, allocation);

        VkImageView view = createImageView(context->device, image, info.desc.format,
                                           GetAspectFlags(info.desc.format),
                                           info.desc.mipLevels, info.desc.arrayLayers);

        res.image      = image;
        res.imageView  = view;
        res.allocation = allocation;
        res.imported   = false;
    }
}

void RenderGraph::FreePhysicalResources() {
    VkDevice device = context ? context->device : VK_NULL_HANDLE;
    for (auto& res : physicalResources) {
        if (res.imported) continue;
        if (res.imageView != VK_NULL_HANDLE) {
            vkDestroyImageView(device, res.imageView, nullptr);
            res.imageView = VK_NULL_HANDLE;
        }
        if (res.image != VK_NULL_HANDLE && res.allocation != VK_NULL_HANDLE) {
            vmaDestroyImage(g_vmaAllocator, res.image, res.allocation);
            res.image = VK_NULL_HANDLE;
            res.allocation = VK_NULL_HANDLE;
        }
    }
    // 释放后 layout 跟踪需要重置（下次重新过渡）
    for (auto& s : resourceStates) s = {};
}

// ------------------------------------------------------------------
// 资源依赖推导 + 拓扑排序
// ------------------------------------------------------------------

void RenderGraph::BuildDependencyGraph() {
    const int n = static_cast<int>(passes.size());
    adjacency_.assign(n, {});
    inDegree_.assign(n, 0);

    // Per-resource lastProducer：上一个触碰该资源的 pass 索引
    std::vector<int> lastProducer(textureInfos.size(), -1);

    auto addEdge = [&](int from, int to) {
        if (from < 0 || to < 0 || from == to) return;
        for (int existing : adjacency_[from]) {
            if (existing == to) return;  // 已存在，去重
        }
        adjacency_[from].push_back(to);
        inDegree_[to]++;
    };

    auto touchResource = [&](int passIdx, RGTextureHandle handle) {
        if (handle == kInvalidRGTextureHandle || handle >= textureInfos.size()) return;
        int prev = lastProducer[handle];
        addEdge(prev, passIdx);
        lastProducer[handle] = passIdx;
    };

    for (int pi = 0; pi < n; ++pi) {
        const auto& b = passes[pi].builder;
        for (const auto& r : b.GetReads())        touchResource(pi, r.handle);
        for (const auto& c : b.GetColorOutputs()) touchResource(pi, c.handle);
        for (const auto& d : b.GetDepthOutputs()) touchResource(pi, d.handle);
    }

    // Merge semantic ordering requested through InsertBefore/InsertAfter with
    // the automatically inferred resource dependencies.
    for (const auto& dependency : explicitDependencies_) {
        int beforeIndex = -1;
        int afterIndex = -1;
        for (int pi = 0; pi < n; ++pi) {
            if (passes[pi].pass == dependency.before) beforeIndex = pi;
            if (passes[pi].pass == dependency.after) afterIndex = pi;
        }
        if (beforeIndex < 0 || afterIndex < 0) {
            throw std::runtime_error(
                "RenderGraph: explicit execution dependency references an "
                "unregistered pass during compilation.");
        }
        addEdge(beforeIndex, afterIndex);
    }

    TopologicalSort();
}

void RenderGraph::TopologicalSort() {
    const int n = static_cast<int>(passes.size());
    executionOrder_.clear();
    executionOrder_.reserve(n);

    std::queue<int> q;
    for (int i = 0; i < n; ++i)
        if (inDegree_[i] == 0) q.push(i);

    while (!q.empty()) {
        int u = q.front(); q.pop();
        executionOrder_.push_back(u);
        for (int v : adjacency_[u])
            if (--inDegree_[v] == 0) q.push(v);
    }

    if (static_cast<int>(executionOrder_.size()) != n) {
        executionOrder_.clear();
        throw std::runtime_error(
            "RenderGraph: cycle detected while combining resource and "
            "explicit pass execution dependencies.");
    }
}

// ------------------------------------------------------------------
// Barrier insertion: 跟踪 per-resource (layout, stage, access)，统一发 barrier2。
// ------------------------------------------------------------------

void RenderGraph::InsertBarriers(VkCommandBuffer cmd, const RGPassNode& node) {
    barrierScratch.clear();

    auto addBarrier = [&](RGTextureHandle handle, VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess,
                          VkImageLayout newLayout) {
        if (handle == kInvalidRGTextureHandle || handle >= physicalResources.size()) return;
        const PhysicalResource& phys = physicalResources[handle];
        if (phys.image == VK_NULL_HANDLE) return;

        const RGTextureInfo& info = textureInfos[handle];
        ResourceState& prev = resourceStates[handle];

        VkPipelineStageFlags2 srcStage = prev.stage;
        VkAccessFlags2        srcAccess = prev.access;
        if (prev.layout == VK_IMAGE_LAYOUT_UNDEFINED) {
            srcStage  = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
            srcAccess = VK_ACCESS_2_NONE;
        }

        VkImageMemoryBarrier2 barrier{};
        barrier.sType                       = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barrier.srcStageMask                = srcStage;
        barrier.srcAccessMask               = srcAccess;
        barrier.dstStageMask                = dstStage;
        barrier.dstAccessMask               = dstAccess;
        barrier.oldLayout                   = prev.layout;
        barrier.newLayout                   = newLayout;
        barrier.srcQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
        barrier.image                       = phys.image;
        barrier.subresourceRange.aspectMask = GetAspectFlags(info.desc.format);
        barrier.subresourceRange.baseMipLevel   = 0;
        barrier.subresourceRange.levelCount     = info.desc.mipLevels;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount     = info.desc.arrayLayers;

        barrierScratch.push_back(barrier);

        prev.layout = newLayout;
        prev.stage  = dstStage;
        prev.access = dstAccess;
    };

    for (const auto& c : node.builder.GetColorOutputs()) {
        addBarrier(c.handle,
                   VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                   VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                   VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    }
    for (const auto& d : node.builder.GetDepthOutputs()) {
        addBarrier(d.handle,
                   VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                       VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                   VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                   VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    }
    for (const auto& r : node.builder.GetReads()) {
        VkImageLayout readLayout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;
        if (r.handle != kInvalidRGTextureHandle && r.handle < textureInfos.size()) {
            const VkImageAspectFlags aspect = GetAspectFlags(textureInfos[r.handle].desc.format);
            if (aspect & VK_IMAGE_ASPECT_DEPTH_BIT) {
                readLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
            }
        }
        addBarrier(r.handle, r.stage, r.access, readLayout);
    }

    if (!barrierScratch.empty()) {
        VkDependencyInfo dep{};
        dep.sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep.imageMemoryBarrierCount  = static_cast<uint32_t>(barrierScratch.size());
        dep.pImageMemoryBarriers     = barrierScratch.data();
        vkCmdPipelineBarrier2(cmd, &dep);
    }
}

// ------------------------------------------------------------------
// Dynamic Rendering: 根据 pass 输出组装 VkRenderingInfo
// ------------------------------------------------------------------

void RenderGraph::BeginRendering(VkCommandBuffer cmd, const RGPassNode& node) {
    VkExtent2D renderExtent = {0, 0};

    colorAttachmentScratch.clear();
    for (const auto& c : node.builder.GetColorOutputs()) {
        if (c.handle == kInvalidRGTextureHandle || c.handle >= physicalResources.size()) continue;
        const PhysicalResource& phys = physicalResources[c.handle];
        if (phys.image == VK_NULL_HANDLE) continue;
        const RGTextureInfo& info = textureInfos[c.handle];

        if (renderExtent.width == 0) {
            renderExtent.width  = info.desc.width;
            renderExtent.height = info.desc.height;
        }

        VkRenderingAttachmentInfo attach{};
        attach.sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        attach.imageView   = phys.imageView;
        attach.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        attach.loadOp      = c.attachment.loadOp;
        attach.storeOp     = c.attachment.storeOp;
        attach.clearValue  = c.attachment.clearValue;
        colorAttachmentScratch.push_back(attach);
    }

    std::optional<VkRenderingAttachmentInfo> depthAttachment;
    for (const auto& d : node.builder.GetDepthOutputs()) {
        if (d.handle == kInvalidRGTextureHandle || d.handle >= physicalResources.size()) continue;
        const PhysicalResource& phys = physicalResources[d.handle];
        if (phys.image == VK_NULL_HANDLE) continue;
        const RGTextureInfo& info = textureInfos[d.handle];

        if (renderExtent.width == 0) {
            renderExtent.width  = info.desc.width;
            renderExtent.height = info.desc.height;
        }

        VkRenderingAttachmentInfo attach{};
        attach.sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        attach.imageView   = phys.imageView;
        attach.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        attach.loadOp      = d.attachment.loadOp;
        attach.storeOp     = d.attachment.storeOp;
        attach.clearValue  = d.attachment.clearValue;
        depthAttachment = attach;
        break;  // 仅支持一张 depth attachment
    }

    // layerCount 取该 pass 第一个 color/depth output 的 arrayLayers；
    // 无输出时默认 1（这种情况不应发生）。
    uint32_t layerCount = 1;
    for (const auto& c : node.builder.GetColorOutputs()) {
        if (c.handle != kInvalidRGTextureHandle && c.handle < textureInfos.size()) {
            layerCount = textureInfos[c.handle].desc.arrayLayers;
            break;
        }
    }
    if (layerCount == 1) {
        for (const auto& d : node.builder.GetDepthOutputs()) {
            if (d.handle != kInvalidRGTextureHandle && d.handle < textureInfos.size()) {
                layerCount = textureInfos[d.handle].desc.arrayLayers;
                break;
            }
        }
    }

    VkRenderingInfo info{};
    info.sType                 = VK_STRUCTURE_TYPE_RENDERING_INFO;
    info.renderArea.offset     = {0, 0};
    info.renderArea.extent     = renderExtent;
    info.layerCount            = layerCount;
    info.colorAttachmentCount  = static_cast<uint32_t>(colorAttachmentScratch.size());
    info.pColorAttachments     = colorAttachmentScratch.data();
    if (depthAttachment.has_value()) {
        info.pDepthAttachment  = &depthAttachment.value();
    }

    vkCmdBeginRendering(cmd, &info);
}

void RenderGraph::BindPassPipeline(VkCommandBuffer cmd,
                                   const FrameContext& frame,
                                   const RGResources& resources,
                                   RGPassNode& node) {
    if (!node.pass || !node.pipelineRuntime) return;
    const PassShaderDesc& shader = node.builder.GetShaderDesc();
    if (shader.autoBind) {
        node.pass->BindShaderVariant(cmd);
    }

    VkPipelineLayout layout = node.pipelineRuntime->baseDesc.layout;
    if (node.ownsPipelineLayout && frame.frameSet.isValid() &&
        frameSetLayout != VK_NULL_HANDLE) {
        ext::vkCmdSetDescriptorBufferOffsetsEXT(
            cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1,
            &frame.frameSet.bufferIndex, &frame.frameSet.offset);
    }

    if (descManager) {
        for (auto& resourceSet : node.autoResourceSets) {
            if (!resourceSet.set.isValid()) continue;
            for (const auto& binding : resourceSet.textureBindings) {
                VkDescriptorImageInfo image{};
                image.imageView = resources.GetImageView(binding.handle);
                image.imageLayout =
                    (GetAspectFlags(GetTextureDesc(binding.handle).format) &
                     VK_IMAGE_ASPECT_DEPTH_BIT)
                    ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
                    : VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;
                if (binding.type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) {
                    image.sampler = resourceSet.usesComparisonSampler
                        ? defaultComparisonSampler
                        : defaultPassSampler;
                }
                descManager->writeImage(resourceSet.layoutId, resourceSet.set,
                                        binding.binding, image, binding.type);
            }
            ext::vkCmdSetDescriptorBufferOffsetsEXT(
                cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout,
                resourceSet.setIndex, 1,
                &resourceSet.set.bufferIndex, &resourceSet.set.offset);
        }
    }

    VkExtent2D extent{0, 0};
    const auto& colors = node.builder.GetColorOutputs();
    const auto& depths = node.builder.GetDepthOutputs();
    if (!colors.empty()) {
        const auto& d = GetTextureDesc(colors.front().handle);
        extent = {d.width, d.height};
    } else if (!depths.empty()) {
        const auto& d = GetTextureDesc(depths.front().handle);
        extent = {d.width, d.height};
    }
    if (extent.width == 0 || extent.height == 0) return;

    VkViewport viewport{};
    viewport.width    = static_cast<float>(extent.width);
    viewport.height   = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{{0, 0}, extent};
    vkCmdSetScissor(cmd, 0, 1, &scissor);
}

// ------------------------------------------------------------------
// Present transition: 把 imported color attachment 过渡到 PRESENT_SRC_KHR
// ------------------------------------------------------------------

void RenderGraph::EmitPresentTransitions(VkCommandBuffer cmd, const RGPassNode& node) {
    presentBarrierScratch.clear();

    for (const auto& c : node.builder.GetColorOutputs()) {
        if (c.handle == kInvalidRGTextureHandle || c.handle >= physicalResources.size()) continue;
        const PhysicalResource& phys = physicalResources[c.handle];
        if (!phys.imported) continue;
        if (phys.image == VK_NULL_HANDLE) continue;

        const RGTextureInfo& info = textureInfos[c.handle];

        VkImageMemoryBarrier2 barrier{};
        barrier.sType                       = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barrier.srcStageMask                = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        barrier.srcAccessMask               = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstStageMask                = VK_PIPELINE_STAGE_2_NONE;
        barrier.dstAccessMask               = VK_ACCESS_2_NONE;
        barrier.oldLayout                   = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barrier.newLayout                   = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        barrier.srcQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
        barrier.image                       = phys.image;
        barrier.subresourceRange.aspectMask = GetAspectFlags(info.desc.format);
        barrier.subresourceRange.baseMipLevel   = 0;
        barrier.subresourceRange.levelCount     = info.desc.mipLevels;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount     = info.desc.arrayLayers;
        presentBarrierScratch.push_back(barrier);

        ResourceState& state = resourceStates[c.handle];
        state.layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        state.stage  = VK_PIPELINE_STAGE_2_NONE;
        state.access = VK_ACCESS_2_NONE;
    }

    if (!presentBarrierScratch.empty()) {
        VkDependencyInfo dep{};
        dep.sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep.imageMemoryBarrierCount = static_cast<uint32_t>(presentBarrierScratch.size());
        dep.pImageMemoryBarriers    = presentBarrierScratch.data();
        vkCmdPipelineBarrier2(cmd, &dep);
    }
}

// ------------------------------------------------------------------
// PerFrame imported 资源生命周期：Execute 末尾自动重置 layout 跟踪
// ------------------------------------------------------------------

void RenderGraph::ResetPerFrameImportedStates() {
    for (RGTextureHandle h = 1; h < physicalResources.size(); ++h) {
        const PhysicalResource& phys = physicalResources[h];
        if (phys.imported && phys.importPolicy == RGImportPolicy::PerFrame) {
            resourceStates[h] = {};
        }
    }
}

// ------------------------------------------------------------------
// Execute: 按资源依赖推导的拓扑序执行
// ------------------------------------------------------------------

void RenderGraph::Execute(VkCommandBuffer cmd, const FrameContext& frame) {
    // Execute 阶段的物理资源只读访问器，逐 pass 透传。
    // 契约见 RGResources 头注释：pass 只允许在 Execute 内解析物理资源。
    const RGResources resources(this);
    for (int idx : executionOrder_) {
        auto& node = passes[idx];
        if (!node.pass || !node.pass->enabled) continue;

        InsertBarriers(cmd, node);
        BeginRendering(cmd, node);
        BindPassPipeline(cmd, frame, resources, node);
        node.pass->Execute(cmd, frame, resources);
        vkCmdEndRendering(cmd);

        EmitPresentTransitions(cmd, node);
    }

    ResetPerFrameImportedStates();
}

} // namespace engine
