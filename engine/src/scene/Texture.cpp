#include "engine/scene/Texture.h"
#include "engine/VulkanUtils.h"

#include <stb_image.h>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace engine {

void Texture::createRgba8(VkImageViewType type, VkExtent3D extent, std::span<const uint8_t> pixels,
                          VkDevice dev, VkPhysicalDevice phys, VkCommandPool pool, VkQueue queue) {
    if (textureImage) throw std::runtime_error("Texture already initialized");
    const bool cube = type == VK_IMAGE_VIEW_TYPE_CUBE;
    const uint32_t layers = cube ? 6 : 1;
    if ((type != VK_IMAGE_VIEW_TYPE_2D && type != VK_IMAGE_VIEW_TYPE_3D && !cube) ||
        !extent.width || !extent.height || !extent.depth ||
        (type != VK_IMAGE_VIEW_TYPE_3D && extent.depth != 1) ||
        (cube && extent.width != extent.height))
        throw std::runtime_error("Invalid RGBA8 texture dimensions");
    uint64_t expected = 4;
    for (uint32_t size : {extent.width, extent.height, extent.depth, layers}) {
        if (expected > std::numeric_limits<uint64_t>::max() / size) throw std::runtime_error("Texture size overflow");
        expected *= size;
    }
    if (pixels.size() != expected) throw std::runtime_error("RGBA8 texture byte count mismatch");
    device = dev; physicalDevice = phys; viewType_ = type; mipLevels = 1;
    VkBuffer staging = VK_NULL_HANDLE; VmaAllocation stagingAlloc = VK_NULL_HANDLE;
    try {
        VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        info.flags = cube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
        info.imageType = type == VK_IMAGE_VIEW_TYPE_3D ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D;
        info.format = VK_FORMAT_R8G8B8A8_UNORM; info.extent = extent;
        info.mipLevels = 1; info.arrayLayers = layers; info.samples = VK_SAMPLE_COUNT_1_BIT;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        VmaAllocationCreateInfo alloc{}; alloc.usage = VMA_MEMORY_USAGE_GPU_ONLY;
        if (vmaCreateImage(g_vmaAllocator, &info, &alloc, &textureImage, &allocation, nullptr) != VK_SUCCESS)
            throw std::runtime_error("Cannot create RGBA8 texture");
        createBufferVMA(expected, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY, staging, stagingAlloc);
        void* mapped = nullptr;
        if (vmaMapMemory(g_vmaAllocator, stagingAlloc, &mapped) != VK_SUCCESS) throw std::runtime_error("Cannot map texture staging");
        std::memcpy(mapped, pixels.data(), pixels.size());
        vmaFlushAllocation(g_vmaAllocator, stagingAlloc, 0, expected);
        vmaUnmapMemory(g_vmaAllocator, stagingAlloc);
        auto cmd = beginSingleTimeCommands(dev, pool);
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.image = textureImage;
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, layers};
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,nullptr,0,nullptr,1,&barrier);
        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT,0,0,layers};
        region.imageExtent = extent;
        vkCmdCopyBufferToImage(cmd, staging, textureImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0,0,nullptr,0,nullptr,1,&barrier);
        endSingleTimeCommands(dev,pool,queue,cmd);
        vmaDestroyBuffer(g_vmaAllocator, staging, stagingAlloc); staging = VK_NULL_HANDLE;
        textureImageView = createImageView(dev,textureImage,info.format,VK_IMAGE_ASPECT_COLOR_BIT,1,layers,type);
        createSampler(dev);
    } catch (...) {
        if (staging) vmaDestroyBuffer(g_vmaAllocator, staging, stagingAlloc);
        cleanup(dev);
        throw;
    }
}

void Texture::createSolid(uint32_t rgba, VkDevice dev, VkPhysicalDevice phys,
                          VkCommandPool pool, VkQueue queue) {
    device = dev; physicalDevice = phys; mipLevels = 1;
    VkBuffer staging = VK_NULL_HANDLE;
    VmaAllocation stagingAllocation = VK_NULL_HANDLE;
    createBufferVMA(4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY, staging, stagingAllocation);
    void* data = nullptr;
    vmaMapMemory(g_vmaAllocator, stagingAllocation, &data);
    std::memcpy(data, &rgba, 4);
    vmaFlushAllocation(g_vmaAllocator, stagingAllocation, 0, 4);
    vmaUnmapMemory(g_vmaAllocator, stagingAllocation);
    createImageVMA(1, 1, 1, 1, VK_SAMPLE_COUNT_1_BIT, VK_FORMAT_R8G8B8A8_UNORM,
                   VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                   VMA_MEMORY_USAGE_GPU_ONLY, textureImage, allocation);
    transitionImageLayout(device, pool, queue, textureImage, VK_FORMAT_R8G8B8A8_UNORM,
                          VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1);
    copyBufferToImage(device, pool, queue, staging, textureImage, 1, 1);
    transitionImageLayout(device, pool, queue, textureImage, VK_FORMAT_R8G8B8A8_UNORM,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1);
    vmaDestroyBuffer(g_vmaAllocator, staging, stagingAllocation);
    textureImageView = createImageView(device, textureImage, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, 1, 1);
    createSampler(device);
}

void Texture::load(const std::string& filepath, VkDevice dev,
                   VkPhysicalDevice phys, VkCommandPool commandPool,
                   VkQueue graphicsQueue, VkFormat format) {
    device = dev;
    physicalDevice = phys;

    int texWidth, texHeight, texChannels;
    stbi_uc* pixels = stbi_load(filepath.c_str(), &texWidth, &texHeight,
                                &texChannels, STBI_rgb_alpha);
    if (!pixels) {
        throw std::runtime_error("failed to load texture image: " + filepath);
    }

    VkDeviceSize imageSize = texWidth * texHeight * 4;
    mipLevels = static_cast<uint32_t>(
                    std::floor(std::log2(std::max(texWidth, texHeight)))) +
                1;

    VkBuffer stagingBuffer;
    VmaAllocation stagingAlloc;
    createBufferVMA(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                    VMA_MEMORY_USAGE_CPU_ONLY, stagingBuffer, stagingAlloc);

    void* data;
    vmaMapMemory(g_vmaAllocator, stagingAlloc, &data);
    memcpy(data, pixels, static_cast<size_t>(imageSize));
    vmaUnmapMemory(g_vmaAllocator, stagingAlloc);

    stbi_image_free(pixels);

    createImageVMA(texWidth, texHeight, 1, mipLevels, VK_SAMPLE_COUNT_1_BIT,
                   format, VK_IMAGE_TILING_OPTIMAL,
                   VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                       VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                       VK_IMAGE_USAGE_SAMPLED_BIT,
                   VMA_MEMORY_USAGE_GPU_ONLY, textureImage, allocation);

    transitionImageLayout(device, commandPool, graphicsQueue, textureImage,
                          format, VK_IMAGE_LAYOUT_UNDEFINED,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, mipLevels);
    copyBufferToImage(device, commandPool, graphicsQueue, stagingBuffer,
                      textureImage, static_cast<uint32_t>(texWidth),
                      static_cast<uint32_t>(texHeight));

    vmaDestroyBuffer(g_vmaAllocator, stagingBuffer, stagingAlloc);

    generateMipmaps(device, physicalDevice, commandPool, graphicsQueue,
                    textureImage, format, texWidth, texHeight, mipLevels);

    textureImageView = createImageView(device, textureImage, format,
                                       VK_IMAGE_ASPECT_COLOR_BIT, mipLevels, 1);
    createSampler(device);
}

void Texture::cleanup(VkDevice dev) {
    vkDestroySampler(dev, textureSampler, nullptr);
    vkDestroyImageView(dev, textureImageView, nullptr);
    if (textureImage) vmaDestroyImage(g_vmaAllocator, textureImage, allocation);
    textureSampler = VK_NULL_HANDLE; textureImageView = VK_NULL_HANDLE;
    textureImage = VK_NULL_HANDLE; allocation = VK_NULL_HANDLE;
}

VkDescriptorImageInfo Texture::descriptorInfo() const {
    VkDescriptorImageInfo info{};
    info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    info.imageView = textureImageView;
    info.sampler = textureSampler;
    return info;
}

void Texture::createSampler(VkDevice dev) {
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physicalDevice, &properties);

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = VK_TRUE;
    samplerInfo.maxAnisotropy = properties.limits.maxSamplerAnisotropy;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = static_cast<float>(mipLevels);
    samplerInfo.mipLodBias = 0.0f;

    if (vkCreateSampler(dev, &samplerInfo, nullptr, &textureSampler) !=
        VK_SUCCESS) {
        throw std::runtime_error("failed to create texture sampler!");
    }
}

} // namespace engine
