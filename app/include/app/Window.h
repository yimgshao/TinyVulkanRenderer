#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <vector>
#include <string>

namespace app {

class Window {
public:
    void init(int width, int height, const std::string& title);
    void cleanup();

    void createSurface(VkInstance instance);
    void destroySurface(VkInstance instance);
    std::vector<const char*> getRequiredInstanceExtensions() const;

    bool shouldClose() const;
    void pollEvents() const;
    void waitEvents() const;
    std::pair<int, int> getFramebufferSize() const;

    GLFWwindow* getHandle() const { return window; }
    VkSurfaceKHR getSurface() const { return surface; }

    bool framebufferResized = false;

private:
    GLFWwindow* window = nullptr;
    VkSurfaceKHR surface = VK_NULL_HANDLE;

    static void framebufferResizeCallback(GLFWwindow* window, int width, int height);
};

} // namespace app
