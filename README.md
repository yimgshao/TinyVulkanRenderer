# Tiny Vulkan Renderer

A lightweight Vulkan-based rendering engine that supports features such as shader variants, render graph, deferred rendering, and PBR materials.

## Compile && Run
```powershell
# Compile && Run
./run.ps1
```

## Using Custom Shaders

See [Creating a Custom Material in TinyVulkanRenderer](docs/tinyvulkanrenderer/custom_material_tutorial.md) for a complete example of registering a custom shader directory, declaring a ShaderAsset, writing an HLSL shader, and attaching the resulting Material to a scene object.

## Adding Custom Renderpass

See [Custom Renderer and RenderPass Guide](docs/tinyvulkanrenderer/custom_renderer.md) for details on creating RenderPasses, declaring RenderGraph resources, drawing scene materials, and assembling a custom renderer.
