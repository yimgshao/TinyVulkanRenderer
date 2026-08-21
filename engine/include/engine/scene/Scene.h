#pragma once

#include "engine/scene/Camera.h"
#include "engine/scene/Light.h"
#include "engine/scene/MaterialInstance.h"
#include "engine/scene/Mesh.h"
#include "engine/scene/RenderObject.h"
#include "engine/scene/Texture.h"

#include <vulkan/vulkan.h>
#include <memory>
#include <vector>

namespace engine {

class MaterialTemplate;

/**
 * Scene container managing meshes, material instances, textures, lights, camera,
 * and render objects.
 *
 * 所有权约定：
 *   - Scene 拥有 mesh / material instance / texture（unique_ptr）
 *   - **Scene 不拥有 MaterialTemplate**：template 由具体 IRenderer 拥有，
 *     scene 里的 MaterialInstance 只持有 raw 指针引用它
 *   - RenderObject 持有 mesh + material instance 的 raw 指针
 *
 * 清理顺序：render objects -> material instances -> meshes -> textures。
 */
class Scene {
public:
    Mesh* createMesh();
    MaterialInstance* createMaterialInstance(MaterialTemplate* tmpl);
    Texture* createTexture();

    void addRenderObject(const RenderObject& obj);
    void addLight(const Light& light);
    void setCamera(const Camera& cam);

    const std::vector<RenderObject>& getRenderObjects() const { return renderObjects; }
    std::vector<RenderObject>& getRenderObjects() { return renderObjects; }

    const std::vector<Light>& getLights() const { return lights; }
    std::vector<Light>& getLights() { return lights; }

    const Camera& getCamera() const { return camera; }
    Camera& getCamera() { return camera; }

    void cleanup(VkDevice device);

private:
    std::vector<std::unique_ptr<Mesh>>             meshes;
    std::vector<std::unique_ptr<MaterialInstance>> materialInstances;
    std::vector<std::unique_ptr<Texture>>          textures;

    std::vector<RenderObject> renderObjects;
    std::vector<Light>        lights;
    Camera                    camera;
};

} // namespace engine
