#include "app/Window.h"

#include <stdexcept>

namespace app {

void Window::init(int width, int height, const std::string& title) {
    glfwInit();

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    window = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
    glfwSetWindowUserPointer(window, this);
    glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);
}

void Window::cleanup() {
    if (window) {
        glfwDestroyWindow(window);
    }
    glfwTerminate();
}

void Window::createSurface(VkInstance instance) {
    if (glfwCreateWindowSurface(instance, window, nullptr, &surface) != VK_SUCCESS) {
        throw std::runtime_error("failed to create window surface!");
    }
}

void Window::destroySurface(VkInstance instance) {
    if (surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(instance, surface, nullptr);
        surface = VK_NULL_HANDLE;
    }
}

std::vector<const char*> Window::getRequiredInstanceExtensions() const {
    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
    return std::vector<const char*>(glfwExtensions, glfwExtensions + glfwExtensionCount);
}

bool Window::shouldClose() const {
    return glfwWindowShouldClose(window);
}

void Window::pollEvents() const {
    glfwPollEvents();
}

void Window::waitEvents() const {
    glfwWaitEvents();
}

std::pair<int, int> Window::getFramebufferSize() const {
    int width, height;
    glfwGetFramebufferSize(window, &width, &height);
    return {width, height};
}

void Window::framebufferResizeCallback(GLFWwindow* w, int width, int height) {
    (void)width; (void)height;
    auto win = reinterpret_cast<Window*>(glfwGetWindowUserPointer(w));
    if (win) {
        win->framebufferResized = true;
    }
}

} // namespace app
