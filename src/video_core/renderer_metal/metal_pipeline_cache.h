// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <atomic>
#include <filesystem>
#include <memory>
#include <optional>
#include <stop_token>
#include <unordered_map>
#include "common/thread_worker.h"
#include "shader_recompiler/frontend/ir/basic_block.h"
#include "shader_recompiler/frontend/ir/value.h"
#include "shader_recompiler/frontend/maxwell/control_flow.h"
#include "shader_recompiler/host_translate_info.h"
#include "shader_recompiler/object_pool.h"
#include "video_core/rasterizer_interface.h"
#include "video_core/renderer_metal/metal_pipeline.h"
#include "video_core/shader_cache.h"

namespace NativeMetal {

/// Guest shader and pipeline cache for the native Metal backend: shared Maxwell frontend,
/// SPIR-V emission with the Metal profile, SPIRV-Cross to MSL, then native Metal objects.
///
/// Pipelines are recorded in a per-title transferable cache (`metal.bin`, the same guest
/// environment format the Vulkan backend uses, with Metal keys) and rebuilt in parallel at
/// boot. With asynchronous shaders enabled, MSL compilation of new pipelines and Metal render
/// pipeline states (one per framebuffer format combination) run on worker threads, and draws
/// using them are skipped until they are ready. The framebuffer format combinations each
/// pipeline was drawn with are remembered in `metal_variants.bin` and prebuilt at boot, so
/// revisited scenes do not compile on the GPU thread. Compute pipelines build synchronously
/// unless CITROSIS_METAL_ASYNC_COMPUTE=1 (skipped dispatches can lose game state).
/// Geometry and tessellation stages are not emulated yet: such pipelines are skipped.
class PipelineCache : public VideoCommon::ShaderCache {
public:
    PipelineCache(Tegra::MaxwellDeviceMemoryManager& device_memory_, PipelineContext context);
    ~PipelineCache();

    GraphicsPipeline* CurrentGraphicsPipeline();
    ComputePipeline* CurrentComputePipeline();

    void LoadDiskResources(u64 title_id, std::stop_token stop_loading,
                           const VideoCore::DiskResourceLoadCallback& callback);

private:
    struct ShaderPools {
        void ReleaseContents() {
            flow_block.ReleaseContents();
            block.ReleaseContents();
            inst.ReleaseContents();
        }
        Shader::ObjectPool<Shader::IR::Inst> inst{8192};
        Shader::ObjectPool<Shader::IR::Block> block{32};
        Shader::ObjectPool<Shader::Maxwell::Flow::Block> flow_block{32};
    };
    /// A stage translated to MSL but not yet compiled into a Metal function.
    struct TranslatedStage {
        Shader::Info info;
        ShaderSource source;
    };
    using TranslatedStages = std::array<std::optional<TranslatedStage>, GraphicsPipeline::NUM_STAGES>;

    struct GraphicsEntry {
        std::atomic<bool> ready{};
        std::unique_ptr<GraphicsPipeline> pipeline;
    };
    struct ComputeEntry {
        std::atomic<bool> ready{};
        std::unique_ptr<ComputePipeline> pipeline;
    };

    /// Maxwell → MSL for every stage of a graphics pipeline. Reads guest code through the
    /// environments, so runtime calls must happen on the GPU thread.
    std::optional<TranslatedStages> TranslateGraphics(ShaderPools& pools,
                                                      const GraphicsPipelineKey& key,
                                                      std::span<Shader::Environment* const> envs);
    /// Compiles translated stages into Metal functions (thread-safe).
    std::unique_ptr<GraphicsPipeline> BuildGraphics(const GraphicsPipelineKey& key,
                                                    TranslatedStages stages);
    /// Maxwell → MSL for a compute shader (reads guest code: GPU thread or disk environments).
    std::optional<TranslatedStage> TranslateCompute(ShaderPools& pools, const ComputePipelineKey& key,
                                                    Shader::Environment& env);
    /// Compiles a translated compute stage (thread-safe).
    std::unique_ptr<ComputePipeline> BuildCompute(const ComputePipelineKey& key,
                                                  TranslatedStage translated);
    void CreateGraphicsPipeline(GraphicsEntry& entry);

    /// Installs the variant hooks and prebuilds the recorded framebuffer variants.
    void AttachVariants(GraphicsPipeline& pipeline, const GraphicsPipelineKey& key);
    void LoadRecordedVariants();
    void RecordVariant(const GraphicsPipelineKey& key, const FramebufferFormats& formats);

    PipelineContext ctx;
    Shader::HostTranslateInfo host_info;
    Vulkan::DynamicFeatures dynamic_features{};
    bool use_asynchronous_shaders{};
    bool use_asynchronous_compute{};
    VariantHooks variant_hooks;

    GraphicsPipelineKey graphics_key{};
    GraphicsEntry* current_entry{};
    GraphicsPipelineKey current_key{};
    std::mutex cache_mutex; // graphics_cache/compute_cache structure (disk loading inserts)
    std::unordered_map<GraphicsPipelineKey, std::unique_ptr<GraphicsEntry>> graphics_cache;
    std::unordered_map<ComputePipelineKey, std::unique_ptr<ComputeEntry>> compute_cache;
    ShaderPools main_pools;

    std::filesystem::path pipeline_cache_filename;
    std::filesystem::path variants_filename;
    // Written only by LoadDiskResources before any pipeline is built; read-only afterwards.
    std::unordered_multimap<u64, FramebufferFormats> recorded_variants;
    Common::ThreadWorker workers;
    Common::ThreadWorker serialization_thread;
};

} // namespace NativeMetal
