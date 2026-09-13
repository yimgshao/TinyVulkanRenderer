# Creating a Custom Material in TinyVulkanRenderer

This guide uses an opaque cube whose color transitions between red and blue over
time as a minimal example. It uses the built-in `GBufferPass` provided by
`DeferredRenderer`, so no custom RenderPass is required.


## 1. Register the Shader Directory in Host Code


```cpp
const auto shaderDirectory = projectRoot / "examples/user_shaders";

app::ConfigLoader configLoader;
configLoader.addCustomShaderSearchPath(shaderDirectory);

app::Application application(std::move(configLoader));
application.run();
```

## 2. Declare the ShaderAsset

```json
{
  "name": "Example/AnimatedColor",
  "properties": [
    {"name": "colorA", "type": "color", "default": [1.0, 0.1, 0.1, 1.0]},
    {"name": "colorB", "type": "color", "default": [0.1, 0.3, 1.0, 1.0]},
    {"name": "speed",  "type": "float", "default": 2.0}
  ],
  "passes": [
    {
      "name": "GBuffer",
      "lightMode": "GBuffer",
      "module": "animated_color.hlsl",
      "vertexLayout": "StaticMesh",
      "state": {
        "cull": "back",
        "depthTest": "less",
        "depthWrite": true
      }
    }
  ]
}
```

`lightMode: "GBuffer"` allows the built-in `GBufferPass` to select this
ShaderPass automatically.

## 3. Write the HLSL Shader

The vertex shader transforms the cube using its object model matrix. The fragment
shader writes to the four GBuffer render targets used by the deferred pipeline:

```hlsl
struct GBufferOutput
{
    float4 g0 : SV_Target0;
    float4 g1 : SV_Target1;
    float4 g2 : SV_Target2;
    float4 g3 : SV_Target3;
};

GBufferOutput fragmentMain(Varyings input)
{
    float phase = 0.5 + 0.5 * sin(gFrameData.timeSeconds * material.speed);
    float3 color = lerp(material.colorA.rgb, material.colorB.rgb, phase);

    GBufferOutput output;
    output.g0 = float4(0.0, 0.0, 0.0, 1.0);
    output.g1 = float4(0.5, 1.0, 0.5, 0.0);
    output.g2 = float4(1.0, 0.0, 0.0, 0.0);
    output.g3 = float4(color, 0.0);
    return output;
}
```

The example writes the animated color to the emissive GBuffer, so the resulting
color is unaffected by scene lighting. The engine updates
`gFrameData.timeSeconds` every frame.

## 4. Attach the Material to a Cube

```cpp
#include "engine/scene/PrimitiveMeshFactory.h"

auto* shader = renderModule.getShaderAssetManager().find("Example/AnimatedColor");

engine::RenderObject cube;
cube.mesh = engine::PrimitiveMeshFactory::createCube(scene, context);
cube.material = scene.createMaterial(*shader);
cube.transform = glm::translate(glm::mat4(1), glm::vec3(0, 1, 0));
scene.addRenderObject(cube);
```

The same ShaderAsset can be used to create multiple Materials, each with its own
`colorA`, `colorB`, and `speed` values.

## 5. Rendering Path

```text
RenderObject
  -> Material(Example/AnimatedColor)
  -> ShaderPass(lightMode = GBuffer)
  -> Built-in GBufferPass
  -> DeferredLighting
  -> Tonemap
```
