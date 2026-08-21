#pragma once

#include <vulkan/vulkan.h>
#include <memory>
#include <string>

namespace engine {

class Scene;
class MaterialTemplate;
class VulkanContext;

class GLTFLoader {
public:
    /// 完整场景加载：创建 Scene、解析 glTF、补全默认相机/灯光。
    /// @param path  包含 .gltf 文件的目录，或 .gltf 文件本身（支持相对路径）
    /// @param vkContext  Vulkan 设备上下文
    /// @param materialTemplate  渲染管线对应的材质模板
    static std::unique_ptr<Scene> loadScene(
        const std::string& path,
        VulkanContext* vkContext,
        MaterialTemplate* materialTemplate);

    /// 低级加载：向已有 Scene 填充数据。
    /// @param directory  包含 .gltf 文件的目录，或 .gltf 文件本身
    static void load(const std::string& directory, Scene* scene,
                     VkDevice device, VkPhysicalDevice physicalDevice,
                     VkCommandPool commandPool, VkQueue graphicsQueue,
                     MaterialTemplate* materialTemplate);
};

} // namespace engine
