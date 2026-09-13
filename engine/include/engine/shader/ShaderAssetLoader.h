#pragma once

#include "engine/shader/ShaderAsset.h"

#include <filesystem>
#include <vector>

namespace engine {

/**
 * Loads ShaderAsset descriptions from custom shader roots and the engine's
 * built-in shader asset directory. Custom roots take precedence.
 */
class ShaderAssetLoader {
public:
    explicit ShaderAssetLoader(
        std::vector<std::filesystem::path> customShaderRoots = {});

    ShaderAsset load(const std::filesystem::path& asset) const;
    std::vector<std::filesystem::path> discoverCustomAssets() const;
    std::vector<std::filesystem::path> discoverEngineAssets() const;

private:
    static std::vector<std::filesystem::path> discoverAssets(
        const std::vector<std::filesystem::path>& roots);

    std::vector<std::filesystem::path> customAssetRoots;
    std::filesystem::path engineAssetRoot;
    std::vector<std::filesystem::path> assetRoots;
    std::vector<std::filesystem::path> sourceRoots;
};

} // namespace engine
