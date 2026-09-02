#pragma once

#include "engine/config/Config.h"
#include "engine/renderer/IRenderer.h"
#include "engine/renderer/renderpass/IRenderPass.h"
#include "engine/scene/MaterialTemplate.h"

#include <vulkan/vulkan.h>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

namespace engine {

class Scene;

/**
 * ForwardRenderer -- 经典前向渲染管线。
 *
 * 实现 IRenderer 接口，由 RenderModule 持有并驱动。
 *
 * 通过 RenderGraph 的资源依赖推导自动确定 pass 执行顺序。
 * 子类可重写 buildRenderGraph 插入自定义 pass，无需修改既有 pass 配置。
 *
 * Pass 通过 passes_ 按插入顺序持有，可在运行时按名访问：
 *   auto* fwd = getPass<ForwardPass>("Forward");
 *   fwd->passParams.set("useFog", false);
 */
class ForwardRenderer : public IRenderer {
public:
    /// @param rendererCfg 渲染器配置（shadow.* / passParams.*），空 Config = 全默认
    /// @param materialCfg 材质配置（type / header），空 Config = 全默认
    explicit ForwardRenderer(const Config& rendererCfg = {},
                             const Config& materialCfg = {})
        : rendererCfg_(rendererCfg), materialCfg_(materialCfg) {}

    void init(const FrameContext& ctx) override;
    void cleanup() override;
    const char* getPipelineName() const override { return "Forward"; }
    void buildRenderGraph(RenderGraph& rg,
                          const RenderGraphBuildContext& ctx) override;

    MaterialTemplate* getDefaultMaterialTemplate() const {
        return defaultMaterialTemplate.get();
    }

    /// 按名字和类型获取 pass（用于运行时修改参数）。
    template<typename T>
    T* getPass(const std::string& name) {
        static_assert(std::is_base_of_v<IRenderPass, T>);
        for (auto& p : passes_) {
            if (p->passName == name) return dynamic_cast<T*>(p.get());
        }
        return nullptr;
    }

    /// 添加一个 render pass。按插入顺序决定 buildRenderGraph 中的迭代顺序。
    /// 应在 buildRenderGraph 之前调用。
    void addPass(std::unique_ptr<IRenderPass> pass);

    /// 将 pass 插入到已存在的目标 pass 之前/之后。目标不存在、名称为空或重名时抛出异常。
    void insertPassBefore(const std::string& targetPassName,
                          std::unique_ptr<IRenderPass> pass);
    void insertPassAfter(const std::string& targetPassName,
                         std::unique_ptr<IRenderPass> pass);

protected:
    VkDevice          device         = VK_NULL_HANDLE;
    VkPhysicalDevice  physicalDevice = VK_NULL_HANDLE;

    std::vector<std::unique_ptr<IRenderPass>> passes_;

    struct PassOrderConstraint {
        IRenderPass* before = nullptr;
        IRenderPass* after  = nullptr;
    };
    std::vector<PassOrderConstraint> orderConstraints_;

private:
    void validateNewPass(const IRenderPass* pass) const;
    void createDefaultPasses(const FrameContext& ctx);
    /// 阴影资源装配：shadow set layout（注入材质模板 extraSetLayouts）
    /// 与 ShadowPass 的 VS-only pipeline layout。描述符由 RenderGraph 管理。
    void setupShadows(const FrameContext& ctx);

    Config rendererCfg_;
    Config materialCfg_;

    DescriptorSetManager* descManager     = nullptr;
    ShaderVariantManager* variantManager  = nullptr;
    VkDescriptorSetLayout frameSetLayout  = VK_NULL_HANDLE;

    std::unique_ptr<MaterialTemplate> defaultMaterialTemplate;

    // ---- 内置阴影资源 ----
    static constexpr uint32_t kShadowSetExtraOffset = 1;  // materialSetIndex + 1
    VkDescriptorSetLayout shadowSetLayout      = VK_NULL_HANDLE;
    VkPipelineLayout      shadowPipelineLayout = VK_NULL_HANDLE;
};

} // namespace engine
