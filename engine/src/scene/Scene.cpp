#include "engine/scene/Scene.h"

namespace engine {

Material* Scene::createMaterial(ShaderAsset& shader) {
    auto material = std::make_unique<Material>();
    material->init(shader);
    auto* result = material.get();
    materials.push_back(std::move(material));
    return result;
}

Mesh* Scene::createMesh() {
    auto m = std::make_unique<Mesh>();
    Mesh* ptr = m.get();
    meshes.push_back(std::move(m));
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
    for (auto& material : materials) material->cleanup();
    materials.clear();
    lights.clear();


    // ShaderAsset lifetime is managed by RenderModule, not Scene.

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
