#pragma once

#include "engine/descriptor/DescriptorBufferHeap.h"
#include "engine/descriptor/DescriptorSetWriter.h"

#include <vulkan/vulkan.h>
#include <cstdint>
#include <memory>
#include <vector>

namespace engine {

// ------------------------------------------------------------------
// VK_EXT_descriptor_buffer function pointers (loaded by init)
// ------------------------------------------------------------------
namespace ext {
    extern PFN_vkGetDescriptorSetLayoutSizeEXT vkGetDescriptorSetLayoutSizeEXT;
    extern PFN_vkGetDescriptorSetLayoutBindingOffsetEXT vkGetDescriptorSetLayoutBindingOffsetEXT;
    extern PFN_vkGetDescriptorEXT vkGetDescriptorEXT;
    extern PFN_vkCmdBindDescriptorBuffersEXT vkCmdBindDescriptorBuffersEXT;
    extern PFN_vkCmdSetDescriptorBufferOffsetsEXT vkCmdSetDescriptorBufferOffsetsEXT;
}

/**
 * LayoutId -- DescriptorSetManager 中已注册布局的强类型句柄。
 *
 * 数值上等于该布局在 descriptor buffer 绑定数组中的 index（也就是
 * DescriptorSetHandle::bufferIndex），按注册顺序连续递增。
 */
enum class LayoutId : uint32_t {};
inline constexpr LayoutId kInvalidLayoutId{ 0xFFFFFFFFu };

// ------------------------------------------------------------------
// Lightweight handle replacing VkDescriptorSet
// ------------------------------------------------------------------
struct DescriptorSetHandle {
    uint32_t bufferIndex = 0;
    uint64_t offset      = UINT64_MAX;

    bool isValid() const { return offset != UINT64_MAX; }
    static DescriptorSetHandle invalid() { return DescriptorSetHandle{}; }
};

/**
 * ============================================================================
 *               DescriptorSetManager 工业级全流程用法示例
 * ============================================================================
 * 
 * 示例场景：
 * - 注册一个全局池 (Set 0): 内含 1 个 Uniform Buffer (相机矩阵)
 * - 注册一个材质池 (Set 1): 内含 1 个纯材质 UBO 属性 + 2 张贴图 (颜色、法线)
 * - 运行时动态分配 2 种不同的材质 (木头、石头)
 * - 在渲染循环中，通过极速切换 Offset 实现高能多材质绘制。
 *
 * ----------------------------------------------------------------------------
 * 1. 对应 Shader 端的结构声明 (GLSL 视角)
 * ----------------------------------------------------------------------------
 * // Set 0: 全局帧数据
 * layout(set = 0, binding = 0) uniform FrameData { mat4 viewProj; } uboFrame;
 * 
 * // Set 1: 物体材质数据
 * layout(set = 1, binding = 0) uniform MaterialProps { vec4 baseColorFactor; } uboMat;
 * layout(set = 1, binding = 1) sampler2D texDiffuse;
 * layout(set = 1, binding = 2) sampler2D texNormal;
 *
 * ----------------------------------------------------------------------------
 * 2. C++ 业务层生命周期实现代码
 * ----------------------------------------------------------------------------
 * void runEngineDescriptorBufferExample(VkDevice device, VkPhysicalDevice physDevice, 
 *                                      VkCommandBuffer cmd, VkPipelineLayout pipelineLayout,
 *                                      VkDescriptorSetLayout globalLayout, VkDescriptorSetLayout materialLayout) 
 * {
 *     // ---------------------------------------------------------
 *     // [步骤 A] 创建并初始化中央管理器
 *     // ---------------------------------------------------------
 *     engine::DescriptorSetManager descManager;
 *     descManager.init(device, physDevice);
 *
 *     // ---------------------------------------------------------
 *     // [步骤 B] 基础设施建设：注册布局 (通常在引擎初始化/场景加载阶段)
 *     // ---------------------------------------------------------
 *     // 注册全局池，最大容纳 2 个全局描述符集
 *     engine::LayoutId globalPoolId = descManager.registerLayout(globalLayout, 2);
 *     // 注册材质池，最大容纳 1000 个不同的材质格子
 *     engine::LayoutId materialPoolId = descManager.registerLayout(materialLayout, 1000);
 *
 *     // ---------------------------------------------------------
 *     // [步骤 C] 空间分配与数据填充：制作具体材质 (动态/运行时阶段)
 *     // ---------------------------------------------------------
 *     
 *     // 1. 配置全局帧数据 (Set 0 对应的槽位)
 *     engine::DescriptorSetHandle globalSet = descManager.allocate(globalPoolId);
 *     VkDescriptorBufferInfo cameraBufferInfo{ myCamBuffer, 0, sizeof(CameraData) };
 *     descManager.writeBuffer(globalPoolId, globalSet, 0, cameraBufferInfo); // 写入 Binding 0
 *
 *     // 2. 实例化第一种材质 ——【木头材质】(租用一个完整的材质格子)
 *     engine::DescriptorSetHandle woodMaterial = descManager.allocate(materialPoolId);
 *     descManager.writeBuffer(materialPoolId, woodMaterial, 0, woodPropBufferInfo);  // Binding 0: 基础属性
 *     descManager.writeImage(materialPoolId, woodMaterial, 1, woodDiffuseImageInfo); // Binding 1: 扩散纹理
 *     descManager.writeImage(materialPoolId, woodMaterial, 2, woodNormalImageInfo);  // Binding 2: 法线纹理
 *
 *     // 3. 实例化第二种材质 ——【石头材质】(租用另一个完整的材质格子)
 *     engine::DescriptorSetHandle stoneMaterial = descManager.allocate(materialPoolId);
 *     descManager.writeBuffer(materialPoolId, stoneMaterial, 0, stonePropBufferInfo);  // Binding 0: 基础属性
 *     descManager.writeImage(materialPoolId, stoneMaterial, 1, stoneDiffuseImageInfo); // Binding 1: 扩散纹理
 *     descManager.writeImage(materialPoolId, stoneMaterial, 2, stoneNormalImageInfo);  // Binding 2: 法线纹理
 *
 *     // ---------------------------------------------------------
 *     // [步骤 D] 核心渲染主循环 (每一帧绘制阶段)
 *     // ---------------------------------------------------------
 *     
 *     // 1. 全局大基准地址绑定：一刀切地把系统里所有的硬件大缓冲全部加载到显卡寄存器
 *     auto bindings = descManager.getBufferBindings();
 *     std::vector<VkDescriptorBufferBindingInfoEXT> bindingInfos(bindings.size());
 *     for (size_t i = 0; i < bindings.size(); ++i) {
 *         bindingInfos[i].sType = VK_STRUCTURE_TYPE_DESCRIPTOR_BUFFER_BINDING_INFO_EXT;
 *         bindingInfos[i].pNext = nullptr;
 *         bindingInfos[i].address = bindings[i].address;
 *         bindingInfos[i].usage = VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT;
 *     }
 *     // 硬件通道建立，此时 GPU 已知道每个 Pool 的首地址。开销极低。
 *     engine::ext::vkCmdBindDescriptorBuffersEXT(cmd, static_cast<uint32_t>(bindingInfos.size()), bindingInfos.data());
 *
 *     // 2. 绑定 Set 0 (全局相机数据)，此后所有 Draw 都能看见它
 *     engine::ext::vkCmdSetDescriptorBufferOffsetsEXT(
 *         cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
 *         0, 1, &globalSet.bufferIndex, &globalSet.offset
 *     );
 *
 *     // 3. 【绘制木头物体】 —— 极速修改 Offset 指针到木头格子
 *     engine::ext::vkCmdSetDescriptorBufferOffsetsEXT(
 *         cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
 *         1, 1, &woodMaterial.bufferIndex, &woodMaterial.offset // 指向 Set 1 槽位
 *     );
 *     vkCmdDrawIndexed(cmd, woodIndexCount, 1, 0, 0, 0); // 木头渲染成功
 *
 *     // 4. 【绘制石头物体】 —— 仅仅修改 Offset 寄存器值，切换到石头格子，没有任何内存拷贝
 *     engine::ext::vkCmdSetDescriptorBufferOffsetsEXT(
 *         cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
 *         1, 1, &stoneMaterial.bufferIndex, &stoneMaterial.offset // 切换 Set 1 槽位
 *     );
 *     vkCmdDrawIndexed(cmd, stoneIndexCount, 1, 0, 0, 0); // 石头渲染成功
 *
 *     // ---------------------------------------------------------
 *     // [步骤 E] 资源生命周期结束与销毁
 *     // ---------------------------------------------------------
 *     descManager.free(materialPoolId, woodMaterial);  // 归还格子给材质池空闲队列
 *     descManager.free(materialPoolId, stoneMaterial);
 *     descManager.free(globalPoolId, globalSet);
 *     
 *     descManager.cleanup(); // 统一轰塌并释放底层的多个 VkBuffer
 * }
 */
class DescriptorSetManager {
public:
    DescriptorSetManager() = default;
    ~DescriptorSetManager() = default;

    DescriptorSetManager(const DescriptorSetManager&) = delete;
    DescriptorSetManager& operator=(const DescriptorSetManager&) = delete;

    void init(VkDevice device, VkPhysicalDevice physicalDevice);
    void cleanup();

    /// 注册一个 descriptor set layout，返回它的 LayoutId（同时即 bufferIndex）。
    /// 同一个 layout 重复注册会得到不同 LayoutId（认为是独立的池子）。
    LayoutId registerLayout(VkDescriptorSetLayout layout, uint32_t maxSets);

    DescriptorSetHandle allocate(LayoutId id);
    void free(LayoutId id, DescriptorSetHandle handle);

    void writeBuffer(LayoutId id, DescriptorSetHandle handle,
                     uint32_t binding, const VkDescriptorBufferInfo& info);
    /// type 必须与 set layout 中该 binding 声明的类型一致
    /// （COMBINED_IMAGE_SAMPLER / SAMPLED_IMAGE / SAMPLER）。
    void writeImage(LayoutId id, DescriptorSetHandle handle,
                    uint32_t binding, const VkDescriptorImageInfo& info,
                    VkDescriptorType type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

    uint32_t getBufferIndex(LayoutId id) const;

    // Binding info for vkCmdBindDescriptorBuffersEXT。
    // 返回顺序与 LayoutId 数值一致（= bufferIndex），调用方可直接按数组下标绑定。
    struct BufferBindingInfo {
        VkBuffer        buffer;
        VkDeviceAddress address;
    };
    std::vector<BufferBindingInfo> getBufferBindings() const;

private:
    struct LayoutEntry {
        VkDescriptorSetLayout                 layout = VK_NULL_HANDLE;
        std::unique_ptr<DescriptorBufferHeap> heap;
        std::unique_ptr<DescriptorSetWriter>  writer;
    };

    VkDevice         device         = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;

    // 按 LayoutId 直接索引（LayoutId 的 underlying = vector index）
    std::vector<LayoutEntry> layouts;
};

} // namespace engine
