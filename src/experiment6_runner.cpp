#include "experiment6_runner.h"
#include "glb_reader.h"
#include "instancing_detector.h"
#include "glb_writer.h"
#include "tileset_writer.h"
#include "utilities.h"
#include "semantic_parser.h"
#include "instancingLOD_manager.h"
#include "QuadtreePipeline.h"

#include <iostream>
#include <fstream>
#include <filesystem>
#include <set>
#include <sstream>
#include <algorithm>
#include <nlohmann/json.hpp>

namespace Experiment6 {

// Forward declarations
ExperimentFramework::CrossGlbHLODExperiment::CrossGlbMetrics collectCrossGlbMetrics(
    const std::filesystem::path& outputDir, bool isMerged);
int estimateOverlappingTiles(const std::vector<std::string>& glbFiles);

// Write unified tileset for a directory containing instancing_lod_output and quadtree_output
void writeUnifiedTileset(const std::filesystem::path& outputDir, const std::string& strategyName) {
    std::filesystem::path lodTilesetPath = outputDir / "instancing_lod_output" / "tileset.json";
    std::filesystem::path quadtreeTilesetPath = outputDir / "quadtree_output" / "tileset.json";
    std::filesystem::path unifiedTilesetPath = outputDir / "tileset.json";

    // Use nlohmann::json to construct proper 3D Tiles 1.1 tileset
    nlohmann::json tilesetJson;
    tilesetJson["asset"]["version"] = "1.1";
    tilesetJson["geometricError"] = 1000000.0;

    nlohmann::json rootJson;
    rootJson["refine"] = "ADD";
    rootJson["geometricError"] = 1000000.0;

    // Use box format for bounding volume (more reliable than region)
    // Center at origin with large extents
    rootJson["boundingVolume"]["box"] = {0.0, 0.0, 0.0, 500000.0, 0.0, 0.0, 0.0, 500000.0, 0.0, 0.0, 0.0, 500000.0};

    // Transform (standard GLB Y-up to Z-up conversion)
    rootJson["transform"] = {
        -0.9023136427, 0.4310860309, 0.0, 0.0,
        0.3731804153, 0.7899661139, 0.4899996041, 0.0,
        0.2117562093, 0.4431713488, -0.8716388481, 0.0,
        -2418525.0442296155, 5374967.3619212005, 2429440.0912170662, 1.0
    };

    nlohmann::json children = nlohmann::json::array();

    if (std::filesystem::exists(lodTilesetPath)) {
        nlohmann::json lodChild;
        lodChild["refine"] = "ADD";
        lodChild["geometricError"] = 100000.0;
        lodChild["content"]["uri"] = "instancing_lod_output/tileset.json";
        // Use a smaller box for child
        lodChild["boundingVolume"]["box"] = {0.0, 0.0, 0.0, 100000.0, 0.0, 0.0, 0.0, 100000.0, 0.0, 0.0, 0.0, 100000.0};
        children.push_back(lodChild);
    }

    if (std::filesystem::exists(quadtreeTilesetPath)) {
        nlohmann::json quadChild;
        quadChild["refine"] = "ADD";
        quadChild["geometricError"] = 50000.0;
        quadChild["content"]["uri"] = "quadtree_output/tileset.json";
        quadChild["boundingVolume"]["box"] = {0.0, 0.0, 0.0, 50000.0, 0.0, 0.0, 0.0, 50000.0, 0.0, 0.0, 0.0, 50000.0};
        children.push_back(quadChild);
    }

    if (!children.empty()) {
        rootJson["children"] = children;
    }

    tilesetJson["root"] = rootJson;

    std::ofstream outFile(unifiedTilesetPath);
    if (outFile.is_open()) {
        outFile << tilesetJson.dump(2);
        outFile.close();
        GltfInstancing::logInfo("[" + strategyName + "] Unified tileset: " + unifiedTilesetPath.string());
    }
}

// Process a single GLB through full pipeline: Instancing Detection -> InstancingLOD -> Quadtree HLOD
void processSingleGlbFullPipeline(
    const std::filesystem::path& glbPath,
    const std::filesystem::path& outputDir,
    const ToolConfiguration& baseConfig) {

    std::string glbName = glbPath.stem().string();
    GltfInstancing::logInfo("[Full Pipeline] Processing: " + glbName);

    // Create subdirectories for outputs
    std::filesystem::path lodOutputDir = outputDir / "instancing_lod_output";
    std::filesystem::path quadtreeOutputDir = outputDir / "quadtree_output";
    std::filesystem::create_directories(lodOutputDir);
    std::filesystem::create_directories(quadtreeOutputDir);

    // ===== Stage 1: Load and Detect Instancing =====
    GltfInstancing::GlbReader reader;
    std::set<std::filesystem::path> fileSet = { glbPath };
    std::vector<GltfInstancing::LoadedGltfModel> loadedModels = reader.loadGltfModels(fileSet);

    if (loadedModels.empty()) {
        GltfInstancing::logError("[Full Pipeline] Failed to load: " + glbPath.string());
        return;
    }

    // Detect instancing
    GltfInstancing::InstancingDetector detector(
        baseConfig.geometryTolerance,
        baseConfig.attributesToSkipDataHash,
        baseConfig.normalTolerance,
        baseConfig.instanceLimit,
        baseConfig.allowNonUniformScaleInstancing
    );
    GltfInstancing::InstancingDetectionResult detectionResult = detector.detect(loadedModels);

    // Write instanced and non-instanced GLBs
    GltfInstancing::GlbWriter glbWriter;
    std::filesystem::path instancedGlbPath = outputDir / "instanced_meshes.glb";
    std::filesystem::path nonInstancedGlbPath = outputDir / "non_instanced_meshes.glb";

    auto instancedResult = glbWriter.writeInstancedMeshesOnly(
        loadedModels, detectionResult, instancedGlbPath);
    auto nonInstancedResult = glbWriter.writeNonInstancedMeshesOnly(
        loadedModels, detectionResult, nonInstancedGlbPath);

    // ===== Stage 2: Generate Instancing LOD =====
    if (baseConfig.enableLodGeneration && std::filesystem::exists(instancedGlbPath) && instancedResult) {
        GltfInstancing::logInfo("[Full Pipeline] Generating Instancing LOD for: " + glbName);

        // Parse semantic data if available
        GltfInstancing::SemanticParser semanticParser;
        if (!baseConfig.semanticDataPath.empty() &&
            std::filesystem::exists(baseConfig.semanticDataPath)) {
            semanticParser.parse(baseConfig.semanticDataPath);
        }

        // Generate LOD configuration
        GltfInstancing::LODConfig lodConfig;
        lodConfig.enableLOD = true;
        lodConfig.maxLODLevels = baseConfig.lodLevelCount > 0 ? baseConfig.lodLevelCount : 5;
        lodConfig.targetScreenSSE = baseConfig.targetScreenSSE;
        lodConfig.enableSemanticCheck = baseConfig.enableSemanticCheck;
        lodConfig.enableGeometricCheck = baseConfig.enableGeometricCheck;

        // Create LOD manager and generate LODs
        GltfInstancing::InstancingLODManager lodManager(lodConfig);
        auto lodResults = lodManager.generateLODs(detectionResult, loadedModels, semanticParser);

        // Write LOD levels to GLB files using writeLODGlb
        std::vector<GltfInstancing::TilesetNode> lodNodes;
        GltfInstancing::TilesetWriter tilesetWriter;

        for (auto& [level, lodResult] : lodResults) {
            std::string filename = "LOD" + std::to_string(level) + ".glb";
            std::filesystem::path levelPath = lodOutputDir / filename;

            // Write LOD level to GLB file
            auto writeRes = glbWriter.writeLODGlb(loadedModels, lodResult, levelPath);
            if (writeRes && writeRes->second.isValid()) {
                GltfInstancing::TilesetNode node;
                node.contentUri = filename;
                node.geometricError = lodResult.geometricError;
                node.boundingVolume = writeRes->second;
                lodNodes.push_back(node);
                GltfInstancing::logInfo("[Full Pipeline] Wrote " + filename + " for " + glbName);
            }
        }

        // Write LOD tileset
        if (!lodNodes.empty()) {
            std::filesystem::path lodTilesetPath = lodOutputDir / "tileset.json";

            // Sort nodes by geometric error (descending)
            std::sort(lodNodes.begin(), lodNodes.end(),
                [](const auto& a, const auto& b) { return a.geometricError > b.geometricError; });

            // Create hierarchical structure
            GltfInstancing::TilesetNode rootNode;
            GltfInstancing::TilesetNode* current = &rootNode;

            for (size_t i = 0; i < lodNodes.size(); ++i) {
                if (i == 0) {
                    rootNode = lodNodes[i];
                    if (rootNode.geometricError < 100.0) rootNode.geometricError = 1000.0;
                    current = &rootNode;
                } else {
                    current->children.push_back(lodNodes[i]);
                    current = &current->children.back();
                }
            }

            tilesetWriter.writeHierarchicalTileset(rootNode, lodTilesetPath);
            GltfInstancing::logInfo("[Full Pipeline] LOD tileset: " + lodTilesetPath.string());
        } else {
            GltfInstancing::logWarning("[Full Pipeline] No LOD nodes generated for " + glbName);
        }
    }

    // ===== Stage 3: Generate Quadtree HLOD =====
    std::filesystem::path quadtreeInput = nonInstancedGlbPath;
    if (!std::filesystem::exists(quadtreeInput) || !nonInstancedResult) {
        quadtreeInput = instancedGlbPath;
    }

    if (std::filesystem::exists(quadtreeInput) && quadtreeInput.has_filename()) {
        GltfInstancing::logInfo("[Full Pipeline] Generating Quadtree HLOD for: " + glbName);

        // Create temporary directory for quadtree input
        std::filesystem::path tempInputDir = outputDir / "quadtree_temp_input";
        std::filesystem::create_directories(tempInputDir);

        // Split GLB into separate files for quadtree
        GltfInstancing::GlbReader splitReader;
        auto modelsToSplit = splitReader.loadGltfModels({ quadtreeInput });
        if (!modelsToSplit.empty()) {
            glbWriter.writeMeshesAsSeparateGlbs(modelsToSplit, tempInputDir);

            // Run quadtree pipeline
            ToolConfiguration quadConfig = baseConfig;
            quadConfig.inputDirectory = tempInputDir.string();
            quadConfig.outputDirectory = quadtreeOutputDir.string();

            QuadtreePipeline::Pipeline pipeline(quadConfig);
            pipeline.run();

            GltfInstancing::logInfo("[Full Pipeline] Quadtree HLOD completed for: " + glbName);
        }
    }

    // Generate unified tileset for this GLB
    writeUnifiedTileset(outputDir, "B_SeparateHLOD_" + glbName);
}

// Run complete Experiment 6
void runExperiment6(
    const ToolConfiguration& config,
    const std::vector<GltfInstancing::LoadedGltfModel>& loadedModels,
    const std::vector<std::string>& inputGlbs,
    const std::string& datasetName,
    ExperimentFramework::ExperimentDirectoryManager& expManager) {

    using namespace ExperimentFramework;

    // ========== Strategy A: Merged HLOD ==========
    GltfInstancing::logInfo("Running Strategy A: Merged HLOD...");

    StrategyInfo mergedStrategy;
    mergedStrategy.id = "A_MergedHLOD";
    mergedStrategy.name = "Merged HLOD";
    mergedStrategy.description = "所有GLB合并构建统一InstancingLOD + Quadtree HLOD";
    mergedStrategy.parameters["max_depth"] = std::to_string(config.quadtreeMaxDepth);
    mergedStrategy.parameters["max_objects"] = std::to_string(config.quadtreeMaxObjectsPerTile);

    auto mergedDir = expManager.createExperimentStructure(
        ExperimentType::CROSS_GLB_HLOD, datasetName, mergedStrategy);

    auto mergedOutputDir = CrossGlbHLODExperiment::setupMergedHLODOutput(
        mergedDir.parent_path(), datasetName, inputGlbs);

    // Create subdirectories
    std::filesystem::path mergedLodDir = mergedOutputDir / "instancing_lod_output";
    std::filesystem::path mergedQuadtreeDir = mergedOutputDir / "quadtree_output";
    std::filesystem::create_directories(mergedLodDir);
    std::filesystem::create_directories(mergedQuadtreeDir);

    // Process all GLBs together
    CrossGlbHLODExperiment::CrossGlbMetrics mergedMetrics;
    {
        // Load all GLBs
        GltfInstancing::GlbReader reader;
        std::set<std::filesystem::path> fileSet;
        for (const auto& glb : inputGlbs) fileSet.insert(glb);
        auto allLoadedModels = reader.loadGltfModels(fileSet);

        if (!allLoadedModels.empty()) {
            // Detect instancing
            GltfInstancing::InstancingDetector detector(
                config.geometryTolerance,
                config.attributesToSkipDataHash,
                config.normalTolerance,
                config.instanceLimit,
                config.allowNonUniformScaleInstancing
            );
            auto detectionResult = detector.detect(allLoadedModels);

            // Write instanced/non-instanced GLBs
            GltfInstancing::GlbWriter glbWriter;
            auto mergedInstancedGlb = mergedOutputDir / "instanced_meshes.glb";
            auto mergedNonInstancedGlb = mergedOutputDir / "non_instanced_meshes.glb";

            glbWriter.writeInstancedMeshesOnly(allLoadedModels, detectionResult, mergedInstancedGlb);
            glbWriter.writeNonInstancedMeshesOnly(allLoadedModels, detectionResult, mergedNonInstancedGlb);

            // Generate Instancing LOD for merged strategy
            if (config.enableLodGeneration && std::filesystem::exists(mergedInstancedGlb)) {
                GltfInstancing::logInfo("[Merged Strategy] Generating Instancing LOD...");

                // Parse semantic data if available
                GltfInstancing::SemanticParser semanticParser;
                if (!config.semanticDataPath.empty() &&
                    std::filesystem::exists(config.semanticDataPath)) {
                    semanticParser.parse(config.semanticDataPath);
                }

                // Generate LOD configuration
                GltfInstancing::LODConfig lodConfig;
                lodConfig.enableLOD = true;
                lodConfig.maxLODLevels = config.lodLevelCount > 0 ? config.lodLevelCount : 5;
                lodConfig.targetScreenSSE = config.targetScreenSSE;
                lodConfig.enableSemanticCheck = config.enableSemanticCheck;
                lodConfig.enableGeometricCheck = config.enableGeometricCheck;

                // Create LOD manager and generate LODs
                GltfInstancing::InstancingLODManager lodManager(lodConfig);
                auto lodResults = lodManager.generateLODs(detectionResult, allLoadedModels, semanticParser);

                // Write LOD levels to GLB files
                std::vector<GltfInstancing::TilesetNode> lodNodes;
                GltfInstancing::TilesetWriter tilesetWriter;

                for (auto& [level, lodResult] : lodResults) {
                    std::string filename = "LOD" + std::to_string(level) + ".glb";
                    std::filesystem::path levelPath = mergedLodDir / filename;

                    // Write LOD level to GLB file
                    auto writeRes = glbWriter.writeLODGlb(allLoadedModels, lodResult, levelPath);
                    if (writeRes && writeRes->second.isValid()) {
                        GltfInstancing::TilesetNode node;
                        node.contentUri = filename;
                        node.geometricError = lodResult.geometricError;
                        node.boundingVolume = writeRes->second;
                        lodNodes.push_back(node);
                        GltfInstancing::logInfo("[Merged Strategy] Wrote " + filename);
                    }
                }

                // Write LOD tileset
                if (!lodNodes.empty()) {
                    std::filesystem::path lodTilesetPath = mergedLodDir / "tileset.json";

                    // Sort nodes by geometric error (descending)
                    std::sort(lodNodes.begin(), lodNodes.end(),
                        [](const auto& a, const auto& b) { return a.geometricError > b.geometricError; });

                    // Create hierarchical structure
                    GltfInstancing::TilesetNode rootNode;
                    GltfInstancing::TilesetNode* current = &rootNode;

                    for (size_t i = 0; i < lodNodes.size(); ++i) {
                        if (i == 0) {
                            rootNode = lodNodes[i];
                            if (rootNode.geometricError < 100.0) rootNode.geometricError = 1000.0;
                            current = &rootNode;
                        } else {
                            current->children.push_back(lodNodes[i]);
                            current = &current->children.back();
                        }
                    }

                    tilesetWriter.writeHierarchicalTileset(rootNode, lodTilesetPath);
                    GltfInstancing::logInfo("[Merged Strategy] LOD tileset: " + lodTilesetPath.string());
                }
            }

            // Generate Quadtree HLOD
            auto quadtreeInput = std::filesystem::exists(mergedNonInstancedGlb) ?
                mergedNonInstancedGlb : mergedInstancedGlb;
            if (std::filesystem::exists(quadtreeInput)) {
                auto tempInputDir = mergedOutputDir / "quadtree_temp_input";
                std::filesystem::create_directories(tempInputDir);

                auto modelsToSplit = reader.loadGltfModels({ quadtreeInput });
                if (!modelsToSplit.empty()) {
                    glbWriter.writeMeshesAsSeparateGlbs(modelsToSplit, tempInputDir);

                    ToolConfiguration quadConfig = config;
                    quadConfig.inputDirectory = tempInputDir.string();
                    quadConfig.outputDirectory = mergedQuadtreeDir.string();

                    QuadtreePipeline::Pipeline pipeline(quadConfig);
                    pipeline.run();
                }
            }

            // Generate unified tileset for merged strategy
            writeUnifiedTileset(mergedOutputDir, "A_MergedHLOD");
        }

        mergedMetrics = collectCrossGlbMetrics(mergedOutputDir, true);
    }

    // ========== Strategy B: Separate HLOD ==========
    GltfInstancing::logInfo("Running Strategy B: Separate HLOD...");

    StrategyInfo separateStrategy;
    separateStrategy.id = "B_SeparateHLOD";
    separateStrategy.name = "Separate HLOD";
    separateStrategy.description = "每个GLB独立构建InstancingLOD + Quadtree HLOD";
    separateStrategy.parameters["max_depth"] = std::to_string(config.quadtreeMaxDepth);
    separateStrategy.parameters["max_objects"] = std::to_string(config.quadtreeMaxObjectsPerTile);

    auto separateDir = expManager.createExperimentStructure(
        ExperimentType::CROSS_GLB_HLOD, datasetName, separateStrategy);

    auto separateOutputDir = CrossGlbHLODExperiment::setupSeparateHLODOutput(
        separateDir.parent_path(), datasetName, inputGlbs);

    CrossGlbHLODExperiment::CrossGlbMetrics separateMetrics;
    {
        int totalTiles = 0;
        int maxDepth = 0;
        double totalTilesetSize = 0;
        int totalRequests = 0;

        for (const auto& glbPath : inputGlbs) {
            std::filesystem::path glbFile(glbPath);
            std::string subdirName = glbFile.stem().string();
            std::filesystem::path subOutputDir = separateOutputDir / subdirName;
            std::filesystem::create_directories(subOutputDir);

            // Run full pipeline for this single GLB
            processSingleGlbFullPipeline(glbFile, subOutputDir, config);

            // Collect metrics from quadtree output
            auto subMetrics = collectCrossGlbMetrics(
                subOutputDir / "quadtree_output", false);
            totalTiles += subMetrics.totalTiles;
            maxDepth = std::max(maxDepth, static_cast<int>(subMetrics.maxDepth));
            totalTilesetSize += subMetrics.tilesetSizeKB;
            totalRequests += subMetrics.initialRequests;
        }

        separateMetrics.totalTiles = totalTiles;
        separateMetrics.maxDepth = maxDepth;
        separateMetrics.tilesetSizeKB = totalTilesetSize;
        separateMetrics.initialRequests = totalRequests;
        separateMetrics.overlappingTiles = 0; // TODO: calculate overlap

        // Generate aggregated tileset using box format
        GltfInstancing::logInfo("Generating aggregated tileset for Separate HLOD...");
        auto aggregatedTilesetPath = separateOutputDir / "tileset.json";

        std::vector<std::filesystem::path> childTilesets;
        for (const auto& entry : std::filesystem::directory_iterator(separateOutputDir)) {
            if (entry.is_directory()) {
                auto subTileset = entry.path() / "tileset.json";
                if (std::filesystem::exists(subTileset)) {
                    childTilesets.push_back(subTileset);
                }
            }
        }

        if (!childTilesets.empty()) {
            nlohmann::json tilesetJson;
            tilesetJson["asset"]["version"] = "1.1";
            tilesetJson["geometricError"] = 1000000.0;

            nlohmann::json rootJson;
            rootJson["refine"] = "ADD";
            rootJson["geometricError"] = 1000000.0;
            rootJson["boundingVolume"]["box"] = {0.0, 0.0, 0.0, 500000.0, 0.0, 0.0, 0.0, 500000.0, 0.0, 0.0, 0.0, 500000.0};
            rootJson["transform"] = {
                -0.9023136427, 0.4310860309, 0.0, 0.0,
                0.3731804153, 0.7899661139, 0.4899996041, 0.0,
                0.2117562093, 0.4431713488, -0.8716388481, 0.0,
                -2418525.0442296155, 5374967.3619212005, 2429440.0912170662, 1.0
            };

            nlohmann::json children = nlohmann::json::array();
            for (const auto& childPath : childTilesets) {
                std::string relativePath = std::filesystem::relative(
                    childPath, separateOutputDir).generic_string();
                std::replace(relativePath.begin(), relativePath.end(), '\\', '/');

                nlohmann::json childJson;
                childJson["refine"] = "ADD";
                childJson["geometricError"] = 100000.0;
                childJson["content"]["uri"] = relativePath;
                childJson["boundingVolume"]["box"] = {0.0, 0.0, 0.0, 100000.0, 0.0, 0.0, 0.0, 100000.0, 0.0, 0.0, 0.0, 100000.0};
                children.push_back(childJson);
            }

            if (!children.empty()) {
                rootJson["children"] = children;
            }

            tilesetJson["root"] = rootJson;

            std::ofstream outFile(aggregatedTilesetPath);
            if (outFile.is_open()) {
                outFile << tilesetJson.dump(2);
                outFile.close();
                GltfInstancing::logInfo("Aggregated tileset: " + aggregatedTilesetPath.string());
            }
        }
    }

    // Generate comparison report
    auto comparisonDir = expManager.getComparisonDir(ExperimentType::CROSS_GLB_HLOD, datasetName);
    CrossGlbHLODExperiment::generateComparisonReport(comparisonDir, datasetName, mergedMetrics, separateMetrics);

    // Write configs
    ConfigGenerator::writeConfigJson(mergedOutputDir / "config.json", config, mergedStrategy);
    ConfigGenerator::writeConfigJson(separateOutputDir / "config.json", config, separateStrategy);

    GltfInstancing::logInfo("Experiment 6 completed. Results at: " + comparisonDir.string());
}

// Collect metrics from output directory
ExperimentFramework::CrossGlbHLODExperiment::CrossGlbMetrics collectCrossGlbMetrics(
    const std::filesystem::path& outputDir,
    bool isMerged) {

    ExperimentFramework::CrossGlbHLODExperiment::CrossGlbMetrics metrics;
    metrics.totalTiles = 0;
    metrics.maxDepth = 0;
    metrics.depthVariance = 0.0;
    metrics.overlappingTiles = 0;
    metrics.aabbUtilization = 0.0;
    metrics.tilesetSizeKB = 0.0;
    metrics.rootGeometricError = 0.0;
    metrics.duplicateResources = 0;
    metrics.avgFrustumQueryTiles = 0.0;
    metrics.lodSwitchConsistency = 0.0;
    metrics.drawCalls = 0;
    metrics.initialRequests = isMerged ? 1 : 0;
    metrics.firstTileLoadTime = 0.0;
    metrics.memoryPeakMB = 0.0;

    // Count tiles from hlod_analysis.csv if exists
    std::filesystem::path analysisPath = outputDir / "hlod_analysis.csv";
    if (std::filesystem::exists(analysisPath)) {
        std::ifstream file(analysisPath);
        std::string line;
        std::getline(file, line); // Skip header

        std::map<int, int> depthCounts;
        int maxLevel = 0;

        while (std::getline(file, line)) {
            std::stringstream ss(line);
            std::string levelStr;
            std::getline(ss, levelStr, ',');
            int level = std::stoi(levelStr);
            depthCounts[level]++;
            maxLevel = std::max(maxLevel, level);
            metrics.totalTiles++;
        }

        metrics.maxDepth = maxLevel;

        // Calculate depth variance
        if (!depthCounts.empty() && metrics.totalTiles > 0) {
            double sum = 0;
            for (const auto& [d, count] : depthCounts) {
                sum += d * count;
            }
            double mean = sum / metrics.totalTiles;
            double variance = 0;
            for (const auto& [d, count] : depthCounts) {
                variance += count * (d - mean) * (d - mean);
            }
            metrics.depthVariance = variance / metrics.totalTiles;
        }
    }

    // Calculate tileset.json size
    std::filesystem::path tilesetPath = outputDir / "tileset.json";
    if (std::filesystem::exists(tilesetPath)) {
        metrics.tilesetSizeKB = static_cast<double>(
            std::filesystem::file_size(tilesetPath)) / 1024.0;
    }

    return metrics;
}

// Estimate overlapping tiles based on AABB
int estimateOverlappingTiles(const std::vector<std::string>& glbFiles) {
    int overlapping = 0;
    for (size_t i = 0; i < glbFiles.size(); ++i) {
        for (size_t j = i + 1; j < glbFiles.size(); ++j) {
            // Simple heuristic: floors likely overlap
            if (glbFiles[i].find("Floor") != std::string::npos &&
                glbFiles[j].find("Floor") != std::string::npos) {
                overlapping++;
            }
        }
    }
    return overlapping;
}

} // namespace Experiment6
