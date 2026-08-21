#pragma once

#include "engine/VulkanContext.h"

#include <vulkan/vulkan.h>
#include <functional>
#include <vector>

namespace engine {

/**
 * Manages the Vulkan swap chain and per-image views.
 *
 * Render passes, framebuffers, and surface attachments are owned by the
 * renderer / render pass implementation, not by SwapChain.
 */
class SwapChain {
public:
    using FramebufferSizeFn = std::function<std::pair<int, int>()>;

    void init(VulkanContext* ctx, VkSurfaceKHR surface,
              FramebufferSizeFn getSize);
    void cleanup();

    void recreate();

    bool acquireNextImage(uint32_t& imageIndex,
                          VkSemaphore signalSemaphore);
    bool present(uint32_t imageIndex, VkSemaphore waitSemaphore);

    // Getters
    const std::vector<VkImage>& getImages() const { return images; }
    const std::vector<VkImageView>& getImageViews() const {
        return imageViews;
    }
    VkExtent2D getExtent() const { return extent; }
    VkFormat getFormat() const { return format; }
    uint32_t getImageCount() const {
        return static_cast<uint32_t>(images.size());
    }

private:
    VulkanContext* context = nullptr;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    FramebufferSizeFn getFramebufferSize;

    // Swap chain
    VkSwapchainKHR swapChain = VK_NULL_HANDLE;
    std::vector<VkImage> images;
    VkFormat format;
    VkExtent2D extent;
    std::vector<VkImageView> imageViews;

    // Lifecycle helpers
    void createSwapChain();
    void createImageViews();
    void cleanupInternal();

    // Selection helpers
    VkSurfaceFormatKHR chooseSwapSurfaceFormat(
        const std::vector<VkSurfaceFormatKHR>& available);
    VkPresentModeKHR chooseSwapPresentMode(
        const std::vector<VkPresentModeKHR>& available);
    VkExtent2D chooseSwapExtent(
        const VkSurfaceCapabilitiesKHR& capabilities);
};

} // namespace engine
