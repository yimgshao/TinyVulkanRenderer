#include "engine/shader/DxcCompiler.h"

#define NOMINMAX
#include <windows.h>
#include <dxc/dxcapi.h>
#include <spirv_cross/spirv_cross_c.h>
#include <wrl.h>

#include <algorithm>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace engine {

// =============================================================================
// 内部工具函数
// =============================================================================

namespace {

using Microsoft::WRL::ComPtr;

static const char* StageToProfile(ShaderStage stage) {
    switch (stage) {
        case ShaderStage::Vertex:   return "vs_6_0";
        case ShaderStage::Fragment: return "ps_6_0";
        case ShaderStage::Compute:  return "cs_6_0";
        case ShaderStage::Geometry: return "gs_6_0";
        case ShaderStage::Hull:     return "hs_6_0";
        case ShaderStage::Domain:   return "ds_6_0";
        default:                    return "vs_6_0";
    }
}

/// 固定入口名约定：每个 stage 的入口函数名由引擎统一规定。
static const char* StageToEntryName(ShaderStage stage) {
    switch (stage) {
        case ShaderStage::Vertex:   return "vertexMain";
        case ShaderStage::Fragment: return "fragmentMain";
        default:                    return "main";
    }
}

static VkShaderStageFlags StageToVk(ShaderStage stage) {
    switch (stage) {
        case ShaderStage::Vertex:   return VK_SHADER_STAGE_VERTEX_BIT;
        case ShaderStage::Fragment: return VK_SHADER_STAGE_FRAGMENT_BIT;
        case ShaderStage::Compute:  return VK_SHADER_STAGE_COMPUTE_BIT;
        case ShaderStage::Geometry: return VK_SHADER_STAGE_GEOMETRY_BIT;
        case ShaderStage::Hull:     return VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
        case ShaderStage::Domain:   return VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
        default:                    return 0;
    }
}

static std::wstring ToWide(const std::string& s) {
    return std::wstring(s.begin(), s.end());
}

/// camelCase -> UPPER_SNAKE（"useNormalMap" -> "USE_NORMAL_MAP"）
static std::string CamelToUpperSnake(const std::string& name) {
    std::string out;
    for (char c : name) {
        if (c >= 'A' && c <= 'Z' && !out.empty()) out += '_';
        out += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return out;
}

/// Resolve a single bool param by name.
/// Looks in passParams first, then materialParams. Returns "1" / "0".
static std::string ResolveParamValue(const std::string& paramName,
                                      const ShaderParamSet& materialParams,
                                      const ShaderParamSet& passParams) {
    if (passParams.has(paramName)) {
        return passParams.getBool(paramName) ? "1" : "0";
    }
    if (materialParams.has(paramName)) {
        return materialParams.getBool(paramName) ? "1" : "0";
    }
    return "0";  // default for missing bool
}

static std::vector<uint32_t> CopyBlobToUint32(IDxcBlob* blob) {
    if (!blob || blob->GetBufferSize() == 0) return {};
    const uint8_t* data = static_cast<const uint8_t*>(blob->GetBufferPointer());
    size_t byteSize = blob->GetBufferSize();
    size_t wordCount = (byteSize + 3) / 4;
    std::vector<uint32_t> result(wordCount);
    std::memcpy(result.data(), data, byteSize);
    return result;
}

// =============================================================================
// spirv-cross 反射 -> ShaderReflection（C API，经 spirv-cross-c-shared.dll）
// =============================================================================

static void AddBinding(ShaderReflection& out, const DescriptorBindingDesc& b) {
    DescriptorSetLayoutDesc* setLayout = nullptr;
    for (auto& s : out.sets) {
        if (s.setIndex == b.setIndex) { setLayout = &s; break; }
    }
    if (!setLayout) {
        DescriptorSetLayoutDesc fresh;
        fresh.setIndex = b.setIndex;
        out.sets.push_back(std::move(fresh));
        setLayout = &out.sets.back();
    }
    for (auto& existing : setLayout->bindings) {
        if (existing.binding == b.binding) {
            existing.stageFlags |= b.stageFlags;
            return;
        }
    }
    setLayout->bindings.push_back(b);
}

static VkFormat SpvcTypeToVkFormat(spvc_type type) {
    unsigned count = spvc_type_get_columns(type) > 1
                   ? spvc_type_get_columns(type) * spvc_type_get_vector_size(type)
                   : spvc_type_get_vector_size(type);
    switch (spvc_type_get_basetype(type)) {
        case SPVC_BASETYPE_FP32:
            switch (count) {
                case 1: return VK_FORMAT_R32_SFLOAT;
                case 2: return VK_FORMAT_R32G32_SFLOAT;
                case 3: return VK_FORMAT_R32G32B32_SFLOAT;
                case 4: return VK_FORMAT_R32G32B32A32_SFLOAT;
            }
            break;
        case SPVC_BASETYPE_INT32:
            switch (count) {
                case 1: return VK_FORMAT_R32_SINT;
                case 2: return VK_FORMAT_R32G32_SINT;
                case 3: return VK_FORMAT_R32G32B32_SINT;
                case 4: return VK_FORMAT_R32G32B32A32_SINT;
            }
            break;
        case SPVC_BASETYPE_UINT32:
            switch (count) {
                case 1: return VK_FORMAT_R32_UINT;
                case 2: return VK_FORMAT_R32G32_UINT;
                case 3: return VK_FORMAT_R32G32B32_UINT;
                case 4: return VK_FORMAT_R32G32B32A32_UINT;
            }
            break;
        default: break;
    }
    return VK_FORMAT_UNDEFINED;
}

static uint32_t SpvcTypeByteSize(spvc_type type) {
    unsigned count = spvc_type_get_columns(type) > 1
                   ? spvc_type_get_columns(type) * spvc_type_get_vector_size(type)
                   : spvc_type_get_vector_size(type);
    return count * (spvc_type_get_bit_width(type) / 8);
}

/// 反射单个 stage 的 SPIR-V，结果合并进 out（同名 binding 的 stageFlags 做 OR）。
static void ReflectStage(const std::vector<uint32_t>& spv,
                         VkShaderStageFlags stages, bool isVertex,
                         ShaderReflection& out) {
    if (spv.empty()) return;

    spvc_context context = nullptr;
    if (spvc_context_create(&context) != SPVC_SUCCESS) return;

    spvc_parsed_ir ir = nullptr;
    spvc_compiler compiler = nullptr;
    spvc_resources resources = nullptr;
    bool ok = spvc_context_parse_spirv(context, spv.data(), spv.size(), &ir) == SPVC_SUCCESS
           && spvc_context_create_compiler(context, SPVC_BACKEND_NONE, ir,
                                           SPVC_CAPTURE_MODE_TAKE_OWNERSHIP,
                                           &compiler) == SPVC_SUCCESS
           && spvc_compiler_create_shader_resources(compiler, &resources) == SPVC_SUCCESS;

    if (ok) {
        auto addResources = [&](spvc_resource_type resourceType, VkDescriptorType vkType) {
            const spvc_reflected_resource* list = nullptr;
            size_t count = 0;
            if (spvc_resources_get_resource_list_for_type(resources, resourceType,
                                                          &list, &count) != SPVC_SUCCESS)
                return;
            for (size_t i = 0; i < count; ++i) {
                DescriptorBindingDesc desc;
                desc.setIndex = spvc_compiler_get_decoration(
                    compiler, list[i].id, SpvDecorationDescriptorSet);
                desc.binding = spvc_compiler_get_decoration(
                    compiler, list[i].id, SpvDecorationBinding);
                desc.descriptorType  = vkType;
                desc.descriptorCount = 1;
                desc.stageFlags      = stages;
                desc.name            = list[i].name ? list[i].name : "";
                AddBinding(out, desc);
            }
        };

        addResources(SPVC_RESOURCE_TYPE_UNIFORM_BUFFER,  VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
        addResources(SPVC_RESOURCE_TYPE_SAMPLED_IMAGE,   VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        addResources(SPVC_RESOURCE_TYPE_SEPARATE_IMAGE,  VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
        addResources(SPVC_RESOURCE_TYPE_SEPARATE_SAMPLERS, VK_DESCRIPTOR_TYPE_SAMPLER);
        addResources(SPVC_RESOURCE_TYPE_STORAGE_BUFFER,  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        addResources(SPVC_RESOURCE_TYPE_STORAGE_IMAGE,   VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
        addResources(SPVC_RESOURCE_TYPE_SUBPASS_INPUT,   VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT);

        // Push constant
        {
            const spvc_reflected_resource* list = nullptr;
            size_t count = 0;
            if (spvc_resources_get_resource_list_for_type(
                    resources, SPVC_RESOURCE_TYPE_PUSH_CONSTANT, &list, &count) == SPVC_SUCCESS) {
                for (size_t i = 0; i < count; ++i) {
                    spvc_type structType =
                        spvc_compiler_get_type_handle(compiler, list[i].base_type_id);
                    size_t size = 0;
                    spvc_compiler_get_declared_struct_size(compiler, structType, &size);
                    if (size == 0) continue;
                    if (out.pushConstant) {
                        out.pushConstant->stageFlags |= stages;
                        out.pushConstant->size =
                            std::max(out.pushConstant->size, static_cast<uint32_t>(size));
                    } else {
                        VkPushConstantRange range{};
                        range.stageFlags = stages;
                        range.offset     = 0;
                        range.size       = static_cast<uint32_t>(size);
                        out.pushConstant = range;
                    }
                }
            }
        }

        // 顶点输入（仅 vertex stage）：按 location 排序后打包 offset/stride
        if (isVertex) {
            const spvc_reflected_resource* list = nullptr;
            size_t count = 0;
            if (spvc_resources_get_resource_list_for_type(
                    resources, SPVC_RESOURCE_TYPE_STAGE_INPUT, &list, &count) == SPVC_SUCCESS
                && count > 0) {
                std::vector<const spvc_reflected_resource*> inputs;
                inputs.reserve(count);
                for (size_t i = 0; i < count; ++i) inputs.push_back(&list[i]);
                std::sort(inputs.begin(), inputs.end(), [&](const auto* a, const auto* b) {
                    return spvc_compiler_get_decoration(compiler, a->id, SpvDecorationLocation)
                         < spvc_compiler_get_decoration(compiler, b->id, SpvDecorationLocation);
                });

                uint32_t cursor = 0;
                for (const auto* r : inputs) {
                    spvc_type type = spvc_compiler_get_type_handle(compiler, r->type_id);
                    VkVertexInputAttributeDescription attr{};
                    attr.binding  = 0;
                    attr.location = spvc_compiler_get_decoration(
                        compiler, r->id, SpvDecorationLocation);
                    attr.format = SpvcTypeToVkFormat(type);
                    attr.offset = cursor;
                    out.vertexAttrs.push_back(attr);
                    cursor += SpvcTypeByteSize(type);
                }

                out.vertexBinding.binding   = 0;
                out.vertexBinding.stride    = cursor;
                out.vertexBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
                out.hasVertexInput = true;
            }
        }
    } else {
        std::cerr << "[DxcCompiler] spirv-cross reflection failed: "
                  << spvc_context_get_last_error_string(context) << std::endl;
    }

    spvc_context_destroy(context);
}

} // anonymous namespace

// =============================================================================
// DxcCompiler::Impl
// =============================================================================

class DxcCompiler::Impl {
public:
    ComPtr<IDxcUtils>          utils_;
    ComPtr<IDxcCompiler3>      compiler_;
    ComPtr<IDxcIncludeHandler> includeHandler_;

    std::filesystem::path searchDir_;
    bool enableDebugInfo_ = false;
    bool isInitialized_   = false;

    Impl() = default;
    ~Impl() { Cleanup(); }

    // -------------------------------------------------------------------------
    // Init / Cleanup
    // -------------------------------------------------------------------------
    bool Init(const std::filesystem::path& shaderSearchDir, bool enableDebugInfo) {
        if (isInitialized_) return true;
        searchDir_      = shaderSearchDir;
        enableDebugInfo_ = enableDebugInfo;

        if (FAILED(DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils_))) || !utils_) {
            std::cerr << "[DxcCompiler] Failed to create IDxcUtils" << std::endl;
            return false;
        }
        if (FAILED(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler_))) || !compiler_) {
            std::cerr << "[DxcCompiler] Failed to create IDxcCompiler3" << std::endl;
            return false;
        }
        if (FAILED(utils_->CreateDefaultIncludeHandler(&includeHandler_)) || !includeHandler_) {
            std::cerr << "[DxcCompiler] Failed to create include handler" << std::endl;
            return false;
        }

        std::cout << "[DxcCompiler] Initialized. SearchDir: " << searchDir_.string() << std::endl;
        isInitialized_ = true;
        return true;
    }

    void Cleanup() {
        includeHandler_.Reset();
        compiler_.Reset();
        utils_.Reset();
        isInitialized_ = false;
    }

    void ClearCaches() {
        // 无内部缓存，接口保留以对齐调用方。
    }

    // -------------------------------------------------------------------------
    // 单个 stage 编译（入口函数名按固定约定，见 StageToEntryName）
    // -------------------------------------------------------------------------
    std::vector<uint32_t> CompileStage(const DxcBuffer& source,
                                        ShaderStage stage,
                                        const std::vector<std::wstring>& defineArgs) {
        std::vector<std::wstring> argStorage;
        argStorage.reserve(defineArgs.size() + 16);
        argStorage.push_back(L"-spirv");
        argStorage.push_back(L"-fspv-target-env=vulkan1.2");
        argStorage.push_back(L"-fspv-entrypoint-name=main");
        argStorage.push_back(L"-fvk-use-gl-layout");
        argStorage.push_back(L"-T");
        argStorage.push_back(ToWide(StageToProfile(stage)));
        argStorage.push_back(L"-E");
        argStorage.push_back(ToWide(StageToEntryName(stage)));
        argStorage.push_back(L"-I");
        argStorage.push_back(searchDir_.wstring());
        for (const auto& def : defineArgs) argStorage.push_back(def);
        if (enableDebugInfo_) argStorage.push_back(L"-Zi");

        std::vector<LPCWSTR> args;
        args.reserve(argStorage.size());
        for (const auto& a : argStorage) args.push_back(a.c_str());

        ComPtr<IDxcResult> result;
        HRESULT hr = compiler_->Compile(&source, args.data(),
                                         static_cast<UINT32>(args.size()),
                                         includeHandler_.Get(), IID_PPV_ARGS(&result));
        if (FAILED(hr) || !result) {
            throw std::runtime_error("IDxcCompiler3::Compile call failed");
        }

        ComPtr<IDxcBlobUtf8> errors;
        result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr);
        if (errors && errors->GetStringLength() > 0) {
            std::cerr << "[Dxc Diagnostics]\n" << errors->GetStringPointer() << std::endl;
        }

        HRESULT status = S_OK;
        result->GetStatus(&status);
        if (FAILED(status)) {
            throw std::runtime_error(std::string("dxc compile failed: ")
                                     + StageToEntryName(stage));
        }

        ComPtr<IDxcBlob> object;
        result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&object), nullptr);
        if (!object) {
            throw std::runtime_error(std::string("dxc produced no object: ")
                                     + StageToEntryName(stage));
        }
        return CopyBlobToUint32(object.Get());
    }

    // -------------------------------------------------------------------------
    // 核心编译流程
    // -------------------------------------------------------------------------
    std::shared_ptr<ShaderVariantBytecode> CompileVariant(
        const ShaderModuleConfig& config, const ShaderVariantKey& key,
        const ShaderParamSet& materialParams, const ShaderParamSet& passParams,
        const std::string& materialHeader) {
        if (!isInitialized_) {
            std::cerr << "[DxcCompiler] Not initialized!" << std::endl;
            return nullptr;
        }

        try {
            // 构建 -D 宏定义：
            //   -D<MaterialType>           材质类型选择（uber-shader 分段）
            //   -DUPPER_SNAKE=1/0          布尔变体参数
            std::vector<std::wstring> defineArgs;
            if (!key.materialType.empty() && key.materialType != "Unknown") {
                defineArgs.push_back(ToWide("-D" + key.materialType));
            }

            {
                std::cout << "[DxcCompiler] Compiling: module=" << config.moduleName
                          << ", material=" << key.materialType;
                for (const auto& paramName : config.genericValueParams) {
                    std::string value = ResolveParamValue(paramName, materialParams, passParams);
                    defineArgs.push_back(ToWide("-D" + CamelToUpperSnake(paramName) + "=" + value));
                    if (value != "0")
                        std::cout << ", " << paramName << "=" << value;
                }
                std::cout << std::endl;
            }

            // 合成根编译单元：材质实现（可选）在前，pass 模块在后。
            // pass 模块不 include 任何材质文件，两者由编译期注入配对，
            // 新增材质/新增 pass 互不感知。include 路径由 -I 搜索目录解析。
            std::string sourceText;
            if (!materialHeader.empty()) {
                sourceText += "#include \"" + materialHeader + "\"\n";
            }
            sourceText += "#include \"" + config.moduleName + ".hlsl\"\n";

            DxcBuffer source{};
            source.Ptr      = sourceText.data();
            source.Size     = sourceText.size();
            source.Encoding = DXC_CP_UTF8;

            // 逐 stage 编译（入口函数名按固定约定）
            auto bytecode = std::make_shared<ShaderVariantBytecode>();
            for (ShaderStage stage : config.stages) {
                auto spv = CompileStage(source, stage, defineArgs);
                switch (stage) {
                    case ShaderStage::Vertex:   bytecode->vertexSpirv   = std::move(spv); break;
                    case ShaderStage::Fragment: bytecode->fragmentSpirv = std::move(spv); break;
                    default: break;
                }
            }

            // spirv-cross 反射（逐 stage 合并）
            for (ShaderStage stage : config.stages) {
                const std::vector<uint32_t>* spv = nullptr;
                switch (stage) {
                    case ShaderStage::Vertex:   spv = &bytecode->vertexSpirv;   break;
                    case ShaderStage::Fragment: spv = &bytecode->fragmentSpirv; break;
                    default: break;
                }
                if (spv) {
                    ReflectStage(*spv, StageToVk(stage),
                                 stage == ShaderStage::Vertex, bytecode->reflection);
                }
            }

            std::sort(bytecode->reflection.sets.begin(), bytecode->reflection.sets.end(),
                      [](const auto& a, const auto& b) { return a.setIndex < b.setIndex; });
            for (auto& s : bytecode->reflection.sets)
                std::sort(s.bindings.begin(), s.bindings.end(),
                          [](const auto& a, const auto& b) { return a.binding < b.binding; });

            std::cout << "[DxcCompiler] Compiled: vert=" << bytecode->vertexSpirv.size()
                      << "w frag=" << bytecode->fragmentSpirv.size() << "w" << std::endl;
            return bytecode;

        } catch (const std::exception& e) {
            std::cerr << "[DxcCompiler] CompileVariant failed: " << e.what() << std::endl;
            return nullptr;
        }
    }
};

// =============================================================================
// DxcCompiler 公共接口
// =============================================================================

DxcCompiler::DxcCompiler() : impl_(std::make_unique<Impl>()) {}
DxcCompiler::~DxcCompiler() = default;
DxcCompiler::DxcCompiler(DxcCompiler&&) noexcept = default;
DxcCompiler& DxcCompiler::operator=(DxcCompiler&&) noexcept = default;

bool DxcCompiler::Init(const std::filesystem::path& shaderSearchDir, bool enableDebugInfo) {
    return impl_->Init(shaderSearchDir, enableDebugInfo);
}

void DxcCompiler::Cleanup() { impl_->Cleanup(); }

std::shared_ptr<ShaderVariantBytecode> DxcCompiler::CompileVariant(
    const ShaderModuleConfig& config, const ShaderVariantKey& key,
    const ShaderParamSet& materialParams, const ShaderParamSet& passParams,
    const std::string& materialHeader) {
    return impl_->CompileVariant(config, key, materialParams, passParams, materialHeader);
}

void DxcCompiler::ClearCaches() { impl_->ClearCaches(); }

} // namespace engine
