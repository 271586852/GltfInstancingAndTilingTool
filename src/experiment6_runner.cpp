#include "experiment6_runner.h"
#include "glb_reader.h"
#include "instancing_result.h"
#include "semantic_material_geometric_detector.h"
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
#include <array>
#include <limits>
#include <nlohmann/json.hpp>

namespace Experiment6 {

// Forward declarations
ExperimentFramework::CrossGlbHLODExperiment::CrossGlbMetrics collectCrossGlbMetrics(
    const std::filesystem::path& outputDir, bool isMerged);
int estimateOverlappingTiles(const std::vector<std::string>& glbFiles);

namespace {

using Box12 = std::array<double, 12>;
using Mat16 = std::array<double, 16>;

struct ExternalTilesetInfo {
    bool loaded = false;
    bool hasValidBox = false;
    Box12 worldBox{};
    double geometricError = 1000.0;
};

Mat16 makeIdentityTransform() {
    return Mat16{
        1.0, 0.0, 0.0, 0.0,
        0.0, 1.0, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0,
        0.0, 0.0, 0.0, 1.0
    };
}

bool parseBox12(const nlohmann::json& boxJson, Box12& outBox) {
    if (!boxJson.is_array() || boxJson.size() != 12) {
        return false;
    }
    for (size_t i = 0; i < outBox.size(); ++i) {
        if (!boxJson[i].is_number()) {
            return false;
        }
        outBox[i] = boxJson[i].get<double>();
    }
    return true;
}

Mat16 parseTransformOrIdentity(const nlohmann::json& rootJson) {
    if (rootJson.contains("transform") &&
        rootJson["transform"].is_array() &&
        rootJson["transform"].size() == 16) {
        Mat16 transform{};
        bool valid = true;
        for (size_t i = 0; i < transform.size(); ++i) {
            if (!rootJson["transform"][i].is_number()) {
                valid = false;
                break;
            }
            transform[i] = rootJson["transform"][i].get<double>();
        }
        if (valid) {
            return transform;
        }
    }
    return makeIdentityTransform();
}

std::array<double, 3> transformPoint(const Mat16& m, double x, double y, double z) {
    // 3D Tiles transform uses column-major order.
    return {
        m[0] * x + m[4] * y + m[8] * z + m[12],
        m[1] * x + m[5] * y + m[9] * z + m[13],
        m[2] * x + m[6] * y + m[10] * z + m[14]
    };
}

std::array<double, 3> transformVector(const Mat16& m, double x, double y, double z) {
    return {
        m[0] * x + m[4] * y + m[8] * z,
        m[1] * x + m[5] * y + m[9] * z,
        m[2] * x + m[6] * y + m[10] * z
    };
}

Box12 transformOrientedBox(const Box12& box, const Mat16& transform) {
    auto center = transformPoint(transform, box[0], box[1], box[2]);
    auto hx = transformVector(transform, box[3], box[4], box[5]);
    auto hy = transformVector(transform, box[6], box[7], box[8]);
    auto hz = transformVector(transform, box[9], box[10], box[11]);
    return Box12{
        center[0], center[1], center[2],
        hx[0], hx[1], hx[2],
        hy[0], hy[1], hy[2],
        hz[0], hz[1], hz[2]
    };
}

void expandAabbByBoxCorners(
    const Box12& box,
    std::array<double, 3>& minV,
    std::array<double, 3>& maxV) {
    const std::array<double, 3> c{ box[0], box[1], box[2] };
    const std::array<double, 3> u{ box[3], box[4], box[5] };
    const std::array<double, 3> v{ box[6], box[7], box[8] };
    const std::array<double, 3> w{ box[9], box[10], box[11] };

    for (int su : {-1, 1}) {
        for (int sv : {-1, 1}) {
            for (int sw : {-1, 1}) {
                std::array<double, 3> p{
                    c[0] + su * u[0] + sv * v[0] + sw * w[0],
                    c[1] + su * u[1] + sv * v[1] + sw * w[1],
                    c[2] + su * u[2] + sv * v[2] + sw * w[2]
                };
                minV[0] = std::min(minV[0], p[0]);
                minV[1] = std::min(minV[1], p[1]);
                minV[2] = std::min(minV[2], p[2]);
                maxV[0] = std::max(maxV[0], p[0]);
                maxV[1] = std::max(maxV[1], p[1]);
                maxV[2] = std::max(maxV[2], p[2]);
            }
        }
    }
}

Box12 aabbToBox(const std::array<double, 3>& minV, const std::array<double, 3>& maxV) {
    const double cx = (minV[0] + maxV[0]) * 0.5;
    const double cy = (minV[1] + maxV[1]) * 0.5;
    const double cz = (minV[2] + maxV[2]) * 0.5;
    const double ex = (maxV[0] - minV[0]) * 0.5;
    const double ey = (maxV[1] - minV[1]) * 0.5;
    const double ez = (maxV[2] - minV[2]) * 0.5;
    return Box12{ cx, cy, cz, ex, 0.0, 0.0, 0.0, ey, 0.0, 0.0, 0.0, ez };
}

ExternalTilesetInfo loadExternalTilesetInfo(const std::filesystem::path& tilesetPath) {
    ExternalTilesetInfo info;

    std::ifstream in(tilesetPath);
    if (!in.is_open()) {
        return info;
    }

    nlohmann::json tilesetJson;
    try {
        in >> tilesetJson;
    } catch (...) {
        return info;
    }
    info.loaded = true;

    if (tilesetJson.contains("root") && tilesetJson["root"].is_object()) {
        const auto& rootJson = tilesetJson["root"];
        if (rootJson.contains("geometricError") && rootJson["geometricError"].is_number()) {
            info.geometricError = rootJson["geometricError"].get<double>();
        } else if (tilesetJson.contains("geometricError") && tilesetJson["geometricError"].is_number()) {
            info.geometricError = tilesetJson["geometricError"].get<double>();
        }

        if (rootJson.contains("boundingVolume") &&
            rootJson["boundingVolume"].is_object() &&
            rootJson["boundingVolume"].contains("box")) {
            Box12 localBox{};
            if (parseBox12(rootJson["boundingVolume"]["box"], localBox)) {
                Mat16 rootTransform = parseTransformOrIdentity(rootJson);
                info.worldBox = transformOrientedBox(localBox, rootTransform);
                info.hasValidBox = true;
            }
        }
    }
    return info;
}

nlohmann::json boxToJson(const Box12& box) {
    return nlohmann::json::array({
        box[0], box[1], box[2],
        box[3], box[4], box[5],
        box[6], box[7], box[8],
        box[9], box[10], box[11]
    });
}

void rebaseTileContentUris(nlohmann::json& tileJson, const std::string& prefix) {
    if (prefix.empty()) {
        return;
    }

    if (tileJson.contains("content") && tileJson["content"].is_object() &&
        tileJson["content"].contains("uri") && tileJson["content"]["uri"].is_string()) {
        tileJson["content"]["uri"] = prefix + tileJson["content"]["uri"].get<std::string>();
    }

    if (tileJson.contains("contents") && tileJson["contents"].is_array()) {
        for (auto& contentJson : tileJson["contents"]) {
            if (contentJson.is_object() &&
                contentJson.contains("uri") &&
                contentJson["uri"].is_string()) {
                contentJson["uri"] = prefix + contentJson["uri"].get<std::string>();
            }
        }
    }

    if (tileJson.contains("children") && tileJson["children"].is_array()) {
        for (auto& childJson : tileJson["children"]) {
            if (childJson.is_object()) {
                rebaseTileContentUris(childJson, prefix);
            }
        }
    }
}

bool writeInlineAggregatedTileset(
    const std::filesystem::path& parentOutputDir,
    const std::vector<std::filesystem::path>& childTilesets,
    const std::string& strategyName) {
    if (childTilesets.empty()) {
        return false;
    }

    nlohmann::json tilesetJson;
    tilesetJson["asset"]["version"] = "1.1";

    nlohmann::json rootJson;
    rootJson["refine"] = "ADD";

    nlohmann::json children = nlohmann::json::array();
    std::vector<Box12> childBoxes;
    double maxChildError = 0.0;

    for (const auto& childPath : childTilesets) {
        std::ifstream in(childPath);
        if (!in.is_open()) {
            GltfInstancing::logWarning("[" + strategyName + "] Cannot open child tileset: " + childPath.string());
            continue;
        }

        nlohmann::json childTilesetJson;
        try {
            in >> childTilesetJson;
        } catch (...) {
            GltfInstancing::logWarning("[" + strategyName + "] Invalid child tileset json: " + childPath.string());
            continue;
        }

        if (!childTilesetJson.contains("root") || !childTilesetJson["root"].is_object()) {
            GltfInstancing::logWarning("[" + strategyName + "] Child tileset has no valid root: " + childPath.string());
            continue;
        }

        nlohmann::json childRoot = childTilesetJson["root"];

        std::string prefix = std::filesystem::relative(childPath.parent_path(), parentOutputDir).generic_string();
        std::replace(prefix.begin(), prefix.end(), '\\', '/');
        if (!prefix.empty() && prefix.back() != '/') {
            prefix += "/";
        }
        rebaseTileContentUris(childRoot, prefix);

        ExternalTilesetInfo childInfo = loadExternalTilesetInfo(childPath);
        if (!childRoot.contains("geometricError") || !childRoot["geometricError"].is_number()) {
            childRoot["geometricError"] = std::max(1.0, childInfo.geometricError);
        }

        if (!childRoot.contains("refine") || !childRoot["refine"].is_string()) {
            childRoot["refine"] = "ADD";
        }

        if (childInfo.hasValidBox) {
            childBoxes.push_back(childInfo.worldBox);
        } else {
            Box12 fallbackBox{0.0, 0.0, 0.0, 100000.0, 0.0, 0.0, 0.0, 100000.0, 0.0, 0.0, 0.0, 100000.0};
            childBoxes.push_back(fallbackBox);
            if (!childRoot.contains("boundingVolume") || !childRoot["boundingVolume"].is_object()) {
                childRoot["boundingVolume"] = nlohmann::json::object();
            }
            if (!childRoot["boundingVolume"].contains("box")) {
                childRoot["boundingVolume"]["box"] = boxToJson(fallbackBox);
            }
        }

        maxChildError = std::max(maxChildError, childRoot["geometricError"].get<double>());
        children.push_back(childRoot);
    }

    if (children.empty()) {
        return false;
    }

    if (!childBoxes.empty()) {
        std::array<double, 3> minV{
            std::numeric_limits<double>::max(),
            std::numeric_limits<double>::max(),
            std::numeric_limits<double>::max()
        };
        std::array<double, 3> maxV{
            std::numeric_limits<double>::lowest(),
            std::numeric_limits<double>::lowest(),
            std::numeric_limits<double>::lowest()
        };
        for (const auto& box : childBoxes) {
            expandAabbByBoxCorners(box, minV, maxV);
        }
        rootJson["boundingVolume"]["box"] = boxToJson(aabbToBox(minV, maxV));
    } else {
        rootJson["boundingVolume"]["box"] = boxToJson(
            Box12{0.0, 0.0, 0.0, 500000.0, 0.0, 0.0, 0.0, 500000.0, 0.0, 0.0, 0.0, 500000.0});
    }

    rootJson["children"] = children;
    rootJson["geometricError"] = std::max(1000.0, maxChildError * 2.0);
    tilesetJson["geometricError"] = rootJson["geometricError"];
    tilesetJson["root"] = rootJson;

    const std::filesystem::path outputPath = parentOutputDir / "tileset.json";
    std::ofstream outFile(outputPath);
    if (!outFile.is_open()) {
        return false;
    }
    outFile << tilesetJson.dump(2, ' ', false, nlohmann::json::error_handler_t::replace);
    outFile.close();
    GltfInstancing::logInfo("[" + strategyName + "] Inline aggregated tileset: " + outputPath.string());
    return true;
}

} // namespace

// Write unified tileset for a directory containing instance_lod_output and quadtree_output
void writeUnifiedTileset(const std::filesystem::path& outputDir, const std::string& strategyName) {
    std::filesystem::path lodTilesetPath = outputDir / "instance_lod_output" / "tileset.json";
    std::filesystem::path quadtreeTilesetPath = outputDir / "quadtree_output" / "tileset.json";
    std::filesystem::path unifiedTilesetPath = outputDir / "tileset.json";

    // Use nlohmann::json to construct proper 3D Tiles 1.1 tileset
    nlohmann::json tilesetJson;
    tilesetJson["asset"]["version"] = "1.1";

    nlohmann::json rootJson;
    rootJson["refine"] = "ADD";

    // NOTE: Do NOT set transform here. Child tilesets (instance_lod_output/tileset.json
    // and quadtree_output/tileset.json) already have their own transform matrices.
    // Setting transform here would cause double transformation.

    nlohmann::json children = nlohmann::json::array();
    std::vector<Box12> childBoxes;
    double maxChildError = 0.0;

    if (std::filesystem::exists(lodTilesetPath)) {
        ExternalTilesetInfo lodInfo = loadExternalTilesetInfo(lodTilesetPath);
        nlohmann::json lodChild;
        lodChild["refine"] = "ADD";
        lodChild["geometricError"] = std::max(1.0, lodInfo.geometricError);
        lodChild["content"]["uri"] = "instance_lod_output/tileset.json";
        if (lodInfo.hasValidBox) {
            lodChild["boundingVolume"]["box"] = boxToJson(lodInfo.worldBox);
            childBoxes.push_back(lodInfo.worldBox);
        } else {
            Box12 fallbackBox{0.0, 0.0, 0.0, 100000.0, 0.0, 0.0, 0.0, 100000.0, 0.0, 0.0, 0.0, 100000.0};
            lodChild["boundingVolume"]["box"] = boxToJson(fallbackBox);
            childBoxes.push_back(fallbackBox);
            GltfInstancing::logWarning("[" + strategyName + "] Missing/invalid boundingVolume in " + lodTilesetPath.string() + ", using fallback box.");
        }
        maxChildError = std::max(maxChildError, lodChild["geometricError"].get<double>());
        children.push_back(lodChild);
    }

    if (std::filesystem::exists(quadtreeTilesetPath)) {
        ExternalTilesetInfo quadInfo = loadExternalTilesetInfo(quadtreeTilesetPath);
        nlohmann::json quadChild;
        quadChild["refine"] = "ADD";
        quadChild["geometricError"] = std::max(1.0, quadInfo.geometricError);
        quadChild["content"]["uri"] = "quadtree_output/tileset.json";
        if (quadInfo.hasValidBox) {
            quadChild["boundingVolume"]["box"] = boxToJson(quadInfo.worldBox);
            childBoxes.push_back(quadInfo.worldBox);
        } else {
            Box12 fallbackBox{0.0, 0.0, 0.0, 100000.0, 0.0, 0.0, 0.0, 100000.0, 0.0, 0.0, 0.0, 100000.0};
            quadChild["boundingVolume"]["box"] = boxToJson(fallbackBox);
            childBoxes.push_back(fallbackBox);
            GltfInstancing::logWarning("[" + strategyName + "] Missing/invalid boundingVolume in " + quadtreeTilesetPath.string() + ", using fallback box.");
        }
        maxChildError = std::max(maxChildError, quadChild["geometricError"].get<double>());
        children.push_back(quadChild);
    }

    if (!childBoxes.empty()) {
        std::array<double, 3> minV{
            std::numeric_limits<double>::max(),
            std::numeric_limits<double>::max(),
            std::numeric_limits<double>::max()
        };
        std::array<double, 3> maxV{
            std::numeric_limits<double>::lowest(),
            std::numeric_limits<double>::lowest(),
            std::numeric_limits<double>::lowest()
        };
        for (const auto& box : childBoxes) {
            expandAabbByBoxCorners(box, minV, maxV);
        }
        rootJson["boundingVolume"]["box"] = boxToJson(aabbToBox(minV, maxV));
    } else {
        rootJson["boundingVolume"]["box"] = boxToJson(
            Box12{0.0, 0.0, 0.0, 500000.0, 0.0, 0.0, 0.0, 500000.0, 0.0, 0.0, 0.0, 500000.0});
    }

    rootJson["geometricError"] = std::max(1000.0, maxChildError * 2.0);
    tilesetJson["geometricError"] = rootJson["geometricError"];

    if (!children.empty()) {
        rootJson["children"] = children;
    }

    tilesetJson["root"] = rootJson;

    std::ofstream outFile(unifiedTilesetPath);
    if (outFile.is_open()) {
        outFile << tilesetJson.dump(2, ' ', false, nlohmann::json::error_handler_t::replace);
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
    std::filesystem::path lodOutputDir = outputDir / "instance_lod_output";
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
    GltfInstancing::InstancingDetectionResult detectionResult;
    {
        GltfInstancing::SemanticParser semanticParser;
        if (!baseConfig.semanticDataPath.empty() && std::filesystem::exists(baseConfig.semanticDataPath)) {
            if (std::filesystem::is_directory(baseConfig.semanticDataPath))
                semanticParser.parseFromFolder(baseConfig.semanticDataPath, { glbPath });
            else
                semanticParser.parse(baseConfig.semanticDataPath);
        }
        double thresh = baseConfig.similarityThresholdsParsed.empty() ? 0.95 : baseConfig.similarityThresholdsParsed[0];
        GltfInstancing::SemanticMaterialGeometricDetector detector(
            &semanticParser, baseConfig.semanticHashFields, thresh, baseConfig.instanceLimit, baseConfig.hausdorffMaxSamplePoints, baseConfig.allowUnknownCrossMeshClustering, baseConfig.materialFilterMode, baseConfig.enableIcpAlignment);
        detectionResult = detector.detect(loadedModels);
    }

    // Write instanced and non-instanced GLBs
    GltfInstancing::GlbWriter glbWriter;
    std::filesystem::path instancedGlbPath = outputDir / "instanced_meshes.glb";
    std::filesystem::path nonInstancedGlbPath = outputDir / "non_instanced_meshes.glb";

    auto instancedResult = glbWriter.writeInstancedMeshesOnly(
        loadedModels, detectionResult, instancedGlbPath);
    auto nonInstancedResult = glbWriter.writeNonInstancedMeshesOnly(
        loadedModels, detectionResult, nonInstancedGlbPath);

    // ===== Stage 2: Generate Instancing LOD =====
    if (baseConfig.enableInstanceLodGeneration && std::filesystem::exists(instancedGlbPath) && instancedResult) {
        GltfInstancing::logInfo("[Full Pipeline] Generating Instancing LOD for: " + glbName);

        // Parse semantic data if available
        GltfInstancing::SemanticParser semanticParser;
        if (!baseConfig.semanticDataPath.empty() &&
            std::filesystem::exists(baseConfig.semanticDataPath)) {
            if (std::filesystem::is_directory(baseConfig.semanticDataPath))
                semanticParser.parseFromFolder(baseConfig.semanticDataPath, { glbPath });
            else
                semanticParser.parse(baseConfig.semanticDataPath);
        }

        // Generate LOD configuration
        GltfInstancing::LODConfig lodConfig;
        lodConfig.enableLOD = true;
        lodConfig.maxLODLevels = baseConfig.lodLevelCount > 0 ? baseConfig.lodLevelCount : 5;
        lodConfig.targetScreenSSE = baseConfig.targetScreenSSE;
        lodConfig.enableSemanticCheck = baseConfig.enableSemanticCheck;
        lodConfig.enableGeometricCheck = baseConfig.enableGeometricCheck;
        lodConfig.similarityThresholdsPerLevel = baseConfig.instanceLodSimilarityThresholdsParsed.size() >= 5
            ? std::vector<double>(baseConfig.instanceLodSimilarityThresholdsParsed.begin() + 1, baseConfig.instanceLodSimilarityThresholdsParsed.begin() + 5)
            : std::vector<double>{ 0.90, 0.85, 0.80, 0.75 };
        lodConfig.hausdorffMaxSamplePoints = baseConfig.hausdorffMaxSamplePoints;
        lodConfig.enableIcpAlignment = baseConfig.enableIcpAlignment;
        lodConfig.instanceLimit = baseConfig.instanceLodInstanceLimit >= 1 ? baseConfig.instanceLodInstanceLimit : baseConfig.instanceLimit;
        lodConfig.materialFilterMode = baseConfig.instanceLodMaterialFilterMode;
        lodConfig.lod4_sizeTolerance = baseConfig.lod4SizeTolerance;
        lodConfig.lod3_aspectRatioTolerance = baseConfig.lod3AspectRatioTolerance;

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
            quadConfig.semanticInputDirectory = glbPath.parent_path().string();

            QuadtreePipeline::Pipeline pipeline(quadConfig);
            pipeline.run();

            GltfInstancing::logInfo("[Full Pipeline] Quadtree HLOD completed for: " + glbName);
        }
    }

    // Generate unified tileset for this GLB
    writeUnifiedTileset(outputDir, "B_SeparateHLOD_" + glbName);
}

// Run complete Experiment 4 (Cross-GLB HLOD)
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
    std::filesystem::path mergedLodDir = mergedOutputDir / "instance_lod_output";
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
            GltfInstancing::InstancingDetectionResult detectionResult;
            {
                GltfInstancing::SemanticParser semanticParser;
                if (!config.semanticDataPath.empty() && std::filesystem::exists(config.semanticDataPath)) {
                    if (std::filesystem::is_directory(config.semanticDataPath)) {
                        std::set<std::filesystem::path> glbSet(inputGlbs.begin(), inputGlbs.end());
                        semanticParser.parseFromFolder(config.semanticDataPath, glbSet);
                    } else {
                        semanticParser.parse(config.semanticDataPath);
                    }
                }
                double thresh = config.similarityThresholdsParsed.empty() ? 0.95 : config.similarityThresholdsParsed[0];
                GltfInstancing::SemanticMaterialGeometricDetector detector(
                    &semanticParser, config.semanticHashFields, thresh, config.instanceLimit, config.hausdorffMaxSamplePoints, config.allowUnknownCrossMeshClustering, config.materialFilterMode, config.enableIcpAlignment);
                detectionResult = detector.detect(allLoadedModels);
            }

            // Write instanced/non-instanced GLBs
            GltfInstancing::GlbWriter glbWriter;
            auto mergedInstancedGlb = mergedOutputDir / "instanced_meshes.glb";
            auto mergedNonInstancedGlb = mergedOutputDir / "non_instanced_meshes.glb";

            glbWriter.writeInstancedMeshesOnly(allLoadedModels, detectionResult, mergedInstancedGlb);
            glbWriter.writeNonInstancedMeshesOnly(allLoadedModels, detectionResult, mergedNonInstancedGlb);

            // Generate Instancing LOD for merged strategy
            if (config.enableInstanceLodGeneration && std::filesystem::exists(mergedInstancedGlb)) {
                GltfInstancing::logInfo("[Merged Strategy] Generating Instancing LOD...");

                // Parse semantic data if available
                GltfInstancing::SemanticParser semanticParser;
                if (!config.semanticDataPath.empty() &&
                    std::filesystem::exists(config.semanticDataPath)) {
                    if (std::filesystem::is_directory(config.semanticDataPath)) {
                        std::set<std::filesystem::path> glbSet(inputGlbs.begin(), inputGlbs.end());
                        semanticParser.parseFromFolder(config.semanticDataPath, glbSet);
                    } else {
                        semanticParser.parse(config.semanticDataPath);
                    }
                }

                // Generate LOD configuration
                GltfInstancing::LODConfig lodConfig;
                lodConfig.enableLOD = true;
                lodConfig.maxLODLevels = config.lodLevelCount > 0 ? config.lodLevelCount : 5;
                lodConfig.targetScreenSSE = config.targetScreenSSE;
                lodConfig.enableSemanticCheck = config.enableSemanticCheck;
                lodConfig.enableGeometricCheck = config.enableGeometricCheck;
                lodConfig.similarityThresholdsPerLevel = config.instanceLodSimilarityThresholdsParsed.size() >= 5
                    ? std::vector<double>(config.instanceLodSimilarityThresholdsParsed.begin() + 1, config.instanceLodSimilarityThresholdsParsed.begin() + 5)
                    : std::vector<double>{ 0.90, 0.85, 0.80, 0.75 };
                lodConfig.hausdorffMaxSamplePoints = config.hausdorffMaxSamplePoints;
                lodConfig.enableIcpAlignment = config.enableIcpAlignment;
                lodConfig.instanceLimit = config.instanceLodInstanceLimit >= 1 ? config.instanceLodInstanceLimit : config.instanceLimit;
                lodConfig.materialFilterMode = config.instanceLodMaterialFilterMode;
                lodConfig.lod4_sizeTolerance = config.lod4SizeTolerance;
                lodConfig.lod3_aspectRatioTolerance = config.lod3AspectRatioTolerance;

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
                    if (!inputGlbs.empty())
                        quadConfig.semanticInputDirectory = std::filesystem::path(inputGlbs[0]).parent_path().string();

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
            // NOTE: Child tilesets already have their own transform, don't set it here

            nlohmann::json children = nlohmann::json::array();
            std::vector<Box12> childBoxes;
            double maxChildError = 0.0;
            for (const auto& childPath : childTilesets) {
                std::string relativePath = std::filesystem::relative(
                    childPath, separateOutputDir).generic_string();
                std::replace(relativePath.begin(), relativePath.end(), '\\', '/');

                ExternalTilesetInfo childInfo = loadExternalTilesetInfo(childPath);
                nlohmann::json childJson;
                childJson["refine"] = "ADD";
                childJson["geometricError"] = std::max(1.0, childInfo.geometricError);
                childJson["content"]["uri"] = relativePath;
                if (childInfo.hasValidBox) {
                    childJson["boundingVolume"]["box"] = boxToJson(childInfo.worldBox);
                    childBoxes.push_back(childInfo.worldBox);
                } else {
                    Box12 fallbackBox{0.0, 0.0, 0.0, 100000.0, 0.0, 0.0, 0.0, 100000.0, 0.0, 0.0, 0.0, 100000.0};
                    childJson["boundingVolume"]["box"] = boxToJson(fallbackBox);
                    childBoxes.push_back(fallbackBox);
                    GltfInstancing::logWarning("Missing/invalid boundingVolume in " + childPath.string() + ", using fallback box.");
                }
                maxChildError = std::max(maxChildError, childJson["geometricError"].get<double>());
                children.push_back(childJson);
            }

            if (!childBoxes.empty()) {
                std::array<double, 3> minV{
                    std::numeric_limits<double>::max(),
                    std::numeric_limits<double>::max(),
                    std::numeric_limits<double>::max()
                };
                std::array<double, 3> maxV{
                    std::numeric_limits<double>::lowest(),
                    std::numeric_limits<double>::lowest(),
                    std::numeric_limits<double>::lowest()
                };
                for (const auto& box : childBoxes) {
                    expandAabbByBoxCorners(box, minV, maxV);
                }
                rootJson["boundingVolume"]["box"] = boxToJson(aabbToBox(minV, maxV));
            } else {
                rootJson["boundingVolume"]["box"] = boxToJson(
                    Box12{0.0, 0.0, 0.0, 500000.0, 0.0, 0.0, 0.0, 500000.0, 0.0, 0.0, 0.0, 500000.0});
            }
            rootJson["geometricError"] = std::max(1000.0, maxChildError * 2.0);
            tilesetJson["geometricError"] = rootJson["geometricError"];

            if (!children.empty()) {
                rootJson["children"] = children;
            }

            tilesetJson["root"] = rootJson;

            std::ofstream outFile(aggregatedTilesetPath);
            if (outFile.is_open()) {
                outFile << tilesetJson.dump(2, ' ', false, nlohmann::json::error_handler_t::replace);
                outFile.close();
                GltfInstancing::logInfo("Aggregated tileset: " + aggregatedTilesetPath.string());
            }
        }
    }

    // ========== Strategy C: Separate + Single Entry ==========
    GltfInstancing::logInfo("Running Strategy C: Separate + Single Entry...");

    StrategyInfo separateSingleEntryStrategy;
    separateSingleEntryStrategy.id = "C_SeparateSingleEntryHLOD";
    separateSingleEntryStrategy.name = "Separate HLOD + Single Entry";
    separateSingleEntryStrategy.description = "每个GLB独立构建InstancingLOD + Quadtree HLOD，但使用单入口内联tileset";
    separateSingleEntryStrategy.parameters["max_depth"] = std::to_string(config.quadtreeMaxDepth);
    separateSingleEntryStrategy.parameters["max_objects"] = std::to_string(config.quadtreeMaxObjectsPerTile);
    separateSingleEntryStrategy.parameters["aggregate_mode"] = "inline_single_entry";

    auto separateSingleEntryDir = expManager.createExperimentStructure(
        ExperimentType::CROSS_GLB_HLOD, datasetName, separateSingleEntryStrategy);
    auto separateSingleEntryOutputDir = separateSingleEntryDir.parent_path() / "C_SeparateSingleEntryHLOD";
    std::filesystem::create_directories(separateSingleEntryOutputDir);

    // Reuse B strategy generated per-GLB outputs to avoid duplicate heavy pipeline run.
    std::ofstream cListFile(separateSingleEntryOutputDir / "input_files.txt");
    for (const auto& f : inputGlbs) {
        cListFile << f << "\n";
    }
    cListFile.close();

    std::vector<std::filesystem::path> cChildTilesets;
    for (const auto& glbPathStr : inputGlbs) {
        std::filesystem::path glbPath(glbPathStr);
        std::string subdirName = glbPath.stem().string();
        std::filesystem::path srcSubDir = separateOutputDir / subdirName;
        std::filesystem::path dstSubDir = separateSingleEntryOutputDir / subdirName;
        if (std::filesystem::exists(srcSubDir)) {
            std::error_code ec;
            std::filesystem::copy(
                srcSubDir,
                dstSubDir,
                std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing,
                ec);
            if (ec) {
                GltfInstancing::logWarning("Failed to copy " + srcSubDir.string() + " to C strategy output: " + ec.message());
            }
            std::filesystem::path dstTilesetPath = dstSubDir / "tileset.json";
            if (std::filesystem::exists(dstTilesetPath)) {
                cChildTilesets.push_back(dstTilesetPath);
            }
        }
    }

    if (!writeInlineAggregatedTileset(
            separateSingleEntryOutputDir,
            cChildTilesets,
            "C_SeparateSingleEntryHLOD")) {
        GltfInstancing::logWarning("Failed to generate C strategy inline aggregated tileset.");
    }

    auto separateSingleEntryMetrics = collectCrossGlbMetrics(separateSingleEntryOutputDir, false);

    // Generate comparison report
    auto comparisonDir = expManager.getComparisonDir(ExperimentType::CROSS_GLB_HLOD, datasetName);
    CrossGlbHLODExperiment::generateComparisonReport(
        comparisonDir,
        datasetName,
        mergedMetrics,
        separateMetrics,
        &separateSingleEntryMetrics);

    // Additional C strategy summary for ablation (single-entry separate strategy).
    {
        std::ofstream cReport(comparisonDir / "strategy_c_metrics.txt");
        if (cReport.is_open()) {
            cReport << "Strategy C (Separate + Single Entry) Metrics\n";
            cReport << "Dataset: " << datasetName << "\n\n";
            cReport << "Total Tiles: " << separateSingleEntryMetrics.totalTiles << "\n";
            cReport << "Max Depth: " << separateSingleEntryMetrics.maxDepth << "\n";
            cReport << "Tileset Size (KB): " << separateSingleEntryMetrics.tilesetSizeKB << "\n";
            cReport << "Initial Requests: " << separateSingleEntryMetrics.initialRequests << "\n";
            cReport.close();
        }
    }

    // Write configs
    ConfigGenerator::writeConfigJson(mergedOutputDir / "config.json", config, mergedStrategy);
    ConfigGenerator::writeConfigJson(separateOutputDir / "config.json", config, separateStrategy);
    ConfigGenerator::writeConfigJson(separateSingleEntryOutputDir / "config.json", config, separateSingleEntryStrategy);

    GltfInstancing::logInfo("Experiment 4 completed. Results at: " + comparisonDir.string());
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
            if (line.empty() || line[0] == '#') continue;  // Skip blank lines and comments
            std::stringstream ss(line);
            std::string levelStr;
            if (!std::getline(ss, levelStr, ',')) continue;
            if (levelStr.empty() || !std::isdigit(static_cast<unsigned char>(levelStr[0]))) continue;  // Skip header "Level"
            try {
                int level = std::stoi(levelStr);
                depthCounts[level]++;
                maxLevel = std::max(maxLevel, level);
                metrics.totalTiles++;
            } catch (...) {}
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
