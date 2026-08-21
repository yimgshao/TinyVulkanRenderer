#include "engine/scene/Texture.h"
#include "engine/VulkanUtils.h"

#include <stb_image.h>
#include <cmath>
#include <stdexcept>

namespace engine {

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
    vmaDestroyImage(g_vmaAllocator, textureImage, allocation);
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
