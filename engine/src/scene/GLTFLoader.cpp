#include "engine/scene/GLTFLoader.h"
#include "engine/scene/Scene.h"
#include "engine/scene/Mesh.h"
#include "engine/scene/MaterialTemplate.h"
#include "engine/scene/MaterialInstance.h"
#include "engine/scene/Texture.h"
#include "engine/scene/RenderObject.h"
#include "engine/scene/Camera.h"
#include "engine/scene/Light.h"
#include "engine/VulkanContext.h"

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <tiny_gltf.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <filesystem>
#include <functional>

namespace engine {

namespace {

const uint8_t* getAccessorPtr(const tinygltf::Model& model, int accessorIndex,
                              size_t& stride) {
    const auto& acc = model.accessors[accessorIndex];
    const auto& bv = model.bufferViews[acc.bufferView];
    const auto& buf = model.buffers[bv.buffer];

    stride = bv.byteStride;
    if (stride == 0) {
        stride = tinygltf::GetNumComponentsInType(acc.type) *
                 tinygltf::GetComponentSizeInBytes(acc.componentType);
    }
    return buf.data.data() + bv.byteOffset + acc.byteOffset;
}

std::vector<glm::vec3> readVec3(const tinygltf::Model& model,
                                int accessorIndex) {
    const auto& acc = model.accessors[accessorIndex];
    std::vector<glm::vec3> result(acc.count);
    size_t stride;
    const uint8_t* ptr = getAccessorPtr(model, accessorIndex, stride);
    for (size_t i = 0; i < acc.count; ++i) {
        const float* f = reinterpret_cast<const float*>(ptr + i * stride);
        result[i] = glm::vec3(f[0], f[1], f[2]);
    }
    return result;
}

std::vector<glm::vec2> readVec2(const tinygltf::Model& model,
                                int accessorIndex) {
    const auto& acc = model.accessors[accessorIndex];
    std::vector<glm::vec2> result(acc.count);
    size_t stride;
    const uint8_t* ptr = getAccessorPtr(model, accessorIndex, stride);
    for (size_t i = 0; i < acc.count; ++i) {
        const float* f = reinterpret_cast<const float*>(ptr + i * stride);
        result[i] = glm::vec2(f[0], f[1]);
    }
    return result;
}

std::vector<uint32_t> readIndices(const tinygltf::Model& model,
                                  int accessorIndex) {
    const auto& acc = model.accessors[accessorIndex];
    std::vector<uint32_t> result(acc.count);
    size_t stride;
    const uint8_t* ptr = getAccessorPtr(model, accessorIndex, stride);
    if (acc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
        for (size_t i = 0; i < acc.count; ++i) {
            result[i] =
                *reinterpret_cast<const uint16_t*>(ptr + i * stride);
        }
    } else if (acc.componentType ==
               TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
        for (size_t i = 0; i < acc.count; ++i) {
            result[i] =
                *reinterpret_cast<const uint32_t*>(ptr + i * stride);
        }
    } else if (acc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
        for (size_t i = 0; i < acc.count; ++i) {
            result[i] = *reinterpret_cast<const uint8_t*>(ptr + i * stride);
        }
    }
    return result;
}

glm::mat4 getNodeMatrix(const tinygltf::Node& node) {
    if (!node.matrix.empty()) {
        return glm::mat4(glm::make_mat4(node.matrix.data()));
    }
    glm::mat4 T(1.0f), R(1.0f), S(1.0f);
    if (!node.translation.empty()) {
        T = glm::translate(glm::mat4(1.0f),
                           glm::vec3(node.translation[0],
                                     node.translation[1],
                                     node.translation[2]));
    }
    if (!node.rotation.empty()) {
        glm::quat q(node.rotation[3], node.rotation[0], node.rotation[1],
                    node.rotation[2]);
        R = glm::toMat4(q);
    }
    if (!node.scale.empty()) {
        S = glm::scale(glm::mat4(1.0f),
                       glm::vec3(node.scale[0], node.scale[1],
                                 node.scale[2]));
    }
    return T * R * S;
}

} // anonymous namespace

void GLTFLoader::load(const std::string& directory, Scene* scene,
                      VkDevice device, VkPhysicalDevice physicalDevice,
                      VkCommandPool commandPool, VkQueue graphicsQueue,
                      MaterialTemplate* materialTemplate) {
    std::filesystem::path dir(directory);
    std::filesystem::path gltfPath;

    if (dir.extension() == ".gltf") {
        // 直接给定 .gltf 文件：纹理等相对资源以其所在目录为基准
        gltfPath = dir;
        dir      = dir.parent_path();
    } else {
        // 给定目录：扫描其中第一个 .gltf 文件
        for (const auto& entry : std::filesystem::directory_iterator(dir)) {
            if (entry.path().extension() == ".gltf") {
                gltfPath = entry.path();
                break;
            }
        }
    }
    if (gltfPath.empty()) {
        throw std::runtime_error("No .gltf file found in " + directory);
    }

    // Load glTF model
    tinygltf::TinyGLTF loader;
    tinygltf::Model model;
    std::string err, warn;
    bool ret = loader.LoadASCIIFromFile(&model, &err, &warn,
                                        gltfPath.string());
    if (!warn.empty()) {
        std::cerr << "[glTF] Warn: " << warn << "\n";
    }
    if (!err.empty()) {
        std::cerr << "[glTF] Err: " << err << "\n";
    }
    if (!ret) {
        throw std::runtime_error("Failed to load glTF: " +
                                 gltfPath.string());
    }

    // Determine image formats based on material usage
    std::vector<VkFormat> imageFormats(model.images.size(),
                                       VK_FORMAT_R8G8B8A8_SRGB);
    for (const auto& mat : model.materials) {
        if (mat.normalTexture.index >= 0) {
            int imgIdx = model.textures[mat.normalTexture.index].source;
            imageFormats[imgIdx] = VK_FORMAT_R8G8B8A8_UNORM;
        }
        if (mat.pbrMetallicRoughness.metallicRoughnessTexture.index >= 0) {
            int imgIdx =
                model.textures[mat.pbrMetallicRoughness.metallicRoughnessTexture.index]
                    .source;
            imageFormats[imgIdx] = VK_FORMAT_R8G8B8A8_UNORM;
        }
        if (mat.occlusionTexture.index >= 0) {
            int imgIdx = model.textures[mat.occlusionTexture.index].source;
            imageFormats[imgIdx] = VK_FORMAT_R8G8B8A8_UNORM;
        }
    }

    // Load textures
    std::vector<Texture*> textures;
    textures.reserve(model.images.size());
    for (size_t i = 0; i < model.images.size(); ++i) {
        const auto& image = model.images[i];
        auto* tex = scene->createTexture();
        std::string decoded;
        tinygltf::URIDecode(image.uri, &decoded, nullptr);
        if (decoded.empty()) {
            decoded = image.uri;
        }
        std::filesystem::path texPath = dir / decoded;
        tex->load(texPath.string(), device, physicalDevice, commandPool,
                  graphicsQueue, imageFormats[i]);
        textures.push_back(tex);
    }

    // Load materials
    std::vector<MaterialInstance*> materials;
    materials.reserve(model.materials.size());
    for (const auto& gltfMat : model.materials) {
        auto* mat = scene->createMaterialInstance(materialTemplate);

        MaterialParams params{};
        const auto& pbr = gltfMat.pbrMetallicRoughness;
        if (!pbr.baseColorFactor.empty()) {
            params.baseColorFactor =
                glm::vec4(pbr.baseColorFactor[0], pbr.baseColorFactor[1],
                          pbr.baseColorFactor[2], pbr.baseColorFactor[3]);
        }
        params.metallicFactor = static_cast<float>(pbr.metallicFactor);
        params.roughnessFactor = static_cast<float>(pbr.roughnessFactor);

        if (!gltfMat.emissiveFactor.empty()) {
            params.emissiveFactor =
                glm::vec4(gltfMat.emissiveFactor[0],
                          gltfMat.emissiveFactor[1],
                          gltfMat.emissiveFactor[2], 1.0f);
        }

        if (gltfMat.normalTexture.index >= 0) {
            params.normalScale =
                static_cast<float>(gltfMat.normalTexture.scale);
        }
        if (gltfMat.occlusionTexture.index >= 0) {
            // ORM 打包约定仅当 occlusion 与 MR 同图时成立（Blender 导出器行为）；
            // 独立 occlusion 贴图暂不支持，按无 AO 处理并告警。
            if (gltfMat.occlusionTexture.index ==
                pbr.metallicRoughnessTexture.index) {
                params.occlusionStrength =
                    static_cast<float>(gltfMat.occlusionTexture.strength);
            } else {
                std::cout << "[GLTFLoader] material '" << gltfMat.name
                          << "': separate occlusionTexture not supported, "
                             "ignoring AO." << std::endl;
                params.occlusionStrength = 0.0f;
            }
        } else {
            // 无 occlusionTexture：MR 贴图 R 通道内容未定义，AO 必须为 1.0
            params.occlusionStrength = 0.0f;
        }

        params.alphaCutoff = static_cast<float>(gltfMat.alphaCutoff);
        if (gltfMat.alphaMode == "MASK") {
            params.alphaMode = AlphaMode::Mask;
        } else if (gltfMat.alphaMode == "BLEND") {
            params.alphaMode = AlphaMode::Blend;
        }
        params.doubleSided = gltfMat.doubleSided ? 1u : 0u;

        mat->setParams(params);

        if (pbr.baseColorTexture.index >= 0) {
            int imgIdx = model.textures[pbr.baseColorTexture.index].source;
            mat->setBaseColor(textures[imgIdx]);
        }
        if (pbr.metallicRoughnessTexture.index >= 0) {
            int imgIdx =
                model.textures[pbr.metallicRoughnessTexture.index].source;
            mat->setOrm(textures[imgIdx]);
        }
        if (gltfMat.normalTexture.index >= 0) {
            int imgIdx = model.textures[gltfMat.normalTexture.index].source;
            mat->setNormal(textures[imgIdx]);
            ShaderParamSet sp;
            sp.set("useNormalMap", true);
            mat->setShaderParams(sp);
        }
        if (gltfMat.emissiveTexture.index >= 0) {
            int imgIdx = model.textures[gltfMat.emissiveTexture.index].source;
            mat->setEmissive(textures[imgIdx]);
        }

        mat->writeDescriptorSet();
        materials.push_back(mat);
    }

    // Load meshes (primitives)
    struct PrimitiveMesh {
        Mesh* mesh = nullptr;
        int materialIndex = -1;
    };
    std::vector<std::vector<PrimitiveMesh>> meshPrimitives;
    meshPrimitives.reserve(model.meshes.size());

    for (const auto& gltfMesh : model.meshes) {
        std::vector<PrimitiveMesh> prims;
        prims.reserve(gltfMesh.primitives.size());
        for (const auto& prim : gltfMesh.primitives) {
            auto* mesh = scene->createMesh();

            std::vector<glm::vec3> positions;
            std::vector<glm::vec3> normals;
            std::vector<glm::vec2> uvs;

            auto posIt = prim.attributes.find("POSITION");
            if (posIt != prim.attributes.end()) {
                positions = readVec3(model, posIt->second);
            }
            auto normIt = prim.attributes.find("NORMAL");
            if (normIt != prim.attributes.end()) {
                normals = readVec3(model, normIt->second);
            }
            auto uvIt = prim.attributes.find("TEXCOORD_0");
            if (uvIt != prim.attributes.end()) {
                uvs = readVec2(model, uvIt->second);
            }

            size_t vertexCount = positions.size();
            std::vector<Vertex> vertices(vertexCount);
            for (size_t i = 0; i < vertexCount; ++i) {
                vertices[i].pos = positions[i];
                if (i < normals.size()) {
                    vertices[i].normal = normals[i];
                } else {
                    vertices[i].normal = glm::vec3(0.0f, 1.0f, 0.0f);
                }
                if (i < uvs.size()) {
                    vertices[i].texCoord = uvs[i];
                } else {
                    vertices[i].texCoord = glm::vec2(0.0f);
                }
            }

            std::vector<uint32_t> indices;
            if (prim.indices >= 0) {
                indices = readIndices(model, prim.indices);
            } else {
                indices.resize(vertexCount);
                for (size_t i = 0; i < vertexCount; ++i) {
                    indices[i] = static_cast<uint32_t>(i);
                }
            }

            mesh->setData(std::move(vertices), std::move(indices));
            mesh->upload(device, physicalDevice, commandPool, graphicsQueue);

            prims.push_back({mesh, prim.material});
        }
        meshPrimitives.push_back(std::move(prims));
    }

    // Process nodes recursively
    std::function<void(int, const glm::mat4&)> processNode =
        [&](int nodeIdx, const glm::mat4& parentTransform) {
            const auto& node = model.nodes[nodeIdx];
            glm::mat4 world = parentTransform * getNodeMatrix(node);

            if (node.mesh >= 0) {
                for (const auto& pm : meshPrimitives[node.mesh]) {
                    RenderObject obj{};
                    obj.transform = world;
                    obj.mesh = pm.mesh;
                    if (pm.materialIndex >= 0 &&
                        pm.materialIndex <
                            static_cast<int>(materials.size())) {
                        obj.material = materials[pm.materialIndex];
                    }
                    scene->addRenderObject(obj);
                }
            }

            // Camera
            if (node.camera >= 0 &&
                node.camera < static_cast<int>(model.cameras.size())) {
                const auto& cam = model.cameras[node.camera];
                Camera camera{};
                if (cam.type == "perspective") {
                    camera.type = CameraType::Perspective;
                    camera.aspectRatio =
                        static_cast<float>(cam.perspective.aspectRatio);
                    camera.yfov = static_cast<float>(cam.perspective.yfov);
                    camera.znear =
                        static_cast<float>(cam.perspective.znear);
                    camera.zfar = static_cast<float>(cam.perspective.zfar);
                } else if (cam.type == "orthographic") {
                    camera.type = CameraType::Orthographic;
                    camera.xmag =
                        static_cast<float>(cam.orthographic.xmag);
                    camera.ymag =
                        static_cast<float>(cam.orthographic.ymag);
                    camera.znear =
                        static_cast<float>(cam.orthographic.znear);
                    camera.zfar =
                        static_cast<float>(cam.orthographic.zfar);
                }
                camera.position = glm::vec3(world[3]);
                scene->setCamera(camera);
            }

            // Lights (KHR_lights_punctual)
            auto extIt = node.extensions.find("KHR_lights_punctual");
            if (extIt != node.extensions.end()) {
                int lightIdx = extIt->second.Get("light").GetNumberAsInt();
                if (lightIdx >= 0 &&
                    lightIdx < static_cast<int>(model.lights.size())) {
                    const auto& gltfLight = model.lights[lightIdx];
                    Light light{};
                    if (gltfLight.type == "directional") {
                        light.type = LightType::Directional;
                    } else if (gltfLight.type == "point") {
                        light.type = LightType::Point;
                    } else if (gltfLight.type == "spot") {
                        light.type = LightType::Spot;
                    }

                    if (!gltfLight.color.empty()) {
                        light.color = glm::vec3(
                            gltfLight.color[0], gltfLight.color[1],
                            gltfLight.color[2]);
                    }
                    light.intensity =
                        static_cast<float>(gltfLight.intensity);
                    light.position = glm::vec3(world[3]);
                    light.range = static_cast<float>(gltfLight.range);
                    light.direction = -glm::normalize(glm::vec3(world[2]));

                    if (gltfLight.type == "spot") {
                        light.innerConeAngle = static_cast<float>(
                            gltfLight.spot.innerConeAngle);
                        light.outerConeAngle = static_cast<float>(
                            gltfLight.spot.outerConeAngle);
                    }

                    scene->addLight(light);
                }
            }

            for (int childIdx : node.children) {
                processNode(childIdx, world);
            }
        };

    // Process root nodes of the default scene
    int sceneIdx = model.defaultScene >= 0 ? model.defaultScene : 0;
    if (sceneIdx < static_cast<int>(model.scenes.size())) {
        for (int nodeIdx : model.scenes[sceneIdx].nodes) {
            processNode(nodeIdx, glm::mat4(1.0f));
        }
    }
}

std::unique_ptr<Scene> GLTFLoader::loadScene(
    const std::string& directory,
    VulkanContext* vkContext,
    MaterialTemplate* materialTemplate) {
    auto scene = std::make_unique<Scene>();

    std::filesystem::path dirPath = directory;
    if (dirPath.is_relative()) {
        std::filesystem::path srcFile     = __FILE__;
        std::filesystem::path projectRoot =
            srcFile.parent_path().parent_path().parent_path().parent_path();
        dirPath = projectRoot / dirPath;
    }

    load(dirPath.string(), scene.get(),
         vkContext->device, vkContext->physicalDevice,
         vkContext->commandPool, vkContext->graphicsQueue,
         materialTemplate);

    if (!scene->getRenderObjects().empty() &&
        scene->getCamera().position == glm::vec3(0.0f, 0.0f, 3.0f)) {
        Camera cam{};
        cam.position = glm::vec3(2.0f, 2.0f, 2.0f);
        cam.yfov     = glm::pi<float>() / 3.0f;
        cam.znear    = 0.01f;
        cam.zfar     = 100.0f;
        scene->setCamera(cam);
    }

    if (scene->getLights().empty()) {
        Light sun{};
        sun.type      = LightType::Directional;
        sun.color     = glm::vec3(1.0f, 0.98f, 0.95f);
        sun.intensity = 2.0f;
        sun.direction = glm::normalize(glm::vec3(-1.0f, -2.0f, -1.0f));
        scene->addLight(sun);

        Light fill{};
        fill.type      = LightType::Directional;
        fill.color     = glm::vec3(0.3f, 0.3f, 0.4f);
        fill.intensity = 0.5f;
        fill.direction = glm::normalize(glm::vec3(1.0f, -0.5f, -0.5f));
        scene->addLight(fill);
    }

    return scene;
}

} // namespace engine
