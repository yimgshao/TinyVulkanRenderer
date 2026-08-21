#include "engine/scene/Scene.h"
#include "engine/scene/MaterialTemplate.h"

namespace engine {

Mesh* Scene::createMesh() {
    auto m = std::make_unique<Mesh>();
    Mesh* ptr = m.get();
    meshes.push_back(std::move(m));
    return ptr;
}

MaterialInstance* Scene::createMaterialInstance(MaterialTemplate* tmpl) {
    auto m = std::make_unique<MaterialInstance>();
    MaterialInstance* ptr = m.get();
    m->init(tmpl);
    materialInstances.push_back(std::move(m));
    return ptr;
}

Texture* Scene::createTexture() {
    auto t = std::make_unique<Texture>();
    Texture* ptr = t.get();
    textures.push_back(std::move(t));
    return ptr;
}

void Scene::addRenderObject(const RenderObject& obj) {
    renderObjects.push_back(obj);
}

void Scene::addLight(const Light& light) {
    lights.push_back(light);
}

void Scene::setCamera(const Camera& cam) {
    camera = cam;
}

void Scene::cleanup(VkDevice device) {
    renderObjects.clear();
    lights.clear();

    for (auto& m : materialInstances) {
        if (m) m->cleanup(device);
    }
    materialInstances.clear();

    // MaterialTemplate 由具体 pipeline (e.g. ForwardRenderer) 拥有，
    // 此处不参与清理。

    for (auto& m : meshes) {
        if (m) m->cleanup(device);
    }
    meshes.clear();

    for (auto& t : textures) {
        if (t) t->cleanup(device);
    }
    textures.clear();
}

} // namespace engine
