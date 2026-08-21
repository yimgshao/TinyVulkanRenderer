#include "engine/SwapChain.h"
#include "engine/VulkanUtils.h"

#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace engine {

void SwapChain::init(VulkanContext* ctx, VkSurfaceKHR surf,
                     FramebufferSizeFn getSize) {
    context = ctx;
    surface = surf;
    getFramebufferSize = std::move(getSize);

    createSwapChain();
    createImageViews();
}

void SwapChain::cleanup() {
    cleanupInternal();
}

void SwapChain::recreate() {
    vkDeviceWaitIdle(context->device);

    cleanupInternal();

    createSwapChain();
    createImageViews();
}

bool SwapChain::acquireNextImage(uint32_t& imageIndex,
                                 VkSemaphore signalSemaphore) {
    VkResult result = vkAcquireNextImageKHR(
        context->device, swapChain, UINT64_MAX, signalSemaphore,
        VK_NULL_HANDLE, &imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        return false;
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("failed to acquire swap chain image!");
    }
    return true;
}

bool SwapChain::present(uint32_t imageIndex, VkSemaphore waitSemaphore) {
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &waitSemaphore;

    VkSwapchainKHR swapChains[] = {swapChain};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &imageIndex;

    VkResult result = vkQueuePresentKHR(context->presentQueue, &presentInfo);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        return false;
    } else if (result != VK_SUCCESS) {
        throw std::runtime_error("failed to present swap chain image!");
    }
    return true;
}

// ------------------------------------------------------------------
// Internal creation / destruction
// ------------------------------------------------------------------

void SwapChain::cleanupInternal() {
    VkDevice device = context->device;

    for (auto imageView : imageViews) {
        vkDestroyImageView(device, imageView, nullptr);
    }
    imageViews.clear();

    if (swapChain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device, swapChain, nullptr);
        swapChain = VK_NULL_HANDLE;
    }
}

void SwapChain::createSwapChain() {
    SwapChainSupportDetails swapChainSupport =
        context->querySwapChainSupport(context->physicalDevice, surface);

    VkSurfaceFormatKHR surfaceFormat =
        chooseSwapSurfaceFormat(swapChainSupport.formats);
    VkPresentModeKHR presentMode =
        chooseSwapPresentMode(swapChainSupport.presentModes);
    VkExtent2D swapExtent = chooseSwapExtent(swapChainSupport.capabilities);

    uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
    if (swapChainSupport.capabilities.maxImageCount > 0 &&
        imageCount > swapChainSupport.capabilities.maxImageCount) {
        imageCount = swapChainSupport.capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = swapExtent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    QueueFamilyIndices indices =
        context->findQueueFamilies(context->physicalDevice, surface);
    uint32_t queueFamilyIndices[] = {indices.graphicsFamily.value(),
                                     indices.presentFamily.value()};

    if (indices.graphicsFamily != indices.presentFamily) {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    } else {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    createInfo.preTransform = swapChainSupport.capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;

    if (vkCreateSwapchainKHR(context->device, &createInfo, nullptr,
                             &swapChain) != VK_SUCCESS) {
        throw std::runtime_error("failed to create swap chain!");
    }

    vkGetSwapchainImagesKHR(context->device, swapChain, &imageCount, nullptr);
    images.resize(imageCount);
    vkGetSwapchainImagesKHR(context->device, swapChain, &imageCount,
                            images.data());

    format = surfaceFormat.format;
    extent = swapExtent;
}

void SwapChain::createImageViews() {
    imageViews.resize(images.size());
    for (uint32_t i = 0; i < images.size(); i++) {
        imageViews[i] =
            createImageView(context->device, images[i], format,
                            VK_IMAGE_ASPECT_COLOR_BIT, 1, 1);
    }
}

// ------------------------------------------------------------------
// Selection helpers
// ------------------------------------------------------------------

VkSurfaceFormatKHR SwapChain::chooseSwapSurfaceFormat(
    const std::vector<VkSurfaceFormatKHR>& availableFormats) {
    for (const auto& availableFormat : availableFormats) {
        if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB &&
            availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return availableFormat;
        }
    }
    return availableFormats[0];
}

VkPresentModeKHR SwapChain::chooseSwapPresentMode(
    const std::vector<VkPresentModeKHR>& availablePresentModes) {
    for (const auto& availablePresentMode : availablePresentModes) {
        if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
            return availablePresentMode;
        }
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D SwapChain::chooseSwapExtent(
    const VkSurfaceCapabilitiesKHR& capabilities) {
    if (capabilities.currentExtent.width !=
        std::numeric_limits<uint32_t>::max()) {
        return capabilities.currentExtent;
    } else {
        auto [width, height] = getFramebufferSize();
        VkExtent2D actualExtent = {static_cast<uint32_t>(width),
                                   static_cast<uint32_t>(height)};
        actualExtent.width = std::clamp(
            actualExtent.width, capabilities.minImageExtent.width,
            capabilities.maxImageExtent.width);
        actualExtent.height = std::clamp(
            actualExtent.height, capabilities.minImageExtent.height,
            capabilities.maxImageExtent.height);
        return actualExtent;
    }
}

} // namespace engine
