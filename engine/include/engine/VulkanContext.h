#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <optional>

namespace engine {

struct QueueFamilyIndices {
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> presentFamily;

    bool isComplete() const {
        return graphicsFamily.has_value() && presentFamily.has_value();
    }
};

struct SwapChainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

class VulkanContext {
public:
    void createInstance(const std::vector<const char*>& extensions);
    void createDevice(VkSurfaceKHR surface);
    void cleanup();

    VkInstance instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkQueue presentQueue = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkSampleCountFlagBits msaaSamples = VK_SAMPLE_COUNT_1_BIT;

    uint32_t graphicsFamily = 0;
    uint32_t presentFamily = 0;

private:
    void setupDebugMessenger();
    void pickPhysicalDevice(VkSurfaceKHR surface);
    void createLogicalDevice(VkSurfaceKHR surface);
    void createCommandPool();

public:
    SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device, VkSurfaceKHR surface) const;
    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface) const;

private:
    bool checkValidationLayerSupport() const;
    std::vector<const char*> getRequiredExtensions(const std::vector<const char*>& windowExtensions) const;
    void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo) const;
    bool isDeviceSuitable(VkPhysicalDevice device, VkSurfaceKHR surface) const;
    bool checkDeviceExtensionSupport(VkPhysicalDevice device) const;
    VkSampleCountFlagBits getMaxUsableSampleCount() const;
};

} // namespace engine
