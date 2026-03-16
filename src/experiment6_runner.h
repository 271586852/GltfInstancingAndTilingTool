#ifndef EXPERIMENT6_RUNNER_H
#define EXPERIMENT6_RUNNER_H

#include "ToolConfiguration.h"
#include "experiment_framework.h"
#include "glb_reader.h"
#include <vector>
#include <string>
#include <filesystem>

namespace Experiment6 {

// Process a single GLB through full pipeline: Instancing Detection -> InstancingLOD -> Quadtree HLOD
void processSingleGlbFullPipeline(
    const std::filesystem::path& glbPath,
    const std::filesystem::path& outputDir,
    const ToolConfiguration& baseConfig);

// Run complete Experiment 4 (Cross-GLB HLOD)
void runExperiment6(
    const ToolConfiguration& config,
    const std::vector<GltfInstancing::LoadedGltfModel>& loadedModels,
    const std::vector<std::string>& inputGlbs,
    const std::string& datasetName,
    ExperimentFramework::ExperimentDirectoryManager& expManager);

} // namespace Experiment6

#endif // EXPERIMENT6_RUNNER_H
