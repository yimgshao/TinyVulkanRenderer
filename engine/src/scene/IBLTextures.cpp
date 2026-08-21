#include "engine/scene/IBLTextures.h"

#include "engine/VulkanUtils.h"

#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>

namespace engine {

namespace {

// .ibl 容器格式（docs/ibl_design.md 4.2；little-endian）：
//   0  magic char[4] "IBL1" | 4 version u32=1 | 8 kind u32 | 12 width u32
//   16 height u32 | 20 layers u32 | 24 mipLevels u32 | 28 reserved u32
//   32 逐 mip：u64 dataSize + float16 数据（层主序，层内 HxWxC 连续）
struct IBLHeader {
    char     magic[4];
    uint32_t version;
    uint32_t kind;  // 1 = cubemap RGBA16F, 2 = 2D RG16F
    uint32_t width;
    uint32_t height;
    uint32_t layers;
    uint32_t mipLevels;
    uint32_t reserved;
};

constexpr uint32_t kIBLVersion   = 1;
constexpr uint32_t kKindCubeRGBA = 1;
constexpr uint32_t kKind2DRG     = 2;

uint32_t channelsOf(uint32_t kind) { return kind == kKindCubeRGBA ? 4u : 2u; }

VkFormat formatOf(uint32_t kind) {
    return kind == kKindCubeRGBA ? VK_FORMAT_R16G16B16A16_SFLOAT
                                 : VK_FORMAT_R16G16_SFLOAT;
}

} // anonymous namespace

bool IBLTextures::loadOne(const std::string& path, IBLImage& out,
                          VulkanContext* ctx) {
    std::ifstream ifs(path, std::ios::binary | std::ios::ate);
    if (!ifs) {
        std::cerr << "[IBL] cannot open " << path << "\n";
        return false;
    }
    std::vector<uint8_t> file(static_cast<size_t>(ifs.tellg()));
    ifs.seekg(0);
    ifs.read(reinterpret_cast<char*>(file.data()),
             static_cast<std::streamsize>(file.size()));

    if (file.size() < sizeof(IBLHeader)) {
        std::cerr << "[IBL] truncated file " << path << "\n";
        return false;
    }
    IBLHeader h{};
    std::memcpy(&h, file.data(), sizeof(h));

    if (std::memcmp(h.magic, "IBL1", 4) != 0 || h.version != kIBLVersion ||
        (h.kind != kKindCubeRGBA && h.kind != kKind2DRG)) {
        std::cerr << "[IBL] bad header in " << path << "\n";
        return false;
    }
    if (h.kind == kKindCubeRGBA && h.layers != 6) {
        std::cerr << "[IBL] cube layers != 6 in " << path << "\n";
        return false;
    }

    out.width     = h.width;
    out.height    = h.height;
    out.layers    = h.layers;
    out.mipLevels = h.mipLevels;
    out.format    = formatOf(h.kind);
    out.cube      = (h.kind == kKindCubeRGBA);

    // 校验并把逐 mip 数据拼成连续 payload（去掉每 mip 的 u64 长度头）
    const size_t elemSize = channelsOf(h.kind) * sizeof(uint16_t);
    size_t payloadSize = 0;
    {
        size_t off = sizeof(IBLHeader);
        for (uint32_t m = 0; m < h.mipLevels; ++m) {
            if (off + 8 > file.size()) {
                std::cerr << "[IBL] truncated mip table in " << path << "\n";
                return false;
            }
            uint64_t dataSize = 0;
            std::memcpy(&dataSize, file.data() + off, 8);
            const uint64_t expect =
                static_cast<uint64_t>(h.layers) * (h.width >> m) *
                (h.height >> m) * elemSize;
            if (dataSize != expect || off + 8 + dataSize > file.size()) {
                std::cerr << "[IBL] mip " << m << " size mismatch in " << path
                          << " (expect " << expect << ", got " << dataSize << ")\n";
                return false;
            }
            off += 8 + dataSize;
            payloadSize += dataSize;
        }
    }

    std::vector<uint8_t> payload(payloadSize);
    {
        size_t src = sizeof(IBLHeader), dst = 0;
        for (uint32_t m = 0; m < h.mipLevels; ++m) {
            uint64_t dataSize = 0;
            std::memcpy(&dataSize, file.data() + src, 8);
            std::memcpy(payload.data() + dst, file.data() + src + 8, dataSize);
            src += 8 + dataSize;
            dst += dataSize;
        }
    }

    return upload(out, payload.data(), payload.size(), ctx);
}

bool IBLTextures::upload(IBLImage& img, const void* payload,
                         size_t payloadSize, VulkanContext* ctx) {
    // staging buffer
    VkBuffer staging;
    VmaAllocation stagingAlloc;
    createBufferVMA(payloadSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                    VMA_MEMORY_USAGE_CPU_ONLY, staging, stagingAlloc);
    void* mapped = nullptr;
    vmaMapMemory(g_vmaAllocator, stagingAlloc, &mapped);
    std::memcpy(mapped, payload, payloadSize);
    vmaUnmapMemory(g_vmaAllocator, stagingAlloc);

    createImageVMA(img.width, img.height, img.layers, img.mipLevels,
                   VK_SAMPLE_COUNT_1_BIT, img.format, VK_IMAGE_TILING_OPTIMAL,
                   VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                   VMA_MEMORY_USAGE_GPU_ONLY, img.image, img.alloc,
                   img.cube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0);

    VkCommandBuffer cmd =
        beginSingleTimeCommands(ctx->device, ctx->commandPool);

    // UNDEFINED → TRANSFER_DST（全 layer × mip 一把罩）
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = img.image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = img.mipLevels;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = img.layers;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &barrier);

    // 逐 (mip, layer) 一个 copy region；payload 内按 mip 主序、层内连续
    const size_t elemSize = img.cube ? 8 : 4;  // RGBA16F=8B / RG16F=4B
    std::vector<VkBufferImageCopy> regions;
    regions.reserve(img.mipLevels * img.layers);
    size_t mipBase = 0;
    for (uint32_t m = 0; m < img.mipLevels; ++m) {
        const uint32_t mw = img.width >> m;
        const uint32_t mh = img.height >> m;
        const size_t layerSize = static_cast<size_t>(mw) * mh * elemSize;
        for (uint32_t l = 0; l < img.layers; ++l) {
            VkBufferImageCopy region{};
            region.bufferOffset = mipBase + l * layerSize;
            region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.mipLevel = m;
            region.imageSubresource.baseArrayLayer = l;
            region.imageSubresource.layerCount = 1;
            region.imageExtent = {mw, mh, 1};
            regions.push_back(region);
        }
        mipBase += layerSize * img.layers;
    }
    vkCmdCopyBufferToImage(cmd, staging, img.image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           static_cast<uint32_t>(regions.size()),
                           regions.data());

    // TRANSFER_DST → SHADER_READ_ONLY（此后常驻，不进 RenderGraph）
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr,
                         0, nullptr, 1, &barrier);

    endSingleTimeCommands(ctx->device, ctx->commandPool, ctx->graphicsQueue,
                          cmd);
    vmaDestroyBuffer(g_vmaAllocator, staging, stagingAlloc);

    img.view = createImageView(ctx->device, img.image, img.format,
                               VK_IMAGE_ASPECT_COLOR_BIT, img.mipLevels,
                               img.layers,
                               img.cube ? VK_IMAGE_VIEW_TYPE_CUBE
                                        : VK_IMAGE_VIEW_TYPE_2D);
    return true;
}

void IBLTextures::createSampler(VkDevice device, uint32_t maxMipLevels) {
    VkSamplerCreateInfo sci{};
    sci.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sci.magFilter    = VK_FILTER_LINEAR;
    sci.minFilter    = VK_FILTER_LINEAR;
    sci.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.minLod       = 0.0f;
    sci.maxLod       = maxMipLevels > 0 ? static_cast<float>(maxMipLevels - 1)
                                        : 0.0f;
    if (vkCreateSampler(device, &sci, nullptr, &sampler_) != VK_SUCCESS) {
        throw std::runtime_error("IBLTextures: failed to create sampler.");
    }
}

bool IBLTextures::load(const std::string& dir, VulkanContext* ctx) {
    struct Slot { const char* file; IBLImage* img; };
    Slot slots[4] = {
        {"irradiance.ibl", &irradiance_},
        {"prefilter.ibl",  &prefilter_},
        {"env_cube.ibl",   &envCube_},
        {"brdf_lut.ibl",   &brdfLut_},
    };
    for (auto& s : slots) {
        if (!loadOne(dir + "/" + s.file, *s.img, ctx)) {
            return false;  // 已分配资源由调用方 cleanup()
        }
    }
    createSampler(ctx->device, prefilter_.mipLevels);
    hasReal_ = true;
    return true;
}

bool IBLTextures::loadFallback(VulkanContext* ctx) {
    // 1x1x6 黑 cube + 1x1 黑 lut；view 访问器在 hasReal_=false 时回退到这里
    fallbackCube_.width = fallbackCube_.height = 1;
    fallbackCube_.layers = 6;
    fallbackCube_.mipLevels = 1;
    fallbackCube_.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    fallbackCube_.cube = true;
    const uint16_t zerosCube[6 * 4] = {};
    if (!upload(fallbackCube_, zerosCube, sizeof(zerosCube), ctx)) {
        return false;
    }

    fallbackLut_.width = fallbackLut_.height = 1;
    fallbackLut_.layers = 1;
    fallbackLut_.mipLevels = 1;
    fallbackLut_.format = VK_FORMAT_R16G16_SFLOAT;
    fallbackLut_.cube = false;
    const uint16_t zerosLut[2] = {};
    if (!upload(fallbackLut_, zerosLut, sizeof(zerosLut), ctx)) {
        return false;
    }

    createSampler(ctx->device, 1);
    hasReal_ = false;
    return true;
}

void IBLTextures::cleanup(VkDevice device) {
    auto destroy = [device](IBLImage& img) {
        if (img.view != VK_NULL_HANDLE) {
            vkDestroyImageView(device, img.view, nullptr);
            img.view = VK_NULL_HANDLE;
        }
        if (img.image != VK_NULL_HANDLE) {
            vmaDestroyImage(g_vmaAllocator, img.image, img.alloc);
            img.image = VK_NULL_HANDLE;
            img.alloc = VK_NULL_HANDLE;
        }
    };
    destroy(irradiance_);
    destroy(prefilter_);
    destroy(envCube_);
    destroy(brdfLut_);
    destroy(fallbackCube_);
    destroy(fallbackLut_);
    if (sampler_ != VK_NULL_HANDLE) {
        vkDestroySampler(device, sampler_, nullptr);
        sampler_ = VK_NULL_HANDLE;
    }
    hasReal_ = false;
}

} // namespace engine
