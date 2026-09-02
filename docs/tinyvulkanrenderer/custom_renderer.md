# Custom Render Passes and Renderers

If you only want to add an effect to the existing rendering pipeline, you do not need to create a new Renderer. In most cases, you only need to:

1. Implement an `IRenderPass`.
2. Add the Pass to `ForwardRenderer` or `DeferredRenderer`.

You only need to implement a new `IRenderer` when neither the Forward nor the Deferred pipeline is suitable.

## Existing Pass Names

When inserting a Pass, you must use its actual Pass name.

Built into the Forward Renderer:

```text
Shadow
Forward
```

Built into the Deferred Renderer:

```text
Shadow
GBuffer
DeferredLighting
```

The App also adds:

```text
ImGui
```

`ImGui` is not a built-in Pass of the engine Renderer. It can only be used as an insertion target after the App has added `app::ImGuiPass`.

## Example: Displaying a GBuffer0 Debug Overlay

The following Pass is only applicable to the Deferred Renderer. It reads the existing `GBuffer0` and blends a translucent debug color over the rendered image.

```cpp
#include "engine/renderer/PipelineStateDesc.h"
#include "engine/renderer/renderpass/IRenderPass.h"

class GBufferDebugOverlayPass final : public engine::IRenderPass {
public:
    GBufferDebugOverlayPass() {
        passName = "GBufferDebugOverlay";
    }

    void Setup(engine::RenderGraphBuilder& builder,
               const engine::RenderGraphBuildContext& ctx) override {
        builder.SetShader("custom/gbuffer_debug_overlay");

        // The resource name exactly matches its declaration in the Deferred Renderer.
        builder.ReadTexture("GBuffer0");

        engine::PipelineStateDesc state{};
        state.cullMode = VK_CULL_MODE_NONE;
        state.depthTestEnable = VK_FALSE;
        state.depthWriteEnable = VK_FALSE;
        state.blendEnable = VK_TRUE;
        builder.SetPipelineState(state);

        // Preserve the Swapchain contents already written by DeferredLighting.
        builder.WriteColorPreserve(ctx.hSwapchain);
    }

    void Execute(VkCommandBuffer cmd,
                 const engine::FrameContext&,
                 const engine::RGResources&) override {
        vkCmdDraw(cmd, 3, 1, 0, 0);
    }
};
```

Place the Shader in the user Shader search directory:

```text
shaders/custom/gbuffer_debug_overlay.hlsl
```

The texture name in the Shader must match `ReadTexture("GBuffer0")`:

```hlsl
[[vk::binding(0, 1)]] Texture2D GBuffer0;
[[vk::binding(1, 1)]] SamplerState linearSampler;

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv       : TEXCOORD0;
};

VertexOutput vertexMain(uint vertexId : SV_VertexID)
{
    float2 uv = float2((vertexId << 1) & 2, vertexId & 2);

    VertexOutput output;
    output.position = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    output.uv = uv;
    return output;
}

float4 fragmentMain(VertexOutput input) : SV_Target
{
    float3 baseColor = GBuffer0.Sample(linearSampler, input.uv).rgb;
    return float4(baseColor, 0.35);
}
```

Based on Shader reflection, the RenderGraph automatically creates and binds a Descriptor for `GBuffer0`. Users do not need to specify the Descriptor Set level, Pipeline Stage, Access Mask, or Image Layout.

## Adding the Pass to the Deferred Renderer

`GBufferDebugOverlayPass` needs to run after `DeferredLighting`:

```cpp
auto renderer = std::make_unique<engine::DeferredRenderer>(
    rendererCfg, materialCfg, iblPath);

renderer->init(renderModule.createFrameContext());

renderer->insertPassAfter(
    "DeferredLighting",
    std::make_unique<GBufferDebugOverlayPass>());

renderer->addPass(std::make_unique<app::ImGuiPass>());

renderModule.setRenderer(std::move(renderer));
renderModule.buildGraph();
```

The resulting relevant order is:

```text
GBuffer
DeferredLighting
GBufferDebugOverlay
ImGui
```

If `ImGui` has already been added, you can also explicitly insert the Pass before it:

```cpp
renderer->insertPassBefore(
    "ImGui",
    std::make_unique<GBufferDebugOverlayPass>());
```

## addPass, insertPassBefore, and insertPassAfter

Append a Pass to the end of the Renderer:

```cpp
renderer->addPass(std::move(pass));
```

Ensure that the new Pass executes before a specified Pass:

```cpp
renderer->insertPassBefore("DeferredLighting", std::move(pass));
```

Ensure that the new Pass executes after a specified Pass:

```cpp
renderer->insertPassAfter("GBuffer", std::move(pass));
```

`insertPassBefore/After` creates explicit execution constraints. The RenderGraph combines these constraints with resource read/write dependencies before performing a topological sort.

If the two types of dependencies conflict, the RenderGraph reports a cyclic dependency during compilation instead of guessing the intended order.

Pass names must:

- Be non-empty.
- Be unique within the same Renderer.
- Refer to an existing target name when inserting a Pass.

The API reports an error immediately if the target does not exist or the Pass name is duplicated.

## Setup and Execute

`Setup` is called when the RenderGraph is built or rebuilt. It declares:

- The Shader.
- RenderGraph resources to read.
- Color or depth write targets.
- Pipeline state.

`Execute` is called every frame and is only responsible for recording draw commands. A typical fullscreen Pass usually only needs:

```cpp
vkCmdDraw(cmd, 3, 1, 0, 0);
```

After the window size changes, the RenderGraph calls `Setup` again. Shaders and compatible Pipelines remain cached.

## Write Targets

Clear a target before writing to it:

```cpp
builder.WriteColor(target);
builder.WriteDepth(depth);
```

Preserve the color contents from the previous Pass:

```cpp
builder.WriteColorPreserve(target);
```

Users generally do not need to specify `VkAttachmentLoadOp` or `VkAttachmentStoreOp` directly.

## Passes Without SetShader

`SetShader()` is optional:

- When `SetShader()` is called, the Shader and Pipeline are created, cached, and bound by the RenderGraph.
- When `SetShader()` is not called, the RenderGraph only manages resource dependencies, Attachments, and Dynamic Rendering.

`app::ImGuiPass` does not call `SetShader()`. In `Execute()`, it uses the ImGui Vulkan Backend, which binds its own Shader, Pipeline, and Descriptor.

## Implementing a New Renderer

If you do need an entirely new rendering pipeline, implement `IRenderer` and register all Passes in `buildRenderGraph`:

```cpp
class MyRenderer final : public engine::IRenderer {
public:
    const char* getPipelineName() const override {
        return "MyRenderer";
    }

    void init(const engine::FrameContext&) override {
        passes.push_back(std::make_unique<MyFirstPass>());
        passes.push_back(std::make_unique<MySecondPass>());
    }

    void buildRenderGraph(
        engine::RenderGraph& graph,
        const engine::RenderGraphBuildContext& ctx) override {
        for (auto& pass : passes) {
            graph.AddPass(pass.get(), ctx);
        }
    }

    void cleanup() override {
        passes.clear();
    }

private:
    std::vector<std::unique_ptr<engine::IRenderPass>> passes;
};
```

`MyFirstPass` and `MySecondPass` are user-defined `IRenderPass` implementations, not built-in engine classes.

If the new Renderer also needs to draw existing glTF scenes, you also need to create a `MaterialTemplate` and pass it to the scene loader. Refer to the implementations of `ForwardRenderer` and `DeferredRenderer`.
