#include "glb_reader.h"
#include "instancing_detector.h"
#include "glb_writer.h"
#include "tileset_writer.h"
#include "utilities.h"
#include "ToolConfiguration.h"
#include "semantic_parser.h"
#include "instancingLOD_manager.h"
#include "NonInstancingLOD_manager.h"
#include "QuadtreePipeline.h"
#include "experiment_framework.h"
#include "experiment6_runner.h"

#include <iostream>
#include <filesystem>
#include <string>
#include <vector>
#include <cstdlib> // For std::atof
#include <set> // Required for std::set
#include <sstream> // Required for std::stringstream
#include <algorithm> // Required for std::remove_if, std::isspace
#include <fstream>   // Required for std::ifstream
#include <iomanip>   // For std::setprecision
#include <any>       // For std::any_cast
#include <chrono>    // For run_manifest timestamp
#include <ctime>     // For std::gmtime
#include <CesiumGltf\ExtensionExtMeshGpuInstancing.h>

#ifdef _WIN32
#include <windows.h>
#endif

// Disable Windows Console Quick Edit Mode to prevent accidental pausing
void disableQuickEditMode() {
#ifdef _WIN32
    HANDLE hInput = GetStdHandle(STD_INPUT_HANDLE);
    if (hInput == INVALID_HANDLE_VALUE) return;

    DWORD prevMode;
    if (!GetConsoleMode(hInput, &prevMode)) return;

    // Disable ENABLE_QUICK_EDIT_MODE to prevent console from pausing on mouse selection
    DWORD newMode = prevMode & ~ENABLE_QUICK_EDIT_MODE;
    SetConsoleMode(hInput, newMode);
#endif
}

// --- 按流水线阶段分层的输出路径 ---
namespace OutputPaths {
    inline std::filesystem::path instancingDir(const ToolConfiguration& config) {
        return std::filesystem::path(config.outputDirectory) / "01_instancing";
    }
    inline std::filesystem::path instancedGlb(const ToolConfiguration& config) {
        return instancingDir(config) / "instanced.glb";
    }
    inline std::filesystem::path nonInstancedGlb(const ToolConfiguration& config) {
        return instancingDir(config) / "non_instanced.glb";
    }
    inline std::filesystem::path instancedTileset(const ToolConfiguration& config) {
        return instancingDir(config) / "instanced.json";
    }
    inline std::filesystem::path nonInstancedTileset(const ToolConfiguration& config) {
        return instancingDir(config) / "non_instanced.json";
    }
    inline std::filesystem::path instancingAnalysisCsv(const ToolConfiguration& config) {
        return instancingDir(config) / "analysis" / "instancing.csv";
    }
    inline std::filesystem::path instancingAnalysisTxt(const ToolConfiguration& config) {
        return instancingDir(config) / "analysis" / "instancing.txt";
    }
    inline std::filesystem::path instancingPerGlbCsv(const ToolConfiguration& config) {
        return instancingDir(config) / "analysis" / "per_glb.csv";
    }
    inline std::filesystem::path instancingOptimizationSummary(const ToolConfiguration& config) {
        return instancingDir(config) / "analysis" / "optimization_summary.txt";
    }
    inline std::filesystem::path instanceLodDir(const ToolConfiguration& config) {
        return std::filesystem::path(config.outputDirectory) / "02_instance_lod";
    }
    inline std::filesystem::path instanceLodAnalysisCsv(const ToolConfiguration& config) {
        return instanceLodDir(config) / "analysis" / "instance_lod.csv";
    }
    inline std::filesystem::path nonInstanceLodDir(const ToolConfiguration& config) {
        return std::filesystem::path(config.outputDirectory) / "02_non_instance_lod";
    }
    inline std::filesystem::path nonInstanceLodAnalysisCsv(const ToolConfiguration& config) {
        return nonInstanceLodDir(config) / "analysis" / "non_instance_lod.csv";
    }
    inline std::filesystem::path hlodDir(const ToolConfiguration& config) {
        return std::filesystem::path(config.outputDirectory) / "03_hlod";
    }
    inline std::filesystem::path hlodAnalysisCsv(const ToolConfiguration& config) {
        return hlodDir(config) / "hlod_analysis.csv";
    }
    inline std::filesystem::path segmentedDir(const ToolConfiguration& config) {
        return std::filesystem::path(config.outputDirectory) / "04_segmented";
    }
    inline std::filesystem::path quadtreeTempInput(const ToolConfiguration& config) {
        return std::filesystem::path(config.outputDirectory) / "03_hlod" / "_temp_input";
    }
    inline std::filesystem::path analysisDir(const ToolConfiguration& config) {
        return std::filesystem::path(config.outputDirectory) / "_analysis";
    }
    inline std::filesystem::path resultsCsv(const ToolConfiguration& config, const std::string& baseName) {
        return analysisDir(config) / (baseName + "_results.csv");
    }
    inline void ensureOutputDirectories(const ToolConfiguration& config) {
        std::filesystem::create_directories(instancingDir(config) / "analysis");
        std::filesystem::create_directories(instanceLodDir(config) / "analysis");
        std::filesystem::create_directories(nonInstanceLodDir(config) / "analysis");
        std::filesystem::create_directories(analysisDir(config));
    }
}

// Function to trim whitespace from both ends of a string
std::string trim(const std::string& str) {
    const auto strBegin = str.find_first_not_of(" 	");
    if (strBegin == std::string::npos)
        return ""; // no content

    const auto strEnd = str.find_last_not_of(" 	");
    const auto strRange = strEnd - strBegin + 1;

    return str.substr(strBegin, strRange);
}

// Function to split a string by a delimiter and trim whitespace
std::set<std::string> splitAndTrim(const std::string& s, char delimiter) {
    std::set<std::string> tokens;
    std::string token;
    std::istringstream tokenStream(s);
    while (std::getline(tokenStream, token, delimiter)) {
        tokens.insert(trim(token));
    }
    return tokens;
}

// Function to parse a key-value pair from a line
bool parseKeyValuePair(const std::string& line, std::string& key, std::string& value) {
    size_t delimiterPos = line.find('=');
    if (delimiterPos == std::string::npos) {
        return false; // No '=' found
    }
    key = line.substr(0, delimiterPos);
    value = line.substr(delimiterPos + 1);
    key = trim(key);
    value = trim(value);
    return !key.empty(); // Key cannot be empty
}

// Function to load configuration from a file
bool loadConfigurationFromFile(const std::string& configFilePath, ToolConfiguration& config) {
    std::ifstream configFile(configFilePath);
    if (!configFile.is_open()) {
        GltfInstancing::logInfo("Configuration file not found or could not be opened: " + configFilePath);
        return false;
    }

    GltfInstancing::logInfo("Loading configuration from: " + configFilePath);
    std::string line;
    int lineNumber = 0;
    while (std::getline(configFile, line)) {
        lineNumber++;
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') {
            continue; // Skip empty lines and comments
        }

        std::string key, value;
        if (parseKeyValuePair(line, key, value)) {
            if (key == "input_directory") {
                config.inputDirectory = value;
                config.inputDirectorySet = true;
            } else if (key == "output_directory") {
                config.outputDirectory = value;
                config.outputDirectorySet = true;
            } else if (key == "tolerance" || key == "geometry_tolerance") {
                try {
                    config.geometryTolerance = std::stod(value);
                    config.geometryToleranceSet = true;
                } catch (const std::exception& e) {
                    GltfInstancing::logWarning("Invalid value for '" + key + "' in config file (line " + std::to_string(lineNumber) + "): " + value + ". Error: " + e.what());
                }
            } else if (key == "normal_tolerance") {
                try {
                    config.normalTolerance = std::stod(value);
                    if (config.normalTolerance < 0.0) {
                        GltfInstancing::logWarning("Negative normal_tolerance in config (line " + std::to_string(lineNumber) + ") adjusted to 0.0.");
                        config.normalTolerance = 0.0;
                    }
                    config.normalToleranceSet = true;
                } catch (const std::exception& e) {
                    GltfInstancing::logWarning("Invalid value for 'normal_tolerance' in config file (line " + std::to_string(lineNumber) + "): " + value + ". Error: " + e.what());
                }
            } else if (key == "skip_attribute_data_hash") {
                config.attributesToSkipDataHash = splitAndTrim(value, ',');
                config.attributesToSkipDataHashSet = true;
            } else if (key == "merge_all_glb") {
                std::transform(value.begin(), value.end(), value.begin(), ::tolower);
                if (value == "true" || value == "1" || value == "yes") {
                    config.mergeAllGlb = true;
                } else if (value == "false" || value == "0" || value == "no") {
                    config.mergeAllGlb = false;
                } else {
                    GltfInstancing::logWarning("Invalid boolean value for 'merge_all_glb' in config file (line " + std::to_string(lineNumber) + "): " + value);
                }
                config.mergeAllGlbSet = true;
            } else if (key == "instance_limit") {
                try {
                    config.instanceLimit = std::stoi(value);
                    if (config.instanceLimit < 1) { // Instance limit cannot be less than 1
                        GltfInstancing::logWarning("Invalid value for 'instance_limit' (must be >= 1) in config file (line " + std::to_string(lineNumber) + "): " + value + ". Using default 2.");
                        config.instanceLimit = 2;
                    }
                    config.instanceLimitSet = true;
                } catch (const std::exception& e) {
                    GltfInstancing::logWarning("Invalid value for 'instance_limit' in config file (line " + std::to_string(lineNumber) + "): " + value + ". Error: " + e.what());
                }
            } else if (key == "allow_non_uniform_scale_instancing") {
                std::transform(value.begin(), value.end(), value.begin(), ::tolower);
                if (value == "true" || value == "1" || value == "yes") {
                    config.allowNonUniformScaleInstancing = true;
                } else if (value == "false" || value == "0" || value == "no") {
                    config.allowNonUniformScaleInstancing = false;
                } else {
                    GltfInstancing::logWarning("Invalid boolean value for 'allow_non_uniform_scale_instancing' in config file (line " + std::to_string(lineNumber) + "): " + value);
                }
            } else if (key == "mesh_segmentation") {
                std::transform(value.begin(), value.end(), value.begin(), ::tolower);
                if (value == "true" || value == "1" || value == "yes") {
                    config.meshSegmentation = true;
                } else if (value == "false" || value == "0" || value == "no") {
                    config.meshSegmentation = false;
                } else {
                    GltfInstancing::logWarning("Invalid boolean value for 'mesh_segmentation' in config file (line " + std::to_string(lineNumber) + "): " + value);
                }
                config.meshSegmentationSet = true;
            } else if (key == "csv_directory") {
                config.csvDirectory = value;
                config.csvDirectorySet = true;
            } 
            // --- Instance LOD Config Parsing ---
            else if (key == "enable_instance_lod_generation") {
                std::transform(value.begin(), value.end(), value.begin(), ::tolower);
                if (value == "true" || value == "1" || value == "yes") config.enableInstanceLodGeneration = true;
                else config.enableInstanceLodGeneration = false;
            } else if (key == "lod_level_count") {
                try { config.lodLevelCount = std::stoi(value); } catch(...) {}
            } else if (key == "target_screen_sse") {
                try { config.targetScreenSSE = std::stod(value); } catch(...) {}
            } else if (key == "lod4_size_tolerance") {
                try { config.lod4SizeTolerance = std::stod(value); } catch(...) {}
            } else if (key == "lod3_aspect_ratio_tolerance") {
                try { config.lod3AspectRatioTolerance = std::stod(value); } catch(...) {}
            } else if (key == "enable_semantic_check") {
                std::transform(value.begin(), value.end(), value.begin(), ::tolower);
                if (value == "true" || value == "1" || value == "yes") config.enableSemanticCheck = true;
                else config.enableSemanticCheck = false;
            } else if (key == "enable_geometric_check") {
                std::transform(value.begin(), value.end(), value.begin(), ::tolower);
                if (value == "true" || value == "1" || value == "yes") config.enableGeometricCheck = true;
                else config.enableGeometricCheck = false;
            } else if (key == "semantic_data_path") {
                config.semanticDataPath = value;
            }
            // --- Non-Instanced LOD Config Parsing ---
            else if (key == "enable_non_instanced_lod_generation") {
                std::transform(value.begin(), value.end(), value.begin(), ::tolower);
                if (value == "true" || value == "1" || value == "yes") config.enableNonInstancedLodGeneration = true;
                else config.enableNonInstancedLodGeneration = false;
            } else if (key == "non_instanced_lod_level_count") {
                try { config.nonInstancedLodLevelCount = std::stoi(value); } catch(...) {}
            } else if (key == "non_instanced_lod_ratio") {
                try { config.nonInstancedLodRatio = std::stod(value); } catch(...) {}
            } else if (key == "non_instanced_min_simplify_index_count") {
                try { config.nonInstancedMinSimplifyIndexCount = static_cast<size_t>(std::stoll(value)); } catch(...) {}
            } else if (key == "enable_non_instanced_lod_instancing") {
                std::transform(value.begin(), value.end(), value.begin(), ::tolower);
                if (value == "true" || value == "1" || value == "yes") config.enableNonInstancedLodInstancing = true;
                else config.enableNonInstancedLodInstancing = false;
            } else if (key == "enable_quadtree") {
                std::transform(value.begin(), value.end(), value.begin(), ::tolower);
                if (value == "true" || value == "1" || value == "yes") config.enableQuadtree = true;
                else config.enableQuadtree = false;
            } else if (key == "quadtree_max_depth") {
                try { config.quadtreeMaxDepth = std::stoi(value); } catch(...) {}
            } else if (key == "quadtree_max_objects_per_tile") {
                try { config.quadtreeMaxObjectsPerTile = std::stoi(value); } catch(...) {}
            } else if (key == "enable_experiment_mode") {
                std::transform(value.begin(), value.end(), value.begin(), ::tolower);
                if (value == "true" || value == "1" || value == "yes") config.enableExperimentMode = true;
                else config.enableExperimentMode = false;
            } else if (key == "use_symbolic_links") {
                std::transform(value.begin(), value.end(), value.begin(), ::tolower);
                if (value == "true" || value == "1" || value == "yes") config.useSymbolicLinks = true;
                else config.useSymbolicLinks = false;
            } else if (key == "experiment1_name") {
                config.experiment1Name = value;
            } else if (key == "experiment2_name") {
                config.experiment2Name = value;
            } else if (key == "experiment3_name") {
                config.experiment3Name = value;
            } else if (key == "run_cross_glb_hlod_experiment") {
                std::transform(value.begin(), value.end(), value.begin(), ::tolower);
                if (value == "true" || value == "1" || value == "yes") config.runCrossGlbHLODExperiment = true;
                else config.runCrossGlbHLODExperiment = false;
            } else if (key == "experiment_dataset_name") {
                config.experimentDatasetName = value;
            } else if (key == "experiment_strategy_id") {
                config.experimentStrategyId = value;
            }
            // --- HLOD Instancing Detection Parameters ---
            else if (key == "hlod_geometry_tolerance" || key == "hlod_tolerance") {
                try {
                    config.hlodGeometryTolerance = std::stod(value);
                    config.hlodGeometryToleranceSet = true;
                } catch (const std::exception& e) {
                    GltfInstancing::logWarning("Invalid value for '" + key + "' in config file (line " + std::to_string(lineNumber) + "): " + value + ". Error: " + e.what());
                }
            } else if (key == "hlod_normal_tolerance") {
                try {
                    config.hlodNormalTolerance = std::stod(value);
                    if (config.hlodNormalTolerance < 0.0) {
                        GltfInstancing::logWarning("Negative hlod_normal_tolerance in config (line " + std::to_string(lineNumber) + ") adjusted to 0.0.");
                        config.hlodNormalTolerance = 0.0;
                    }
                    config.hlodNormalToleranceSet = true;
                } catch (const std::exception& e) {
                    GltfInstancing::logWarning("Invalid value for 'hlod_normal_tolerance' in config file (line " + std::to_string(lineNumber) + "): " + value + ". Error: " + e.what());
                }
            } else if (key == "hlod_skip_attribute_data_hash") {
                config.hlodAttributesToSkipDataHash = splitAndTrim(value, ',');
                config.hlodAttributesToSkipDataHashSet = true;
            } else if (key == "hlod_instance_limit") {
                try {
                    config.hlodInstanceLimit = std::stoi(value);
                    if (config.hlodInstanceLimit < 1) {
                        GltfInstancing::logWarning("Invalid value for 'hlod_instance_limit' (must be >= 1) in config file (line " + std::to_string(lineNumber) + "): " + value + ". Using default 2.");
                        config.hlodInstanceLimit = 2;
                    }
                    config.hlodInstanceLimitSet = true;
                } catch (const std::exception& e) {
                    GltfInstancing::logWarning("Invalid value for 'hlod_instance_limit' in config file (line " + std::to_string(lineNumber) + "): " + value + ". Error: " + e.what());
                }
            } else if (key == "hlod_allow_non_uniform_scale_instancing") {
                std::transform(value.begin(), value.end(), value.begin(), ::tolower);
                if (value == "true" || value == "1" || value == "yes") {
                    config.hlodAllowNonUniformScaleInstancing = true;
                } else if (value == "false" || value == "0" || value == "no") {
                    config.hlodAllowNonUniformScaleInstancing = false;
                } else {
                    GltfInstancing::logWarning("Invalid boolean value for 'hlod_allow_non_uniform_scale_instancing' in config file (line " + std::to_string(lineNumber) + "): " + value);
                }
            }
            else {
                GltfInstancing::logWarning("Unknown configuration key in config file (line " + std::to_string(lineNumber) + "): " + key);
            }
        } else {
            GltfInstancing::logWarning("Malformed line in config file (line " + std::to_string(lineNumber) + "): " + line);
        }
    }
    configFile.close();
    GltfInstancing::logInfo("Finished loading configuration from: " + configFilePath);
    return true;
}

void printUsage(const char* progName) {
    GltfInstancing::logInfo("Usage: " + std::string(progName) + " --input_directory <path> [options]");
    GltfInstancing::logInfo("");
    GltfInstancing::logInfo("Required Arguments:");
    GltfInstancing::logInfo("  --input_directory <path>:            Directory containing GLB files to process.");
    GltfInstancing::logInfo("");
    GltfInstancing::logInfo("Optional Arguments:");
    GltfInstancing::logInfo("  --output_directory <path>:           Directory where processed files will be saved. Defaults to '<input_directory>/processed_output'.");
    GltfInstancing::logInfo("  --config <file_path>:                Path to a configuration file to load settings from.");
    GltfInstancing::logInfo("  --log-level <level>:                 Set log verbosity. Options: NONE, ERROR, WARNING, INFO, DEBUG, VERBOSE. Default: INFO.");
    GltfInstancing::logInfo("  --tolerance <value>:                 Geometric tolerance for POSITION comparison (e.g., 0.01). Default: 0.0.");
    GltfInstancing::logInfo("  --skip-attribute-data-hash <attrs>:  Comma-separated attributes (e.g., NORMAL,TEXCOORD_0) to skip data hashing for.");
    GltfInstancing::logInfo("                                       POSITION is always skipped if tolerance > 0.");
    GltfInstancing::logInfo("  --normal-tolerance <value>:          Tolerance for NORMAL vector comparison. Default: 0.0.");
    GltfInstancing::logInfo("  --merge-all-glb:                     Merge all GLB outputs into a single file per type. Default: false.");
    GltfInstancing::logInfo("  --instance-limit <value>:            Minimum number of instances to form a group. Default: 2.");
    GltfInstancing::logInfo("  --mesh-segmentation:                 Export each mesh as a separate GLB file. Default: false.");
    GltfInstancing::logInfo("  --csv-dir <path>:                    Path to directory with CSV files for post-processing.");
    GltfInstancing::logInfo("  --enable-quadtree:                   Enable Quadtree HLOD pipeline. Default: false.");
    GltfInstancing::logInfo("  --quadtree-max-depth <value>:        Max depth for Quadtree. Default: 6.");
    GltfInstancing::logInfo("  --quadtree-max-objs <value>:         Max objects per tile for Quadtree splitting. Default: 50.");
    GltfInstancing::logInfo("");
    GltfInstancing::logInfo("Experiment Mode Options:");
    GltfInstancing::logInfo("  --enable-experiment-mode:            Enable experiment mode to organize outputs for comparison.");
    GltfInstancing::logInfo("  --use-symbolic-links:                Use symbolic links instead of copying files (saves disk space).");
    GltfInstancing::logInfo("  --run-cross-glb-hlod-experiment:     Run Experiment 6: Cross-GLB HLOD comparison.");
    GltfInstancing::logInfo("  --experiment-dataset-name <name>:    Dataset name for experiment organization.");
    GltfInstancing::logInfo("  --experiment-strategy-id <id>:       Strategy ID for experiment organization.");
    GltfInstancing::logInfo("");
    GltfInstancing::logInfo("HLOD Instancing Detection Parameters (Independent from Stage 1):");
    GltfInstancing::logInfo("  --hlod-tolerance <value>:             Geometric tolerance for HLOD instancing detection.");
    GltfInstancing::logInfo("                                       If not set, uses Stage 1 tolerance.");
    GltfInstancing::logInfo("  --hlod-normal-tolerance <value>:    Normal tolerance for HLOD instancing detection.");
    GltfInstancing::logInfo("                                       If not set, uses Stage 1 normal tolerance.");
    GltfInstancing::logInfo("  --hlod-skip-attribute-data-hash <attrs>: Attributes to skip for HLOD detection.");
    GltfInstancing::logInfo("                                       If not set, uses Stage 1 attributes.");
    GltfInstancing::logInfo("  --hlod-instance-limit <value>:        Instance limit for HLOD detection. Default: uses Stage 1 limit.");
    GltfInstancing::logInfo("  --hlod-allow-non-uniform-scale-instancing: Allow non-uniform scale for HLOD.");
}

struct CsvEntry {
    std::string meshHash;
    std::string elementId;
};

struct ResultEntry {
    std::string meshNameOrHash;
    std::string componentId;
    std::string status;
};

// Helper function to read a CSV file
bool loadCsvEntries(const std::filesystem::path& csvPath, std::vector<CsvEntry>& entries) {
    std::ifstream file(csvPath);
    if (!file.is_open()) {
        GltfInstancing::logError("Could not open CSV file: " + csvPath.string());
        return false;
    }

    std::string line;
    // Skip header
    if (!std::getline(file, line)) {
        GltfInstancing::logWarning("CSV file is empty or could not read header: " + csvPath.string());
        return true; // Not a failure, just empty
    }

    int lineNumber = 1;
    while (std::getline(file, line)) {
        lineNumber++;
        if (line.empty() || line.find_first_not_of(" \t\r\n") == std::string::npos) {
            continue; // Skip empty lines
        }

        std::stringstream ss(line);
        std::string meshHash, elementId;

        if (std::getline(ss, meshHash, ',') && std::getline(ss, elementId)) {
            // Trim whitespace
            meshHash.erase(0, meshHash.find_first_not_of(" \t\r\n"));
            meshHash.erase(meshHash.find_last_not_of(" \t\r\n") + 1);
            elementId.erase(0, elementId.find_first_not_of(" \t\r\n"));
            elementId.erase(elementId.find_last_not_of(" \t\r\n") + 1);

            if (!meshHash.empty()) {
                entries.push_back({meshHash, elementId});
            } else {
                GltfInstancing::logWarning("Skipping row " + std::to_string(lineNumber) + " in " + csvPath.filename().string() + " due to empty mesh hash.");
            }
        } else {
            GltfInstancing::logWarning("Skipping malformed row " + std::to_string(lineNumber) + " in " + csvPath.filename().string());
        }
    }
    return true;
}

// Helper to count total nodes recursively
int countTotalNodes(const CesiumGltf::Model& model, int32_t nodeIndex) {
    if (nodeIndex < 0 || static_cast<size_t>(nodeIndex) >= model.nodes.size()) return 0;
    int count = 1; // Count self
    for (int32_t child : model.nodes[nodeIndex].children) {
        count += countTotalNodes(model, child);
    }
    return count;
}

// Forward declarations
void writeInstancingAnalysisCsvEnhanced(
    const ToolConfiguration& config,
    int inputModels,
    size_t initialNodes, size_t initialMeshes, size_t initialInstances,
    size_t instancedGroups, size_t finalInstances, size_t nonInstancedMeshes,
    size_t finalNodes, size_t finalMeshes, size_t totalDisplayedMeshes,
    double nodeReduction, double finalInstancingRatio, double instancingIncrease,
    double inputFileSizeMB, double outputFileSizeMB,
    const std::string& datasetName,
    const std::string& strategyId);

// 从 detection 结果计算统计量，供 CSV 更新使用
void updateInstancingAnalysisCsvWithFileSizes(
    const ToolConfiguration& config,
    const std::vector<GltfInstancing::LoadedGltfModel>& loadedModels,
    const GltfInstancing::InstancingDetectionResult& detectionResult,
    double outputFileSizeMB);

// Per-GLB stats for optimization summary
struct PerGlbStats {
    size_t finalInstances = 0;
    size_t nonInstancedCount = 0;
    size_t finalMeshes = 0;  // instanced groups contributed to + non-instanced count
};

// Helper to write human-readable optimization summary (with optional per-GLB section)
void writeInstancingOptimizationSummary(
    const ToolConfiguration& config,
    size_t initialNodes, size_t finalNodes,
    size_t initialMeshes, size_t finalMeshes,
    size_t initialInstances, size_t finalInstances,
    size_t totalDisplayedMeshes,
    double nodeReduction,
    double finalInstancingRatio,
    double instancingIncrease,
    const std::vector<GltfInstancing::LoadedGltfModel>* loadedModels = nullptr,
    const std::vector<PerGlbStats>* perGlbStats = nullptr) {

    const long long savedNodes = static_cast<long long>(initialNodes) - static_cast<long long>(finalNodes);
    const long long savedMeshes = static_cast<long long>(initialMeshes) - static_cast<long long>(finalMeshes);
    const double meshReduction = (initialMeshes > 0)
        ? (1.0 - static_cast<double>(finalMeshes) / static_cast<double>(initialMeshes)) * 100.0
        : 0.0;
    const double initialInstancingRatio = (totalDisplayedMeshes > 0)
        ? (static_cast<double>(initialInstances) / static_cast<double>(totalDisplayedMeshes)) * 100.0
        : 0.0;

    std::filesystem::path summaryPath = OutputPaths::instancingOptimizationSummary(config);

    std::ofstream summaryFile(summaryPath);
    if (!summaryFile.is_open()) {
        GltfInstancing::logError("Failed to write optimization summary to: " + summaryPath.string());
        return;
    }

    summaryFile << "Instancing Optimization Summary\n";
    summaryFile << "========================================\n\n";
    summaryFile << std::fixed << std::setprecision(2);
    summaryFile << "Initial Nodes: " << initialNodes << "\n";
    summaryFile << "Final Nodes: " << finalNodes << "\n";
    summaryFile << "Node Reduction Count: " << savedNodes << "\n";
    summaryFile << "Node Reduction Rate (%): " << nodeReduction << "\n\n";

    summaryFile << "Initial Meshes: " << initialMeshes << "\n";
    summaryFile << "Final Meshes: " << finalMeshes << "\n";
    summaryFile << "Mesh Reduction Count: " << savedMeshes << "\n";
    summaryFile << "Mesh Reduction Rate (%): " << meshReduction << "\n\n";

    summaryFile << "Initial Instancing Ratio (%): " << initialInstancingRatio << "\n";
    summaryFile << "Final Instancing Ratio (%): " << finalInstancingRatio << "\n";
    summaryFile << "Instancing Ratio Increase (%): " << instancingIncrease << "\n";
    summaryFile << "Final Instanced Mesh Count: " << finalInstances << "\n";
    summaryFile << "Total Displayed Mesh Count: " << totalDisplayedMeshes << "\n";

    if (loadedModels && perGlbStats && perGlbStats->size() == loadedModels->size()) {
        summaryFile << "\n\n--- Per-GLB Statistics ---\n\n";
        for (size_t i = 0; i < loadedModels->size(); ++i) {
            const auto& lm = (*loadedModels)[i];
            const auto& stats = (*perGlbStats)[i];
            summaryFile << "GLB " << (i + 1) << ": " << lm.originalPath.filename().string() << "\n";
            summaryFile << "  Final Instances: " << stats.finalInstances << "\n";
            summaryFile << "  Non-instanced Count: " << stats.nonInstancedCount << "\n";
            summaryFile << "  Final Meshes (groups+nonInst): " << stats.finalMeshes << "\n\n";
        }
    }

    summaryFile.close();
    GltfInstancing::logInfo("Instancing optimization summary written to: " + summaryPath.string());
}

// Helper to write CSV analysis report
void writeAnalysisCsv(const ToolConfiguration& config, 
                     const std::vector<GltfInstancing::LoadedGltfModel>& loadedModels,
                     const GltfInstancing::InstancingDetectionResult& result) {
    
    // 1. Calculate Initial Stats
    size_t inputModels = loadedModels.size();
    size_t initialNodes = 0;
    size_t initialMeshes = 0;
    size_t initialInstances = 0; // Items using EXT_mesh_gpu_instancing in input

    for (const auto& loadedModel : loadedModels) {
        initialMeshes += loadedModel.model.meshes.size();
        if (!loadedModel.model.scenes.empty()) {
            int sceneIdx = loadedModel.model.scene >= 0 ? loadedModel.model.scene : 0;
            if (static_cast<size_t>(sceneIdx) < loadedModel.model.scenes.size()) {
                for (int32_t root : loadedModel.model.scenes[sceneIdx].nodes) {
                    initialNodes += countTotalNodes(loadedModel.model, root);
                }
            }
        }
        
        // Count initial instances (EXT_mesh_gpu_instancing)
        for (const auto& node : loadedModel.model.nodes) {
             auto it = node.extensions.find("EXT_mesh_gpu_instancing");
             if (it != node.extensions.end()) {
                 try {
                     // We need to peek into the extension to get count. 
                     // Since we don't have easy access to the exact count without parsing attributes again,
                     // we'll try to find a common accessor count.
                     // Simpler approach: InstancingDetector does this. 
                     // But here we just want a rough count.
                     // Let's iterate attributes map in the extension JSON object if possible, or use Cesium's type.
                     // The ExtensionExtMeshGpuInstancing is a struct.
                     const auto* extData = std::any_cast<CesiumGltf::ExtensionExtMeshGpuInstancing>(&it->second);
                     if (extData) {
                         // Find any accessor and get its count
                         for (auto const& [key, accessorId] : extData->attributes) {
                             if (accessorId >= 0 && static_cast<size_t>(accessorId) < loadedModel.model.accessors.size()) {
                                 initialInstances += loadedModel.model.accessors[accessorId].count;
                                 break; // Found one valid accessor, that's the count
                             }
                         }
                     }
                 } catch (...) {}
             }
        }
    }

    // 2. Calculate Final Stats
    size_t instancedGroups = result.instancedGroups.size();
    size_t finalInstances = 0;
    for(const auto& group : result.instancedGroups) {
        finalInstances += group.instances.size();
    }
    size_t nonInstancedMeshes = result.nonInstancedMeshes.size();
    
    // Final Nodes: Each group is 1 node + Each non-instanced mesh is 1 node
    size_t finalNodes = instancedGroups + nonInstancedMeshes;
    
    // Final Meshes: Each group has 1 representative mesh + Each non-instanced has 1 mesh
    // (Assuming max reuse, but non-instanced might share meshes. However, "Final Meshes" usually refers to unique mesh data written)
    // For this report, let's treat it as (Unique Rep Meshes + Unique Non-Instanced Meshes).
    // But simplified: Instanced Groups Count + Non-Instanced Count.
    size_t finalMeshes = instancedGroups + nonInstancedMeshes; 

    // Total Displayed Meshes: All instances + All non-instanced items
    size_t totalDisplayedMeshes = finalInstances + nonInstancedMeshes;

    // 2b. Per-GLB stats
    std::vector<PerGlbStats> perGlbStats(loadedModels.size());
    std::vector<std::set<size_t>> perGlbGroupIndices(loadedModels.size());  // groups this GLB contributed to
    for (size_t gIdx = 0; gIdx < result.instancedGroups.size(); ++gIdx) {
        for (const auto& inst : result.instancedGroups[gIdx].instances) {
            int32_t src = inst.sourceModelIndexInLoadedModels;
            if (src >= 0 && static_cast<size_t>(src) < loadedModels.size()) {
                perGlbStats[src].finalInstances++;
                perGlbGroupIndices[src].insert(gIdx);
            }
        }
    }
    for (const auto& ni : result.nonInstancedMeshes) {
        int32_t src = ni.sourceModelIndexInLoadedModels;
        if (src >= 0 && static_cast<size_t>(src) < loadedModels.size()) {
            perGlbStats[src].nonInstancedCount++;
        }
    }
    for (size_t i = 0; i < perGlbStats.size(); ++i) {
        perGlbStats[i].finalMeshes = perGlbGroupIndices[i].size() + perGlbStats[i].nonInstancedCount;
    }

    // 3. Ratios
    double nodeReduction = (initialNodes > 0) ? (1.0 - (double)finalNodes / (double)initialNodes) * 100.0 : 0.0;
    double initialInstancingRatio = (totalDisplayedMeshes > 0) ? ((double)initialInstances / (double)totalDisplayedMeshes) * 100.0 : 0.0;
    double finalInstancingRatio = (totalDisplayedMeshes > 0) ? ((double)finalInstances / (double)totalDisplayedMeshes) * 100.0 : 0.0;
    double instancingIncrease = finalInstancingRatio - initialInstancingRatio;

    // 3b. 计算输入文件总大小 SO (MB)
    double inputFileSizeMB = 0.0;
    for (const auto& lm : loadedModels) {
        try {
            if (std::filesystem::exists(lm.originalPath)) {
                inputFileSizeMB += static_cast<double>(std::filesystem::file_size(lm.originalPath)) / (1024.0 * 1024.0);
            }
        } catch (...) {}
    }

    // 4. Write CSV - 使用 Metric,Value,Unit 格式，包含 SO/SC/CR/Eo/Ec/ECR/EIc/IR/Ic/PIC 等指标
    double outputFileSizeMB = 0.0;  // GLB 写入后更新
    std::string datasetName = config.enableExperimentMode ? (config.experimentDatasetName.empty() ? "default_dataset" : config.experimentDatasetName) : "";
    std::string strategyId = config.enableExperimentMode ? (config.experimentStrategyId.empty() ? "default" : config.experimentStrategyId) : "";
    writeInstancingAnalysisCsvEnhanced(
        config,
        inputModels,
        initialNodes, initialMeshes, initialInstances,
        instancedGroups, finalInstances, nonInstancedMeshes,
        finalNodes, finalMeshes, totalDisplayedMeshes,
        nodeReduction, finalInstancingRatio, instancingIncrease,
        inputFileSizeMB, outputFileSizeMB,
        datasetName,
        strategyId
    );
    GltfInstancing::logInfo("Instancing analysis CSV written to: " + OutputPaths::instancingAnalysisCsv(config).string());

    // 5. Write human-readable optimization summary (with per-GLB section)
    writeInstancingOptimizationSummary(
        config,
        initialNodes, finalNodes,
        initialMeshes, finalMeshes,
        initialInstances, finalInstances,
        totalDisplayedMeshes,
        nodeReduction,
        finalInstancingRatio,
        instancingIncrease,
        &loadedModels,
        &perGlbStats
    );

    // 5b. Write per-GLB CSV
    std::filesystem::path perGlbCsvPath = OutputPaths::instancingPerGlbCsv(config);
    std::ofstream perGlbCsv(perGlbCsvPath);
    if (perGlbCsv.is_open()) {
        perGlbCsv << "GLB File,Final Instances,Non-instanced Count,Final Meshes\n";
        for (size_t i = 0; i < loadedModels.size(); ++i) {
            perGlbCsv << loadedModels[i].originalPath.filename().string() << ","
                      << perGlbStats[i].finalInstances << ","
                      << perGlbStats[i].nonInstancedCount << ","
                      << perGlbStats[i].finalMeshes << "\n";
        }
        perGlbCsv.close();
        GltfInstancing::logInfo("Per-GLB instancing stats written to: " + perGlbCsvPath.string());
    }

}

// Enhanced version using new experiment framework
void writeInstancingAnalysisCsvEnhanced(
    const ToolConfiguration& config,
    int inputModels,
    size_t initialNodes, size_t initialMeshes, size_t initialInstances,
    size_t instancedGroups, size_t finalInstances, size_t nonInstancedMeshes,
    size_t finalNodes, size_t finalMeshes, size_t totalDisplayedMeshes,
    double nodeReduction, double finalInstancingRatio, double instancingIncrease,
    double inputFileSizeMB, double outputFileSizeMB,
    const std::string& datasetName = "default",
    const std::string& strategyId = "default") {

    // 使用新框架生成标准化的CSV
    std::map<std::string, ExperimentFramework::MetricValue> metrics;
    // 文件大小指标
    metrics["SO"] = {"SO", inputFileSizeMB, "MB", "原文件大小"};
    metrics["SC"] = {"SC", outputFileSizeMB, "MB", "优化后大小"};
    double cr = (inputFileSizeMB > 0) ? (outputFileSizeMB / inputFileSizeMB) : 0.0;
    metrics["CR"] = {"CR", cr, "", "压缩比率 SC/SO"};
    // 实体数量指标
    double eo = static_cast<double>(initialNodes);
    double ec = static_cast<double>(finalNodes);
    metrics["Eo"] = {"Eo", eo, "count", "优化前实体数"};
    metrics["Ec"] = {"Ec", ec, "count", "优化后实体数"};
    double ecr = (eo > 0) ? (ec / eo) : 0.0;
    metrics["ECR"] = {"ECR", ecr, "", "优化前后实体数量比值 Ec/Eo"};
    // 实例化指标
    double eic = static_cast<double>(finalInstances);
    metrics["EIc"] = {"EIc", eic, "count", "实例化构件所表征的实体总数"};
    metrics["IR"] = {"IR", finalInstancingRatio, "%", "实例化率"};
    double ic = static_cast<double>(instancedGroups);
    metrics["Ic"] = {"Ic", ic, "count", "优化后实例化构件的数量"};
    double pic = (instancedGroups > 0) ? (static_cast<double>(finalInstances) / instancedGroups) : 0.0;
    metrics["PIC"] = {"PIC", pic, "", "平均每个实例化构件表达的实体数量"};
    metrics["Input Models"] = {"Input Models", static_cast<double>(inputModels), "count", "Number of input GLB files"};
    metrics["Initial Nodes"] = {"Initial Nodes", static_cast<double>(initialNodes), "count", "Initial scene graph nodes"};
    metrics["Initial Meshes"] = {"Initial Meshes", static_cast<double>(initialMeshes), "count", "Initial mesh primitives"};
    metrics["Initial Instances"] = {"Initial Instances", static_cast<double>(initialInstances), "count", "Initial instanced meshes"};
    metrics["Instanced Groups"] = {"Instanced Groups", static_cast<double>(instancedGroups), "count", "Detected instancing groups"};
    metrics["Final Instances"] = {"Final Instances", static_cast<double>(finalInstances), "count", "Final instanced meshes"};
    metrics["Non-instanced Meshes"] = {"Non-instanced Meshes", static_cast<double>(nonInstancedMeshes), "count", "Non-instanced mesh count"};
    metrics["Final Nodes"] = {"Final Nodes", static_cast<double>(finalNodes), "count", "Final scene graph nodes"};
    metrics["Final Meshes"] = {"Final Meshes", static_cast<double>(finalMeshes), "count", "Final mesh count"};
    metrics["Total Displayed Meshes"] = {"Total Displayed Meshes", static_cast<double>(totalDisplayedMeshes), "count", "Total visible meshes"};
    metrics["Node Reduction (%)"] = {"Node Reduction (%)", nodeReduction, "%", "Percentage of nodes reduced"};
    double initialInstancingRatio = finalInstancingRatio - instancingIncrease;
    metrics["Initial Instancing Ratio (%)"] = {"Initial Instancing Ratio (%)", initialInstancingRatio, "%", "Initial instancing ratio"};
    metrics["Final Instancing Ratio (%)"] = {"Final Instancing Ratio (%)", finalInstancingRatio, "%", "Final instancing ratio"};
    metrics["Instancing Increase (%)"] = {"Instancing Increase (%)", instancingIncrease, "%", "Instancing improvement"};
    metrics["File Size Input (MB)"] = {"File Size Input (MB)", inputFileSizeMB, "MB", "Input file size"};
    metrics["File Size Output (MB)"] = {"File Size Output (MB)", outputFileSizeMB, "MB", "Output file size"};
    double fileReduction = (inputFileSizeMB > 0) ? (1.0 - outputFileSizeMB / inputFileSizeMB) * 100.0 : 0.0;
    metrics["File Size Reduction (%)"] = {"File Size Reduction (%)", fileReduction, "%", "File size reduction"};

    // 生成标准化CSV路径
    std::filesystem::path csvPath = OutputPaths::instancingAnalysisCsv(config);
    ExperimentFramework::CsvReportGenerator::writeInstancingAnalysis(csvPath, metrics);

    // 如果使用实验模式，同时生成到实验目录
    if (config.enableExperimentMode && !datasetName.empty()) {
        std::filesystem::path experimentsBaseDir = std::filesystem::path(config.outputDirectory) / "experiments";
        GltfInstancing::logInfo("Creating experiment directory structure at: " + experimentsBaseDir.string());

        ExperimentFramework::ExperimentDirectoryManager expManager(experimentsBaseDir);

        ExperimentFramework::StrategyInfo strategy;
        strategy.id = strategyId;
        strategy.name = strategyId;
        strategy.description = "Instancing detection with tolerance " + std::to_string(config.geometryTolerance);
        strategy.parameters["tolerance"] = std::to_string(config.geometryTolerance);
        strategy.parameters["instance_limit"] = std::to_string(config.instanceLimit);
        strategy.parameters["normal_tolerance"] = std::to_string(config.normalTolerance);

        GltfInstancing::logInfo("Creating experiment structure for INSTANCING_STRATEGY, dataset: " + datasetName + ", strategy: " + strategyId);
        auto expDir = expManager.createExperimentStructure(
            ExperimentFramework::ExperimentType::INSTANCING_STRATEGY,
            datasetName, strategy);
        GltfInstancing::logInfo("Experiment directory created at: " + expDir.string());

        // 写入标准化CSV
        std::filesystem::path expCsvPath = expDir / "instancing_analysis.csv";
        ExperimentFramework::CsvReportGenerator::writeInstancingAnalysis(expCsvPath, metrics);
        GltfInstancing::logInfo("Experiment CSV written to: " + expCsvPath.string());

        // 写入配置
        std::filesystem::path configPath = expDir / "config.json";
        ExperimentFramework::ConfigGenerator::writeConfigJson(configPath, config, strategy);
        GltfInstancing::logInfo("Experiment config written to: " + configPath.string());

        // 生成策略README
        std::filesystem::path readmePath = expDir / "README.md";
        ExperimentFramework::ReadmeGenerator::writeStrategyReadme(readmePath, strategy, metrics);
        GltfInstancing::logInfo("Experiment README written to: " + readmePath.string());
    } else {
        if (!config.enableExperimentMode) {
            GltfInstancing::logInfo("Experiment mode disabled, skipping experiment directory creation.");
        } else if (datasetName.empty()) {
            GltfInstancing::logWarning("Dataset name is empty, skipping experiment directory creation.");
        }
    }
}

void updateInstancingAnalysisCsvWithFileSizes(
    const ToolConfiguration& config,
    const std::vector<GltfInstancing::LoadedGltfModel>& loadedModels,
    const GltfInstancing::InstancingDetectionResult& detectionResult,
    double outputFileSizeMB) {

    // 计算 inputFileSizeMB
    double inputFileSizeMB = 0.0;
    for (const auto& lm : loadedModels) {
        try {
            if (std::filesystem::exists(lm.originalPath)) {
                inputFileSizeMB += static_cast<double>(std::filesystem::file_size(lm.originalPath)) / (1024.0 * 1024.0);
            }
        } catch (...) {}
    }

    // 计算 stats（与 writeAnalysisCsv 相同逻辑）
    size_t inputModels = loadedModels.size();
    size_t initialNodes = 0, initialMeshes = 0, initialInstances = 0;
    for (const auto& loadedModel : loadedModels) {
        initialMeshes += loadedModel.model.meshes.size();
        if (!loadedModel.model.scenes.empty()) {
            int sceneIdx = loadedModel.model.scene >= 0 ? loadedModel.model.scene : 0;
            if (static_cast<size_t>(sceneIdx) < loadedModel.model.scenes.size()) {
                for (int32_t root : loadedModel.model.scenes[sceneIdx].nodes) {
                    initialNodes += countTotalNodes(loadedModel.model, root);
                }
            }
        }
        for (const auto& node : loadedModel.model.nodes) {
            auto it = node.extensions.find("EXT_mesh_gpu_instancing");
            if (it != node.extensions.end()) {
                try {
                    const auto* extData = std::any_cast<CesiumGltf::ExtensionExtMeshGpuInstancing>(&it->second);
                    if (extData) {
                        for (auto const& [key, accessorId] : extData->attributes) {
                            if (accessorId >= 0 && static_cast<size_t>(accessorId) < loadedModel.model.accessors.size()) {
                                initialInstances += loadedModel.model.accessors[accessorId].count;
                                break;
                            }
                        }
                    }
                } catch (...) {}
            }
        }
    }
    size_t instancedGroups = detectionResult.instancedGroups.size();
    size_t finalInstances = 0;
    for (const auto& group : detectionResult.instancedGroups) finalInstances += group.instances.size();
    size_t nonInstancedMeshes = detectionResult.nonInstancedMeshes.size();
    size_t finalNodes = instancedGroups + nonInstancedMeshes;
    size_t finalMeshes = instancedGroups + nonInstancedMeshes;
    size_t totalDisplayedMeshes = finalInstances + nonInstancedMeshes;
    double nodeReduction = (initialNodes > 0) ? (1.0 - (double)finalNodes / (double)initialNodes) * 100.0 : 0.0;
    double finalInstancingRatio = (totalDisplayedMeshes > 0) ? ((double)finalInstances / (double)totalDisplayedMeshes) * 100.0 : 0.0;
    double initialInstancingRatio = (totalDisplayedMeshes > 0) ? ((double)initialInstances / (double)totalDisplayedMeshes) * 100.0 : 0.0;
    double instancingIncrease = finalInstancingRatio - initialInstancingRatio;

    std::string datasetName = config.enableExperimentMode ? (config.experimentDatasetName.empty() ? "default_dataset" : config.experimentDatasetName) : "";
    std::string strategyId = config.enableExperimentMode ? (config.experimentStrategyId.empty() ? "default" : config.experimentStrategyId) : "";

    writeInstancingAnalysisCsvEnhanced(config, inputModels,
        initialNodes, initialMeshes, initialInstances,
        instancedGroups, finalInstances, nonInstancedMeshes,
        finalNodes, finalMeshes, totalDisplayedMeshes,
        nodeReduction, finalInstancingRatio, instancingIncrease,
        inputFileSizeMB, outputFileSizeMB, datasetName, strategyId);
    GltfInstancing::logInfo("Updated instancing_analysis.csv with SO, SC, CR (file sizes)");
}

struct LodStats {
    int level;
    double fileSizeMB;
    int uniqueMeshes;
    size_t totalInstances;
    size_t totalVertices;
};

// Helper function to get HLOD instancing detection parameters
// If HLOD-specific parameters are not set, use Stage 1 parameters
struct HlodInstancingParams {
    double geometryTolerance;
    double normalTolerance;
    std::set<std::string> attributesToSkipDataHash;
    int instanceLimit;
    bool allowNonUniformScaleInstancing;
};

HlodInstancingParams getHlodInstancingParams(const ToolConfiguration& config) {
    HlodInstancingParams params;
    
    // Geometry tolerance: use HLOD value if set, otherwise use Stage 1 value
    params.geometryTolerance = config.hlodGeometryToleranceSet ? 
        config.hlodGeometryTolerance : config.geometryTolerance;
    
    // Normal tolerance: use HLOD value if set, otherwise use Stage 1 value
    params.normalTolerance = config.hlodNormalToleranceSet ? 
        config.hlodNormalTolerance : config.normalTolerance;
    
    // Attributes to skip: use HLOD value if set, otherwise use Stage 1 value
    params.attributesToSkipDataHash = config.hlodAttributesToSkipDataHashSet ? 
        config.hlodAttributesToSkipDataHash : config.attributesToSkipDataHash;
    
    // Instance limit: use HLOD value if set, otherwise use Stage 1 value
    params.instanceLimit = config.hlodInstanceLimitSet ? 
        config.hlodInstanceLimit : config.instanceLimit;
    
    // Allow non-uniform scale: use HLOD value (always has a default)
    params.allowNonUniformScaleInstancing = config.hlodAllowNonUniformScaleInstancing;
    
    return params;
}

// Helper to write LOD analysis report
void writeLodAnalysisCsv(const ToolConfiguration& config, 
                        const std::vector<LodStats>& stats,
                        double originalFileSizeMB,
                        size_t originalVertices,
                        size_t originalInstances) {
    std::filesystem::path csvPath = OutputPaths::instanceLodAnalysisCsv(config);
    std::ofstream csvFile(csvPath);
    
    if (csvFile.is_open()) {
        csvFile << "# Instance LOD Analysis\n";
        csvFile << "Metric,Original (Input),Instanced (LOD5),LOD4 (Variant),LOD3 (Class),LOD2 (Abstract),LOD1 (Proxy)\n";
        
        // File Size
        csvFile << "File Size (MB)," << std::fixed << std::setprecision(2) << originalFileSizeMB;
        // Map stats to levels (assuming LOD5 is first in stats vector if sorted, or find by level)
        // We expect stats to contain LOD5, LOD4... LOD1
        // Let's create a map for easy lookup
        std::map<int, LodStats> statsMap;
        for (const auto& s : stats) statsMap[s.level] = s;

        for (int l = 5; l >= 1; --l) {
            if (statsMap.count(l)) csvFile << "," << statsMap[l].fileSizeMB;
            else csvFile << ",-";
        }
        csvFile << "\n";

        // Unique Meshes
        csvFile << "Unique Meshes,-"; // Original unique meshes is hard to count exactly without processing, maybe N/A or from detection
        for (int l = 5; l >= 1; --l) {
            if (statsMap.count(l)) csvFile << "," << statsMap[l].uniqueMeshes;
            else csvFile << ",-";
        }
        csvFile << "\n";

        // Total Instances
        csvFile << "Total Instances," << originalInstances;
        for (int l = 5; l >= 1; --l) {
            if (statsMap.count(l)) csvFile << "," << statsMap[l].totalInstances;
            else csvFile << ",-";
        }
        csvFile << "\n";

        // Vertices (Loaded/Displayed)
        // Note: For Instanced, this is usually (Unique Mesh Verts). Total scene verts would be (Unique * Instances) if flattened.
        // Let's report Unique Mesh Vertices (Loaded GPU Memory metric)
        csvFile << "Vertices (Loaded)," << originalVertices;
        for (int l = 5; l >= 1; --l) {
            if (statsMap.count(l)) csvFile << "," << statsMap[l].totalVertices;
            else csvFile << ",-";
        }
        csvFile << "\n";

        // Reduction Rate (File Size vs Original)
        csvFile << "Reduction Rate (%),-";
        for (int l = 5; l >= 1; --l) {
            if (statsMap.count(l) && originalFileSizeMB > 0) {
                double rate = (1.0 - statsMap[l].fileSizeMB / originalFileSizeMB) * 100.0;
                csvFile << "," << rate << "%";
            } else {
                csvFile << ",-";
            }
        }
        csvFile << "\n";

        csvFile.close();
        GltfInstancing::logInfo("LOD analysis CSV written to: " + csvPath.string());
    } else {
        GltfInstancing::logError("Failed to write LOD analysis CSV to: " + csvPath.string());
    }

    // 实验模式：生成LOD策略实验目录
    if (config.enableExperimentMode) {
        std::string datasetName = config.experimentDatasetName.empty() ? "default_dataset" : config.experimentDatasetName;
        std::string strategyId = config.experimentStrategyId.empty() ? "InstancingLOD" : config.experimentStrategyId;

        std::filesystem::path experimentsBaseDir = std::filesystem::path(config.outputDirectory) / "experiments";
        ExperimentFramework::ExperimentDirectoryManager expManager(experimentsBaseDir);

        ExperimentFramework::StrategyInfo strategy;
        strategy.id = strategyId;
        strategy.name = "Instance-based Semantic LOD";
        strategy.description = "Semantic-driven 5-level LOD generation with GPU instancing";
        strategy.parameters["lod_levels"] = std::to_string(config.lodLevelCount);
        strategy.parameters["target_sse"] = std::to_string(config.targetScreenSSE);

        GltfInstancing::logInfo("Creating experiment structure for LOD_STRATEGY, dataset: " + datasetName + ", strategy: " + strategyId);
        auto expDir = expManager.createExperimentStructure(
            ExperimentFramework::ExperimentType::LOD_STRATEGY,
            datasetName, strategy);
        GltfInstancing::logInfo("LOD experiment directory created at: " + expDir.string());

        // 写入 Instance LOD 分析CSV到实验目录
        std::filesystem::path expCsvPath = expDir / "instance_lod_analysis.csv";
        std::ofstream expCsvFile(expCsvPath);
        if (expCsvFile.is_open()) {
            expCsvFile << "Metric,Original (Input),Instanced (LOD5),LOD4 (Variant),LOD3 (Class),LOD2 (Abstract),LOD1 (Proxy)\n";
            expCsvFile << "File Size (MB)," << std::fixed << std::setprecision(2) << originalFileSizeMB;
            std::map<int, LodStats> statsMap;
            for (const auto& s : stats) statsMap[s.level] = s;
            for (int l = 5; l >= 1; --l) {
                if (statsMap.count(l)) expCsvFile << "," << statsMap[l].fileSizeMB;
                else expCsvFile << ",-";
            }
            expCsvFile << "\n";
            expCsvFile.close();
            GltfInstancing::logInfo("Instance LOD experiment CSV written to: " + expCsvPath.string());
        }

        // 写入配置
        std::filesystem::path configPath = expDir / "config.json";
        ExperimentFramework::ConfigGenerator::writeConfigJson(configPath, config, strategy);

        // 生成README
        std::map<std::string, ExperimentFramework::MetricValue> lodMetrics;
        if (!stats.empty()) {
            lodMetrics["LOD Levels Generated"] = {"LOD Levels", static_cast<double>(stats.size()), "count", "Number of LOD levels"};
            lodMetrics["Original File Size (MB)"] = {"Original Size", originalFileSizeMB, "MB", "Input file size"};
            // Recreate statsMap for this scope
            std::map<int, LodStats> statsMap;
            for (const auto& s : stats) statsMap[s.level] = s;
            if (statsMap.count(1)) {
                double reduction = (originalFileSizeMB > 0) ? (1.0 - statsMap[1].fileSizeMB / originalFileSizeMB) * 100.0 : 0.0;
                lodMetrics["File Size Reduction (%)"] = {"Reduction", reduction, "%", "LOD1 vs Original"};
            }
        }
        std::filesystem::path readmePath = expDir / "README.md";
        ExperimentFramework::ReadmeGenerator::writeStrategyReadme(readmePath, strategy, lodMetrics);
        GltfInstancing::logInfo("LOD experiment README written to: " + readmePath.string());
    }
}

// Main function to process GLB against CSV files, similar to the Python script
void processCsvAgainstGlb(const ToolConfiguration& config) {
    if (!config.csvDirectorySet || config.csvDirectory.empty()) {
        GltfInstancing::logInfo("Stage 3: CSV Processing is disabled (no --csv-dir specified). Skipping.");
        return;
    }

    GltfInstancing::logInfo("Stage 3: Starting CSV processing against generated GLB.");

    // 1. Check for CSV directory
    std::filesystem::path csvDirPath(config.csvDirectory);
    if (!std::filesystem::is_directory(csvDirPath)) {
        GltfInstancing::logError("CSV directory specified does not exist or is not a directory: " + config.csvDirectory);
        return;
    }

    // 2. Locate the non-instanced GLB file from Stage 1
    std::filesystem::path nonInstancedGlbPath = OutputPaths::nonInstancedGlb(config);
    if (!std::filesystem::exists(nonInstancedGlbPath)) {
        GltfInstancing::logError("non_instanced_meshes.glb not found in output directory. Cannot perform CSV processing. Path: " + nonInstancedGlbPath.string());
        return;
    }

    // 3. Extract mesh names from the GLB
    GltfInstancing::logInfo("Reading mesh names from: " + nonInstancedGlbPath.string());
    GltfInstancing::GlbReader reader;
    std::set<std::filesystem::path> glbFileSet = {nonInstancedGlbPath};
    std::vector<GltfInstancing::LoadedGltfModel> models = reader.loadGltfModels(glbFileSet);
    
    if (models.empty()) {
        GltfInstancing::logError("Failed to load non_instanced_meshes.glb for CSV processing.");
        return;
    }

    std::set<std::string> meshNamesFromGlb;
    for (const auto& modelData : models) {
        for (const auto& mesh : modelData.model.meshes) {
            if (!mesh.name.empty()) {
                meshNamesFromGlb.insert(mesh.name);
            }
        }
    }
    GltfInstancing::logInfo("Found " + std::to_string(meshNamesFromGlb.size()) + " unique mesh names in the GLB file.");


    // 4. Find and process each CSV file
    GltfInstancing::logInfo("Scanning for CSV files in: " + csvDirPath.string());
    const std::string suffix = "_IDExport.csv";
    for (const auto& entry : std::filesystem::directory_iterator(csvDirPath)) {
        const auto& path = entry.path();
        const std::string filename = path.filename().string();
        
        // Check if the file is a regular file and ends with the required suffix
        if (entry.is_regular_file() && 
            filename.length() >= suffix.length() && 
            filename.compare(filename.length() - suffix.length(), suffix.length(), suffix) == 0) {
            
            GltfInstancing::logInfo("--- Processing CSV file: " + filename + " ---");

            // Load CSV entries
            std::vector<CsvEntry> csvEntries;
            if (!loadCsvEntries(path, csvEntries)) {
                GltfInstancing::logError("Failed to load CSV file, skipping: " + path.string());
                continue;
            }
            GltfInstancing::logInfo("Loaded " + std::to_string(csvEntries.size()) + " entries from " + filename);

            // Perform comparison
            std::vector<ResultEntry> nonInstancedMatches;
            std::vector<ResultEntry> instancedFromCsv;
            std::set<std::string> matchedGlbMeshNames;

            for (const auto& csvEntry : csvEntries) {
                if (meshNamesFromGlb.count(csvEntry.meshHash)) {
                    // Non-Instanced: mesh hash from CSV is in GLB
                    nonInstancedMatches.push_back({csvEntry.meshHash, csvEntry.elementId, "Non-Instanced"});
                    matchedGlbMeshNames.insert(csvEntry.meshHash);
                } else {
                    // Instanced: mesh hash from CSV is NOT in GLB
                    instancedFromCsv.push_back({csvEntry.meshHash, csvEntry.elementId, "Instanced"});
                }
            }

            // Find meshes from GLB that were not in any CSV entry
            std::vector<ResultEntry> instancedFromGlb;
            for (const auto& glbMeshName : meshNamesFromGlb) {
                if (matchedGlbMeshNames.find(glbMeshName) == matchedGlbMeshNames.end()) {
                    instancedFromGlb.push_back({glbMeshName, "", "Instanced"});
                }
            }
            
            GltfInstancing::logInfo("Comparison complete:");
            GltfInstancing::logInfo("  Non-Instanced (in GLB and CSV): " + std::to_string(nonInstancedMatches.size()));
            GltfInstancing::logInfo("  Instanced (in CSV only): " + std::to_string(instancedFromCsv.size()));
            GltfInstancing::logInfo("  Instanced (in GLB only): " + std::to_string(instancedFromGlb.size()));

            // Write results to a new CSV
            std::string outputFileName = path.stem().string() + "_results.csv";
            std::filesystem::path outputCsvPath = OutputPaths::resultsCsv(config, path.stem().string());

            std::ofstream outFile(outputCsvPath);
            if (!outFile.is_open()) {
                GltfInstancing::logError("Failed to open output CSV file for writing: " + outputCsvPath.string());
                continue;
            }

            outFile << "Mesh Name/Hash,Component ID,Status\n";
            for (const auto& result : nonInstancedMatches) {
                outFile << "\"" << result.meshNameOrHash << "\",\"" << result.componentId << "\",\"" << result.status << "\"\n";
            }
            for (const auto& result : instancedFromCsv) {
                outFile << "\"" << result.meshNameOrHash << "\",\"" << result.componentId << "\",\"" << result.status << "\"\n";
            }
            for (const auto& result : instancedFromGlb) {
                outFile << "\"" << result.meshNameOrHash << "\",\"" << result.componentId << "\",\"" << result.status << "\"\n";
            }

            outFile.close();
            GltfInstancing::logInfo("Results written to: " + outputCsvPath.string());
        }
    }
     GltfInstancing::logInfo("--- Finished processing all CSV files. ---");
}

int main(int argc, char* argv[]) {
    // Disable Windows Console Quick Edit Mode to prevent accidental pausing
    disableQuickEditMode();

    GltfInstancing::logInfo("GltfInstancingTool starting...");

    ToolConfiguration config;
    std::string customConfigFilePath;
    bool useCustomConfigFile = false;

    // Set default log level, can be overridden by CLI
    GltfInstancing::setLogLevel(GltfInstancing::LogLevel::LEVEL_INFO);

    // 1. First pass: Check for --config and --log-level arguments to set them up early
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--config") {
            if (i + 1 < argc) {
                customConfigFilePath = argv[++i];
                useCustomConfigFile = true;
            }
        }
        else if (arg == "--log-level") {
            if (i + 1 < argc) {
                std::string levelStr = argv[++i];
                std::transform(levelStr.begin(), levelStr.end(), levelStr.begin(), ::toupper);
                if (levelStr == "NONE") GltfInstancing::setLogLevel(GltfInstancing::LogLevel::NONE);
                else if (levelStr == "ERROR") GltfInstancing::setLogLevel(GltfInstancing::LogLevel::LEVEL_ERROR);
                else if (levelStr == "WARNING") GltfInstancing::setLogLevel(GltfInstancing::LogLevel::LEVEL_WARNING);
                else if (levelStr == "INFO") GltfInstancing::setLogLevel(GltfInstancing::LogLevel::LEVEL_INFO);
                else if (levelStr == "DEBUG") GltfInstancing::setLogLevel(GltfInstancing::LogLevel::LEVEL_DEBUG);
                else if (levelStr == "VERBOSE") GltfInstancing::setLogLevel(GltfInstancing::LogLevel::LEVEL_VERBOSE);
            }
        }
    }

    if (useCustomConfigFile) {
        GltfInstancing::logInfo("Custom configuration file specified: " + customConfigFilePath);
        if (!loadConfigurationFromFile(customConfigFilePath, config)) {
            GltfInstancing::logError("Failed to load specified configuration file: " + customConfigFilePath + ". Exiting.");
            return 1;
        }
    }

    // 2. Parse all command-line arguments (will override config file settings)
    int argIndex = 1;
    while (argIndex < argc) {
        std::string arg = argv[argIndex];

        // Options processed in the first pass can be skipped
        if (arg == "--config" || arg == "--log-level") {
            argIndex += 2;
            if (argIndex > argc) break;
            continue;
        }

        if (arg == "--input_directory") {
            if (argIndex + 1 < argc) {
                config.inputDirectory = argv[++argIndex];
                config.inputDirectorySet = true;
            }
            else {
                GltfInstancing::logError("--input_directory option (CLI) requires a path."); printUsage(argv[0]); return 1;
            }
        }
        else if (arg == "--output_directory") {
            if (argIndex + 1 < argc) {
                config.outputDirectory = argv[++argIndex];
                config.outputDirectorySet = true;
            }
            else {
                GltfInstancing::logError("--output_directory option (CLI) requires a path."); printUsage(argv[0]); return 1;
            }
        }
        else if (arg == "--tolerance") {
            if (argIndex + 1 < argc) {
                try {
                    config.geometryTolerance = std::stod(argv[++argIndex]);
                    config.geometryToleranceSet = true;
                    GltfInstancing::logDebug("Command-line override: Using geometry tolerance: " + std::to_string(config.geometryTolerance));
                }
                catch (const std::exception& e) {
                    GltfInstancing::logError("Invalid value for --tolerance (CLI): " + std::string(argv[argIndex]) + ". Error: " + e.what()); printUsage(argv[0]); return 1;
                }
            }
            else {
                GltfInstancing::logError("--tolerance option (CLI) requires a value."); printUsage(argv[0]); return 1;
            }
        }
        else if (arg == "--skip-attribute-data-hash") {
            if (argIndex + 1 < argc) {
                config.attributesToSkipDataHash = splitAndTrim(argv[++argIndex], ',');
                config.attributesToSkipDataHashSet = true;
                if (!config.attributesToSkipDataHash.empty()) {
                    std::string attrsLogged = "Command-line override: Tolerance mode will skip data hashing for attributes: ";
                    for (const auto& attr : config.attributesToSkipDataHash) attrsLogged += attr + " ";
                    GltfInstancing::logDebug(attrsLogged);
                }
            }
            else {
                GltfInstancing::logError("--skip-attribute-data-hash option (CLI) requires a comma-separated list."); printUsage(argv[0]); return 1;
            }
        }
        else if (arg == "--normal-tolerance") {
            if (argIndex + 1 < argc) {
                try {
                    config.normalTolerance = std::stod(argv[++argIndex]);
                    if (config.normalTolerance < 0.0) {
                        GltfInstancing::logWarning("WARNING (CLI): Normal tolerance cannot be negative. Using 0.0.");
                        config.normalTolerance = 0.0;
                    }
                    config.normalToleranceSet = true;
                    GltfInstancing::logDebug("Command-line override: Using normal tolerance: " + std::to_string(config.normalTolerance));
                }
                catch (const std::exception& e) {
                    GltfInstancing::logError("Invalid value for --normal-tolerance (CLI): " + std::string(argv[argIndex]) + ". Error: " + e.what()); printUsage(argv[0]); return 1;
                }
            }
            else {
                GltfInstancing::logError("--normal-tolerance option (CLI) requires a value."); printUsage(argv[0]); return 1;
            }
        }
        else if (arg == "--merge-all-glb") {
            config.mergeAllGlb = true;
            config.mergeAllGlbSet = true;
            GltfInstancing::logDebug("Command-line override: Merge all GLB outputs enabled.");
        }
        else if (arg == "--instance-limit") {
            if (argIndex + 1 < argc) {
                try {
                    config.instanceLimit = std::stoi(argv[++argIndex]);
                    if (config.instanceLimit < 1) {
                        GltfInstancing::logWarning("WARNING (CLI): Instance limit must be >= 1. Using default 2.");
                        config.instanceLimit = 2;
                    }
                    config.instanceLimitSet = true;
                    GltfInstancing::logDebug("Command-line override: Using instance limit: " + std::to_string(config.instanceLimit));
                }
                catch (const std::exception& e) {
                    GltfInstancing::logError("Invalid value for --instance-limit (CLI): " + std::string(argv[argIndex]) + ". Error: " + e.what()); printUsage(argv[0]); return 1;
                }
            }
            else {
                GltfInstancing::logError("--instance-limit option (CLI) requires a value."); printUsage(argv[0]); return 1;
            }
        }
        else if (arg == "--mesh-segmentation") {
            config.meshSegmentation = true;
            config.meshSegmentationSet = true;
            GltfInstancing::logDebug("Command-line override: Mesh segmentation enabled (each mesh to a separate GLB).");
        }
        else if (arg == "--csv-dir") {
            if (argIndex + 1 < argc) {
                config.csvDirectory = argv[++argIndex];
                config.csvDirectorySet = true;
                GltfInstancing::logDebug("Command-line override: CSV processing directory set to: " + config.csvDirectory);
            }
            else {
                GltfInstancing::logError("--csv-dir option (CLI) requires a path."); printUsage(argv[0]); return 1;
            }
        }
        else if (arg == "--enable-quadtree") {
            config.enableQuadtree = true;
            config.enableQuadtreeSet = true;
        }
        else if (arg == "--quadtree-max-depth") {
            if (argIndex + 1 < argc) {
                try {
                    config.quadtreeMaxDepth = std::stoi(argv[++argIndex]);
                }
                catch (...) {
                    GltfInstancing::logWarning("Invalid value for --quadtree-max-depth. Using default.");
                }
            }
        }
        else if (arg == "--quadtree-max-objs") {
            if (argIndex + 1 < argc) {
                try {
                    config.quadtreeMaxObjectsPerTile = std::stoi(argv[++argIndex]);
                }
                catch (...) {
                    GltfInstancing::logWarning("Invalid value for --quadtree-max-objs. Using default.");
                }
            }
        }
        else if (arg == "--enable-experiment-mode") {
            config.enableExperimentMode = true;
            GltfInstancing::logDebug("Command-line override: Experiment mode enabled.");
        }
        else if (arg == "--use-symbolic-links") {
            config.useSymbolicLinks = true;
            GltfInstancing::logDebug("Command-line override: Using symbolic links for experiment results.");
        }
        else if (arg == "--run-cross-glb-hlod-experiment") {
            config.runCrossGlbHLODExperiment = true;
            GltfInstancing::logDebug("Command-line override: Will run Cross-GLB HLOD experiment.");
        }
        else if (arg == "--experiment-dataset-name") {
            if (argIndex + 1 < argc) {
                config.experimentDatasetName = argv[++argIndex];
                GltfInstancing::logDebug("Command-line override: Experiment dataset name: " + config.experimentDatasetName);
            }
            else {
                GltfInstancing::logError("--experiment-dataset-name option (CLI) requires a value."); printUsage(argv[0]); return 1;
            }
        }
        else if (arg == "--experiment-strategy-id") {
            if (argIndex + 1 < argc) {
                config.experimentStrategyId = argv[++argIndex];
                GltfInstancing::logDebug("Command-line override: Experiment strategy ID: " + config.experimentStrategyId);
            }
            else {
                GltfInstancing::logError("--experiment-strategy-id option (CLI) requires a value."); printUsage(argv[0]); return 1;
            }
        }
        else if (arg == "--hlod-tolerance" || arg == "--hlod-geometry-tolerance") {
            if (argIndex + 1 < argc) {
                try {
                    config.hlodGeometryTolerance = std::stod(argv[++argIndex]);
                    config.hlodGeometryToleranceSet = true;
                    GltfInstancing::logDebug("Command-line override: Using HLOD geometry tolerance: " + std::to_string(config.hlodGeometryTolerance));
                }
                catch (const std::exception& e) {
                    GltfInstancing::logError("Invalid value for --hlod-tolerance (CLI): " + std::string(argv[argIndex]) + ". Error: " + e.what()); printUsage(argv[0]); return 1;
                }
            }
            else {
                GltfInstancing::logError("--hlod-tolerance option (CLI) requires a value."); printUsage(argv[0]); return 1;
            }
        }
        else if (arg == "--hlod-normal-tolerance") {
            if (argIndex + 1 < argc) {
                try {
                    config.hlodNormalTolerance = std::stod(argv[++argIndex]);
                    if (config.hlodNormalTolerance < 0.0) {
                        GltfInstancing::logWarning("WARNING (CLI): HLOD normal tolerance cannot be negative. Using 0.0.");
                        config.hlodNormalTolerance = 0.0;
                    }
                    config.hlodNormalToleranceSet = true;
                    GltfInstancing::logDebug("Command-line override: Using HLOD normal tolerance: " + std::to_string(config.hlodNormalTolerance));
                }
                catch (const std::exception& e) {
                    GltfInstancing::logError("Invalid value for --hlod-normal-tolerance (CLI): " + std::string(argv[argIndex]) + ". Error: " + e.what()); printUsage(argv[0]); return 1;
                }
            }
            else {
                GltfInstancing::logError("--hlod-normal-tolerance option (CLI) requires a value."); printUsage(argv[0]); return 1;
            }
        }
        else if (arg == "--hlod-skip-attribute-data-hash") {
            if (argIndex + 1 < argc) {
                config.hlodAttributesToSkipDataHash = splitAndTrim(argv[++argIndex], ',');
                config.hlodAttributesToSkipDataHashSet = true;
                if (!config.hlodAttributesToSkipDataHash.empty()) {
                    std::string attrsLogged = "Command-line override: HLOD tolerance mode will skip data hashing for attributes: ";
                    for (const auto& attr : config.hlodAttributesToSkipDataHash) attrsLogged += attr + " ";
                    GltfInstancing::logDebug(attrsLogged);
                }
            }
            else {
                GltfInstancing::logError("--hlod-skip-attribute-data-hash option (CLI) requires a comma-separated list."); printUsage(argv[0]); return 1;
            }
        }
        else if (arg == "--hlod-instance-limit") {
            if (argIndex + 1 < argc) {
                try {
                    config.hlodInstanceLimit = std::stoi(argv[++argIndex]);
                    if (config.hlodInstanceLimit < 1) {
                        GltfInstancing::logWarning("WARNING (CLI): HLOD instance limit must be >= 1. Using default 2.");
                        config.hlodInstanceLimit = 2;
                    }
                    config.hlodInstanceLimitSet = true;
                    GltfInstancing::logDebug("Command-line override: Using HLOD instance limit: " + std::to_string(config.hlodInstanceLimit));
                }
                catch (const std::exception& e) {
                    GltfInstancing::logError("Invalid value for --hlod-instance-limit (CLI): " + std::string(argv[argIndex]) + ". Error: " + e.what()); printUsage(argv[0]); return 1;
                }
            }
            else {
                GltfInstancing::logError("--hlod-instance-limit option (CLI) requires a value."); printUsage(argv[0]); return 1;
            }
        }
        else if (arg == "--hlod-allow-non-uniform-scale-instancing") {
            config.hlodAllowNonUniformScaleInstancing = true;
            GltfInstancing::logDebug("Command-line override: HLOD allow non-uniform scale instancing enabled.");
        }
        else { // An unknown option
            GltfInstancing::logError("Unexpected command-line argument: " + arg);
            printUsage(argv[0]);
            return 1;
        }
        argIndex++;
    }

    // 3. Finalize and validate configuration
    if (config.inputDirectory.empty()) {
        GltfInstancing::logError("--input_directory must be specified.");
        printUsage(argv[0]);
        return 1;
    }

    if (config.outputDirectory.empty()) {
        std::filesystem::path inputPath(config.inputDirectory);
        config.outputDirectory = (inputPath / "processed_output").string();
        GltfInstancing::logInfo("Output directory not specified, defaulting to: " + config.outputDirectory);
    }

    // --- Quadtree Pipeline Execution ---
    // HLOD is now executed AFTER standard processing, using the results of Stage 1/2.
    // NOTE: It requires separated GLB files. 
    // If meshSegmentation was NOT enabled by user, we perform a temporary segmentation here for Quadtree.
    if (config.enableQuadtree) {
        GltfInstancing::logInfo("Quadtree Pipeline Enabled (Post-Processing Stage).");

        std::string quadtreeInputPath = "";
        bool tempInput = false;

        // 1. Determine Input Source
        if (config.meshSegmentation) {
            // User already generated segmented files, use them directly
            quadtreeInputPath = OutputPaths::segmentedDir(config).string();
            GltfInstancing::logInfo("Using existing segmented output for Quadtree input: " + quadtreeInputPath);
        }
        else {
            // We need to generate separated files from the non-instanced result of Stage 1
            GltfInstancing::logInfo("Mesh segmentation was not enabled. Generating temporary separated GLBs for Quadtree input...");

            // Locate Non-Instanced Output from Stage 1
            std::filesystem::path nonInstancedGlbPath = OutputPaths::nonInstancedGlb(config);

            if (std::filesystem::exists(nonInstancedGlbPath)) {
                std::filesystem::path tempOutputDir = OutputPaths::quadtreeTempInput(config);
                std::filesystem::create_directories(tempOutputDir);

                GltfInstancing::GlbReader splitReader;
                std::set<std::filesystem::path> fileSet = { nonInstancedGlbPath };
                auto modelsToSplit = splitReader.loadGltfModels(fileSet);

                GltfInstancing::GlbWriter splitWriter;
                if (!modelsToSplit.empty()) {
                    if (splitWriter.writeMeshesAsSeparateGlbs(modelsToSplit, tempOutputDir)) {
                        quadtreeInputPath = tempOutputDir.string();
                        tempInput = true;
                        GltfInstancing::logInfo("Generated temporary Quadtree input at: " + quadtreeInputPath);
                    }
                    else {
                        GltfInstancing::logError("Failed to generate temporary separated GLBs.");
                    }
                }
                else {
                    GltfInstancing::logError("Failed to load non-instanced GLB for splitting: " + nonInstancedGlbPath.string());
                }
            }
            else {
                GltfInstancing::logError("Non-instanced GLB not found (" + nonInstancedGlbPath.string() + "). Cannot run Quadtree Pipeline.");
            }
        }

        // 2. Run Pipeline
        if (!quadtreeInputPath.empty()) {
            // Create a temporary config that points to the new input directory
            ToolConfiguration quadConfig = config;
            quadConfig.inputDirectory = quadtreeInputPath;
            // Output to a subfolder to avoid overwriting standard output
            quadConfig.outputDirectory = OutputPaths::hlodDir(config).string();

            GltfInstancing::logInfo("Starting Quadtree Pipeline...");
            QuadtreePipeline::Pipeline pipeline(quadConfig);
            pipeline.run();
            GltfInstancing::logInfo("Quadtree Pipeline Finished. Output at: " + quadConfig.outputDirectory);

            // 实验模式：生成HLOD参数实验目录 (03_HLODParams)
            if (config.enableExperimentMode) {
                std::string datasetName = config.experimentDatasetName.empty() ? "default_dataset" : config.experimentDatasetName;
                std::string strategyId = config.experimentStrategyId.empty() ?
                    "Depth" + std::to_string(config.quadtreeMaxDepth) + "_Obj" + std::to_string(config.quadtreeMaxObjectsPerTile) : config.experimentStrategyId;

                std::filesystem::path experimentsBaseDir = std::filesystem::path(config.outputDirectory) / "experiments";
                ExperimentFramework::ExperimentDirectoryManager expManager(experimentsBaseDir);

                ExperimentFramework::StrategyInfo strategy;
                strategy.id = strategyId;
                strategy.name = "Quadtree HLOD";
                strategy.description = "Hierarchical LOD with quadtree spatial partitioning";
                strategy.parameters["max_depth"] = std::to_string(config.quadtreeMaxDepth);
                strategy.parameters["max_objects_per_tile"] = std::to_string(config.quadtreeMaxObjectsPerTile);
                strategy.parameters["enable_lod"] = std::to_string(config.enableInstanceLodGeneration);

                GltfInstancing::logInfo("Creating experiment structure for HLOD_PARAMS, dataset: " + datasetName + ", strategy: " + strategyId);
                auto expDir = expManager.createExperimentStructure(
                    ExperimentFramework::ExperimentType::HLOD_PARAMS,
                    datasetName, strategy);
                GltfInstancing::logInfo("HLOD experiment directory created at: " + expDir.string());

                // 复制hlod_analysis.csv到实验目录（如果存在）
                std::filesystem::path hlodAnalysisPath = OutputPaths::hlodAnalysisCsv(config);
                if (std::filesystem::exists(hlodAnalysisPath)) {
                    std::filesystem::path expCsvPath = expDir / "hlod_analysis.csv";
                    std::filesystem::copy_file(hlodAnalysisPath, expCsvPath, std::filesystem::copy_options::overwrite_existing);
                    GltfInstancing::logInfo("HLOD analysis CSV copied to: " + expCsvPath.string());
                }

                // 写入配置
                std::filesystem::path configPath = expDir / "config.json";
                ExperimentFramework::ConfigGenerator::writeConfigJson(configPath, config, strategy);

                // 生成README
                std::map<std::string, ExperimentFramework::MetricValue> hlodMetrics;
                hlodMetrics["Max Depth"] = {"Max Depth", static_cast<double>(config.quadtreeMaxDepth), "level", "Quadtree max depth"};
                hlodMetrics["Max Objects/Tile"] = {"Max Objects/Tile", static_cast<double>(config.quadtreeMaxObjectsPerTile), "count", "Max objects per tile"};
                std::filesystem::path readmePath = expDir / "README.md";
                ExperimentFramework::ReadmeGenerator::writeStrategyReadme(readmePath, strategy, hlodMetrics);
                GltfInstancing::logInfo("HLOD experiment README written to: " + readmePath.string());
            }

        }
    }

    // 5. Validate crucial final configuration & create output directory
    if (!std::filesystem::is_directory(config.inputDirectory)) {
        GltfInstancing::logError("Final input directory does not exist or is not a directory: " + config.inputDirectory);
        return 1;
    }
    if (!std::filesystem::exists(config.outputDirectory)) {
        try {
            if (std::filesystem::create_directories(config.outputDirectory)) {
                GltfInstancing::logInfo("Created output directory: " + config.outputDirectory);
            }
            else if (!std::filesystem::is_directory(config.outputDirectory)) {
                GltfInstancing::logError("Failed to create output directory (or it's not a directory): " + config.outputDirectory);
                return 1;
            }
        }
        catch (const std::filesystem::filesystem_error& e) {
            GltfInstancing::logError("Failed to create output directory: " + config.outputDirectory + ". Error: " + e.what());
            return 1;
        }
    }
    else if (!std::filesystem::is_directory(config.outputDirectory)) {
        GltfInstancing::logError("Output path exists but is not a directory: " + config.outputDirectory);
        return 1;
    }

    // Using ExperimentFramework for standardized directory structure.
    // Experiment directories will be created on-demand during result generation.

    // Staged 模式：创建分层目录结构
    OutputPaths::ensureOutputDirectories(config);

    // 写入 run_manifest.json（运行元数据）
    {
        std::filesystem::path manifestPath = std::filesystem::path(config.outputDirectory) / "run_manifest.json";
        std::ofstream mf(manifestPath);
        if (mf.is_open()) {
            auto now = std::chrono::system_clock::now();
            auto timeT = std::chrono::system_clock::to_time_t(now);
            char timeBuf[64];
            std::strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&timeT));
            mf << "{\n  \"timestamp\": \"" << timeBuf << "\",\n";
            mf << "  \"input_directory\": \"" << config.inputDirectory << "\",\n";
            mf << "  \"stages_enabled\": [\"instancing\"";
            if (config.enableInstanceLodGeneration) mf << ", \"instance_lod\"";
            if (config.enableNonInstancedLodGeneration) mf << ", \"non_instance_lod\"";
            if (config.enableQuadtree) mf << ", \"hlod\"";
            if (config.meshSegmentation) mf << ", \"segmented\"";
            mf << "]\n}\n";
            mf.close();
            GltfInstancing::logInfo("Run manifest written to: " + manifestPath.string());
        }
    }

    GltfInstancing::logInfo("Stage 1: Discovering, Reading, and Processing GLB files for Instancing...");
    GltfInstancing::GlbReader reader;
    std::set<std::filesystem::path> initialGlbFilePaths = reader.discoverGlbFiles(config.inputDirectory, true /* recursive */);

    if (initialGlbFilePaths.empty()) {
        GltfInstancing::logInfo("No GLB files found in input directory to process.");
        return 0;
    }

    std::vector<GltfInstancing::LoadedGltfModel> loadedModels = reader.loadGltfModels(initialGlbFilePaths);
    if (loadedModels.empty()) {
        GltfInstancing::logError("Failed to load any GLB models from input directory.");
        return 1;
    }
    GltfInstancing::logInfo("Successfully loaded " + std::to_string(loadedModels.size()) + " initial GLB model(s).");

    GltfInstancing::logInfo("Stage 1: Detecting instancing opportunities...");
    // Stage 1 uses Stage 1 parameters (config.geometryTolerance, etc.)
    GltfInstancing::InstancingDetector detector(config.geometryTolerance, config.attributesToSkipDataHash, config.normalTolerance, config.instanceLimit, config.allowNonUniformScaleInstancing);
    GltfInstancing::InstancingDetectionResult detectionResult = detector.detect(loadedModels);
    GltfInstancing::logInfo("Stage 1: Instancing detection finished. Generating optimization analysis outputs...");

    GltfInstancing::GlbWriter glbWriter;
    GltfInstancing::TilesetWriter tilesetWriter;

    // --- REPORT GENERATION (Restored - Text) ---
    std::string reportContent = detectionResult.getReport();
    std::filesystem::path reportPath = OutputPaths::instancingAnalysisTxt(config);
    std::ofstream reportFile(reportPath);
    if (reportFile.is_open()) {
        reportFile << reportContent;
        reportFile.close();
        // GltfInstancing::logInfo("Instancing analysis report written to: " + reportPath.string());
    }
    else {
        GltfInstancing::logError("Failed to write instancing analysis report to: " + reportPath.string());
    }

    // --- STANDARD OUTPUT GENERATION (Always run) ---
    // (Generate CSV Report first)
    writeAnalysisCsv(config, loadedModels, detectionResult);

    // 实验模式：生成非均匀缩放实验目录 (05_NonUniformScale) - 当启用时
    if (config.enableExperimentMode && config.allowNonUniformScaleInstancing) {
        std::string datasetName = config.experimentDatasetName.empty() ? "default_dataset" : config.experimentDatasetName;
        std::string strategyId = config.experimentStrategyId.empty() ? "NonUniform_Allowed" : config.experimentStrategyId;

        std::filesystem::path experimentsBaseDir = std::filesystem::path(config.outputDirectory) / "experiments";
        ExperimentFramework::ExperimentDirectoryManager expManager(experimentsBaseDir);

        ExperimentFramework::StrategyInfo strategy;
        strategy.id = strategyId;
        strategy.name = "Non-Uniform Scale Instancing";
        strategy.description = "Allow non-uniform scale transformations for instancing detection";
        strategy.parameters["allow_non_uniform_scale"] = "true";
        strategy.parameters["geometry_tolerance"] = std::to_string(config.geometryTolerance);
        strategy.parameters["instance_limit"] = std::to_string(config.instanceLimit);

        GltfInstancing::logInfo("Creating experiment structure for NON_UNIFORM_SCALE, dataset: " + datasetName + ", strategy: " + strategyId);
        auto expDir = expManager.createExperimentStructure(
            ExperimentFramework::ExperimentType::NON_UNIFORM_SCALE,
            datasetName, strategy);
        GltfInstancing::logInfo("Non-uniform scale experiment directory created at: " + expDir.string());

        // 写入配置
        std::filesystem::path configPath = expDir / "config.json";
        ExperimentFramework::ConfigGenerator::writeConfigJson(configPath, config, strategy);

        // 生成README
        std::map<std::string, ExperimentFramework::MetricValue> metrics;
        metrics["Non-Uniform Scale Enabled"] = {"Non-Uniform Scale", 1.0, "boolean", "Allow non-uniform scale instancing"};
        metrics["Geometry Tolerance"] = {"Tolerance", config.geometryTolerance, "m", "Geometry matching tolerance"};
        std::filesystem::path readmePath = expDir / "README.md";
        ExperimentFramework::ReadmeGenerator::writeStrategyReadme(readmePath, strategy, metrics);
        GltfInstancing::logInfo("Non-uniform scale experiment README written to: " + readmePath.string());
    }

    const std::string instancedGlbBase = "instanced";
    const std::string nonInstancedGlbBase = "non_instanced";
    std::vector<std::filesystem::path> stage1_outputGlbs;

    std::optional<std::pair<std::filesystem::path, GltfInstancing::BoundingBox>> instancedWriteResult;
    std::optional<std::pair<std::filesystem::path, GltfInstancing::BoundingBox>> nonInstancedWriteResult;

    // Declare tileset paths here for later use in experiment mode
    std::filesystem::path instancedTilesetPath;
    std::filesystem::path nonInstancedTilesetPath;

    if (config.mergeAllGlb) {
        std::filesystem::path mergedInstancedGlbPath = OutputPaths::instancingDir(config) / (instancedGlbBase + ".glb");
        instancedWriteResult = glbWriter.writeInstancedMeshesOnly(loadedModels, detectionResult, mergedInstancedGlbPath);
        if (instancedWriteResult) stage1_outputGlbs.push_back(instancedWriteResult->first);

        std::filesystem::path mergedNonInstancedGlbPath = OutputPaths::instancingDir(config) / (nonInstancedGlbBase + ".glb");
        nonInstancedWriteResult = glbWriter.writeNonInstancedMeshesOnly(loadedModels, detectionResult, mergedNonInstancedGlbPath);
        if (nonInstancedWriteResult) stage1_outputGlbs.push_back(nonInstancedWriteResult->first);
    }
    else {
        std::filesystem::path instancedGlbPath = OutputPaths::instancingDir(config) / (instancedGlbBase + ".glb");
        instancedWriteResult = glbWriter.writeInstancedMeshesOnly(loadedModels, detectionResult, instancedGlbPath);
        if (instancedWriteResult) stage1_outputGlbs.push_back(instancedWriteResult->first);

        std::filesystem::path nonInstancedGlbPath = OutputPaths::instancingDir(config) / (nonInstancedGlbBase + ".glb");
        nonInstancedWriteResult = glbWriter.writeNonInstancedMeshesOnly(loadedModels, detectionResult, nonInstancedGlbPath);
        if (nonInstancedWriteResult) stage1_outputGlbs.push_back(nonInstancedWriteResult->first);
    }

    if (instancedWriteResult && instancedWriteResult->second.isValid()) {
        instancedTilesetPath = OutputPaths::instancedTileset(config);
        std::vector<std::filesystem::path> instancedUris = { instancedWriteResult->first };
        GltfInstancing::BoundingBox bbox = instancedWriteResult->second;
        glm::dvec3 extents = bbox.max - bbox.min;
        double diagonal = glm::length(extents);
        double rootGeometricError = (diagonal > 0) ? (diagonal * 0.1) : 1.0;
        if (rootGeometricError < 1.0) rootGeometricError = 1.0;
        tilesetWriter.writeTileset(instancedUris, instancedTilesetPath, rootGeometricError);
    }

    if (nonInstancedWriteResult && nonInstancedWriteResult->second.isValid()) {
        nonInstancedTilesetPath = OutputPaths::nonInstancedTileset(config);
        std::vector<std::filesystem::path> nonInstancedUris = { nonInstancedWriteResult->first };
        GltfInstancing::BoundingBox bbox = nonInstancedWriteResult->second;
        glm::dvec3 extents = bbox.max - bbox.min;
        double diagonal = glm::length(extents);
        double rootGeometricError = (diagonal > 0) ? (diagonal * 0.1) : 1.0;
        if (rootGeometricError < 1.0) rootGeometricError = 1.0;
        tilesetWriter.writeTileset(nonInstancedUris, nonInstancedTilesetPath, rootGeometricError);
    }

    // 更新 instancing_analysis.csv 中的 SO, SC, CR（GLB 已写入，可计算实际文件大小）
    double outputFileSizeMB = 0.0;
    try {
        std::filesystem::path instPath = OutputPaths::instancedGlb(config);
        std::filesystem::path nonInstPath = OutputPaths::nonInstancedGlb(config);
        if (std::filesystem::exists(instPath)) outputFileSizeMB += static_cast<double>(std::filesystem::file_size(instPath)) / (1024.0 * 1024.0);
        if (std::filesystem::exists(nonInstPath)) outputFileSizeMB += static_cast<double>(std::filesystem::file_size(nonInstPath)) / (1024.0 * 1024.0);
    } catch (...) {}
    updateInstancingAnalysisCsvWithFileSizes(config, loadedModels, detectionResult, outputFileSizeMB);

    // 实验模式：将 INSTANCING_STRATEGY 输出文件复制到实验目录（GLB 等需在写入后复制）
    if (config.enableExperimentMode) {
        std::string datasetName = config.experimentDatasetName.empty() ? "default_dataset" : config.experimentDatasetName;
        std::string strategyId = config.experimentStrategyId.empty() ? "default" : config.experimentStrategyId;

        std::filesystem::path experimentsBaseDir = std::filesystem::path(config.outputDirectory) / "experiments";
        ExperimentFramework::ExperimentDirectoryManager expManager(experimentsBaseDir);

        ExperimentFramework::StrategyInfo strategy;
        strategy.id = strategyId;
        strategy.name = strategyId;
        strategy.description = "Instancing detection with tolerance " + std::to_string(config.geometryTolerance);

        auto expDir = expManager.createExperimentStructure(
            ExperimentFramework::ExperimentType::INSTANCING_STRATEGY,
            datasetName, strategy);

        std::vector<std::pair<std::string, std::filesystem::path>> filesToCopy = {
            {"instanced_meshes.glb", OutputPaths::instancedGlb(config)},
            {"non_instanced_meshes.glb", OutputPaths::nonInstancedGlb(config)},
            {"instancing_per_glb.csv", OutputPaths::instancingPerGlbCsv(config)},
            {"instancing_optimization_summary.txt", OutputPaths::instancingOptimizationSummary(config)},
            {"instancing_analysis.txt", OutputPaths::instancingAnalysisTxt(config)}
        };

        for (const auto& [filename, sourcePath] : filesToCopy) {
            if (std::filesystem::exists(sourcePath)) {
                std::filesystem::path destPath = expDir / filename;
                std::filesystem::copy_file(sourcePath, destPath, std::filesystem::copy_options::overwrite_existing);
                GltfInstancing::logInfo("Copied " + filename + " to INSTANCING_STRATEGY experiment directory");
            }
        }
    }

    // --- Non-Instanced LOD Generation with Post-LOD Instancing ---
    if (config.enableNonInstancedLodGeneration && nonInstancedWriteResult) {
        GltfInstancing::logInfo("Generating LODs for non-instanced meshes with post-process instancing...");
        std::filesystem::path lodOutputDir = OutputPaths::nonInstanceLodDir(config);

        // 1. Generate Raw LOD Files (Geometry Simplification only)
        auto lodLevels = NonInstancingLOD::NonInstancingLODManager::generateLODFilesOnly(
            nonInstancedWriteResult->first,
            lodOutputDir,
            config.nonInstancedLodLevelCount,
            (float)config.nonInstancedLodRatio,
            config.nonInstancedMinSimplifyIndexCount
        );

        if (!lodLevels.empty()) {
            // -------------------------------------------------------
            // 1. Always generate Standard Tileset (Base functionality)
            // -------------------------------------------------------
            GltfInstancing::logInfo("Organizing generated LOD files into standard tileset...");

            GltfInstancing::TilesetNode standardRoot;
            GltfInstancing::TilesetNode* stdCurrent = &standardRoot;

            // Reverse iterate (Coarsest -> Finest)
            for (int i = lodLevels.size() - 1; i >= 0; --i) {
                GltfInstancing::TilesetNode node;
                node.contentUri = lodLevels[i].filePath.filename().string();

                double error = lodLevels[i].geometricError;
                if (i == lodLevels.size() - 1) error = 1000.0; // Root error

                node.geometricError = error;
                node.boundingVolume = { glm::dvec3(-10000), glm::dvec3(10000) };

                if (i == lodLevels.size() - 1) {
                    standardRoot = node;
                    stdCurrent = &standardRoot;
                }
                else {
                    stdCurrent->children.push_back(node);
                    stdCurrent = &stdCurrent->children.back();
                }
            }

            std::filesystem::path standardTilesetPath = lodOutputDir / "tileset.json";
            tilesetWriter.writeHierarchicalTileset(standardRoot, standardTilesetPath);
            GltfInstancing::logInfo("Standard Non-Instanced LOD tileset generated at: " + standardTilesetPath.string());

            // -------------------------------------------------------
            // 2. Optional: Post-process Instancing (Advanced feature)
            // -------------------------------------------------------
            if (config.enableNonInstancedLodInstancing) {
                GltfInstancing::logInfo("Applying instancing detection to generated LOD levels (Outputting to subfolder)...");

                // Create sub-directory for instanced results
                std::filesystem::path instancedOutputDir = lodOutputDir / "instanced_lods";
                std::filesystem::create_directories(instancedOutputDir);

                std::vector<GltfInstancing::TilesetNode> finalNodes;
                GltfInstancing::GlbReader lodReader;

                // Use HLOD-specific instancing detection parameters
                auto hlodParams = getHlodInstancingParams(config);
                GltfInstancing::InstancingDetector lodDetector(
                    hlodParams.geometryTolerance,
                    hlodParams.attributesToSkipDataHash,
                    hlodParams.normalTolerance,
                    hlodParams.instanceLimit,
                    hlodParams.allowNonUniformScaleInstancing
                );
                GltfInstancing::logInfo("Using HLOD instancing detection parameters: tolerance=" +
                    std::to_string(hlodParams.geometryTolerance) + ", instance_limit=" +
                    std::to_string(hlodParams.instanceLimit));

                for (const auto& levelInfo : lodLevels) {
                    GltfInstancing::logInfo("Processing Level " + std::to_string(levelInfo.level) + " for instancing...");

                    std::set<std::filesystem::path> fileSet = { levelInfo.filePath };
                    auto lodModels = lodReader.loadGltfModels(fileSet);
                    if (lodModels.empty()) continue;

                    auto lodDetectionResult = lodDetector.detect(lodModels);

                    std::string baseName = levelInfo.filePath.stem().string();
                    // Output to subfolder
                    std::filesystem::path instancedPath = instancedOutputDir / (baseName + "_instanced.glb");
                    std::filesystem::path uniquePath = instancedOutputDir / (baseName + "_unique.glb");

                    auto writeInst = glbWriter.writeInstancedMeshesOnly(lodModels, lodDetectionResult, instancedPath);
                    auto writeUniq = glbWriter.writeNonInstancedMeshesOnly(lodModels, lodDetectionResult, uniquePath);

                    std::vector<std::filesystem::path> levelContents;
                    GltfInstancing::BoundingBox combinedBBox;

                    if (writeInst && writeInst->second.isValid()) {
                        levelContents.push_back(instancedPath);
                        combinedBBox.merge(writeInst->second);
                    }
                    if (writeUniq && writeUniq->second.isValid()) {
                        levelContents.push_back(uniquePath);
                        combinedBBox.merge(writeUniq->second);
                    }

                    std::string finalUri;

                    if (levelContents.empty()) {
                        GltfInstancing::logWarning("Level " + std::to_string(levelInfo.level) + " produced no content during instancing.");
                        continue;
                    }
                    else if (levelContents.size() == 1) {
                        finalUri = levelContents[0].filename().string();
                    }
                    else {
                        std::string wrapperName = baseName + "_wrapper.json";
                        std::filesystem::path wrapperPath = instancedOutputDir / wrapperName;

                        if (tilesetWriter.writeWrapperTileset(levelContents, wrapperPath, levelInfo.geometricError)) {
                            finalUri = wrapperName;
                        }
                        else {
                            GltfInstancing::logError("Failed to write wrapper tileset for " + baseName);
                            finalUri = levelContents[0].filename().string();
                        }
                    }

                    GltfInstancing::TilesetNode node;
                    node.contentUri = finalUri;
                    node.geometricError = levelInfo.geometricError;
                    node.boundingVolume = combinedBBox;
                    finalNodes.push_back(node);
                }

                if (!finalNodes.empty()) {
                    GltfInstancing::TilesetNode rootNode;
                    GltfInstancing::TilesetNode* current = &rootNode;

                    for (int i = finalNodes.size() - 1; i >= 0; --i) {
                        auto& srcNode = finalNodes[i];
                        if (i == finalNodes.size() - 1 && srcNode.geometricError < 100.0) srcNode.geometricError = 1000.0;

                        if (i == finalNodes.size() - 1) {
                            rootNode = srcNode;
                            current = &rootNode;
                        }
                        else {
                            current->children.push_back(srcNode);
                            current = &current->children.back();
                        }
                    }

                    std::filesystem::path finalTilesetPath = instancedOutputDir / "tileset.json";
                    tilesetWriter.writeHierarchicalTileset(rootNode, finalTilesetPath);
                    GltfInstancing::logInfo("Advanced Instanced-LOD tileset generated at: " + finalTilesetPath.string());
                }
            }
        }
    }

    // Stage 2: Mesh Segmentation
    if (config.meshSegmentation) {
        std::filesystem::path segmentationOutputDir = OutputPaths::segmentedDir(config);
        std::filesystem::create_directories(segmentationOutputDir);
        GltfInstancing::GlbReader stage2Reader;
        std::vector<GltfInstancing::LoadedGltfModel> modelsToSegment;
        for (const auto& glbPath : stage1_outputGlbs) {
            if (std::filesystem::exists(glbPath)) {
                std::set<std::filesystem::path> singleFileSet = { glbPath };
                auto loadedSingleModelVec = stage2Reader.loadGltfModels(singleFileSet);
                modelsToSegment.insert(modelsToSegment.end(), loadedSingleModelVec.begin(), loadedSingleModelVec.end());
            }
        }
        if (!modelsToSegment.empty()) {
            glbWriter.writeMeshesAsSeparateGlbs(modelsToSegment, segmentationOutputDir);
        }
    }

    // --- LOD Generation Logic ---
    if (config.enableInstanceLodGeneration) {
        GltfInstancing::logInfo("LOD Generation Enabled. Loading semantic data...");

        GltfInstancing::SemanticParser semanticParser;
        if (!config.semanticDataPath.empty() && std::filesystem::exists(config.semanticDataPath)) {
            semanticParser.parse(config.semanticDataPath);
        }
        else {
            GltfInstancing::logWarning("Semantic data path invalid or not set. LOD generation will proceed without semantic hints (mostly geometry-based).");
        }

        GltfInstancing::LODConfig lodConfig;
        lodConfig.enableLOD = true;
        lodConfig.maxLODLevels = config.lodLevelCount;
        lodConfig.targetScreenSSE = config.targetScreenSSE;
        lodConfig.enableSemanticCheck = config.enableSemanticCheck;
        lodConfig.enableGeometricCheck = config.enableGeometricCheck;
        lodConfig.lod4_sizeTolerance = config.lod4SizeTolerance;
        lodConfig.lod3_aspectRatioTolerance = config.lod3AspectRatioTolerance;

        GltfInstancing::InstancingLODManager lodManager(lodConfig);
        auto lodResults = lodManager.generateLODs(detectionResult, loadedModels, semanticParser);

        std::filesystem::path lodOutputDir = OutputPaths::instanceLodDir(config);
        std::filesystem::create_directories(lodOutputDir);

        // Map to store LOD hierarchy nodes
        // Key: Level, Value: List of TilesetNodes (one for each GLB at this level, though usually one combined GLB per level)
        // Here we assume one GLB per LOD level for simplicity
        std::vector<GltfInstancing::TilesetNode> levelNodes(config.lodLevelCount + 1); // Index 1-5
        std::vector<LodStats> lodStatistics;

        for (auto const& [level, result] : lodResults) {
            std::string filename = "LOD" + std::to_string(level) + ".glb";
            std::filesystem::path outputPath = lodOutputDir / filename;

            auto writeRes = glbWriter.writeLODGlb(loadedModels, result, outputPath);
            if (writeRes && writeRes->second.isValid()) {
                GltfInstancing::TilesetNode node;
                node.contentUri = filename;
                node.boundingVolume = writeRes->second;
                node.geometricError = result.geometricError;
                levelNodes[level] = node;

                // Collect Stats
                LodStats stats;
                stats.level = level;
                try {
                    stats.fileSizeMB = (double)std::filesystem::file_size(outputPath) / (1024.0 * 1024.0);
                }
                catch (...) { stats.fileSizeMB = 0.0; }

                stats.uniqueMeshes = 0;
                stats.totalVertices = 0;
                stats.totalInstances = 0;

                // Calculate from result.nodes (ExtendedMeshInfo)
                // Note: result.nodes contains the *representatives*.
                stats.uniqueMeshes = result.nodes.size();
                for (const auto& meshInfo : result.nodes) {
                    stats.totalVertices += meshInfo.vertexCount;
                    stats.totalInstances += meshInfo.instances.size();
                }
                lodStatistics.push_back(stats);
            }
        }

        // Calculate Originals for Comparison
        double originalFileSizeMB = 0.0;
        size_t originalVertices = 0;
        size_t originalInstancesTotal = 0; // Total instances in original scene (flattened)
        for (const auto& p : initialGlbFilePaths) {
            try { originalFileSizeMB += (double)std::filesystem::file_size(p) / (1024.0 * 1024.0); }
            catch (...) {}
        }
        // Estimate original unique vertices (loadedModels)
        for (const auto& lm : loadedModels) {
            for (const auto& mesh : lm.model.meshes) {
                for (const auto& prim : mesh.primitives) {
                    auto it = prim.attributes.find("POSITION");
                    if (it != prim.attributes.end()) {
                        int accId = it->second;
                        if (accId >= 0 && accId < lm.model.accessors.size()) {
                            originalVertices += lm.model.accessors[accId].count;
                        }
                    }
                }
            }
            // Estimate original instances (roughly total nodes if no instancing, or sum of instance counts)
            // A better metric for "Original Instances" in this table context might be "Total Objects"
            // We can sum up instances from LOD5 result as the baseline "Total Objects" count.
            if (!lodResults.empty() && lodResults.count(5)) {
                for (const auto& m : lodResults.at(5).nodes) originalInstancesTotal += m.instances.size();
            }
        }

        writeLodAnalysisCsv(config, lodStatistics, originalFileSizeMB, originalVertices, originalInstancesTotal);

        // Build Tree (LOD1 -> LOD2 -> ... -> LOD5)
        // This is a simplified chain for now. 
        // In a real scenario with spatial splitting, it would be a tree.
        GltfInstancing::TilesetNode rootNode;
        GltfInstancing::TilesetNode* currentNode = &rootNode;

        bool firstNodeFound = false;

        // Iterate from LOD1 (Root) down to LOD5 (Leaf)
        for (int l = 1; l <= config.lodLevelCount; ++l) {
            if (!levelNodes[l].contentUri.empty()) { // If level exists
                if (!firstNodeFound) {
                    rootNode = levelNodes[l];
                    // Root node geometric error should be set based on scene size or default
                    // But here we take what InstancingLODManager calculated
                    currentNode = &rootNode;
                    firstNodeFound = true;
                }
                else {
                    currentNode->children.push_back(levelNodes[l]);
                    currentNode = &currentNode->children.back();
                }
            }
        }

        if (firstNodeFound) {
            std::filesystem::path tilesetPath = lodOutputDir / "tileset.json";
            tilesetWriter.writeHierarchicalTileset(rootNode, tilesetPath);
            GltfInstancing::logInfo("LOD processing complete. Tileset written to: " + tilesetPath.string());
        }

        // Stage 3: CSV Processing (Always run if configured)
        processCsvAgainstGlb(config);

        // --- Quadtree Pipeline Execution ---
        // HLOD is now executed AFTER standard processing, using the results of Stage 1/2.
        // NOTE: It requires separated GLB files. 
        // If meshSegmentation was NOT enabled by user, we perform a temporary segmentation here for Quadtree.
        if (config.enableQuadtree) {
            GltfInstancing::logInfo("Quadtree Pipeline Enabled (Post-Processing Stage).");

            std::string quadtreeInputPath = "";
            bool tempInput = false;

            // 1. Determine Input Source
            if (config.meshSegmentation) {
                // User already generated segmented files, use them directly
                quadtreeInputPath = OutputPaths::segmentedDir(config).string();
                GltfInstancing::logInfo("Using existing segmented output for Quadtree input: " + quadtreeInputPath);
            }
            else {
                // We need to generate separated files from the non-instanced result of Stage 1
                GltfInstancing::logInfo("Mesh segmentation was not enabled. Generating temporary separated GLBs for Quadtree input...");

                // Locate Non-Instanced Output from Stage 1
                std::filesystem::path nonInstancedGlbPath = OutputPaths::nonInstancedGlb(config);

                if (std::filesystem::exists(nonInstancedGlbPath)) {
                    std::filesystem::path tempOutputDir = OutputPaths::quadtreeTempInput(config);
                    std::filesystem::create_directories(tempOutputDir);

                    GltfInstancing::GlbReader splitReader;
                    std::set<std::filesystem::path> fileSet = { nonInstancedGlbPath };
                    auto modelsToSplit = splitReader.loadGltfModels(fileSet);

                    GltfInstancing::GlbWriter splitWriter;
                    if (!modelsToSplit.empty()) {
                        if (splitWriter.writeMeshesAsSeparateGlbs(modelsToSplit, tempOutputDir)) {
                            quadtreeInputPath = tempOutputDir.string();
                            tempInput = true;
                            GltfInstancing::logInfo("Generated temporary Quadtree input at: " + quadtreeInputPath);
                        }
                        else {
                            GltfInstancing::logError("Failed to generate temporary separated GLBs.");
                        }
                    }
                    else {
                        GltfInstancing::logError("Failed to load non-instanced GLB for splitting: " + nonInstancedGlbPath.string());
                    }
                }
                else {
                    GltfInstancing::logError("Non-instanced GLB not found (" + nonInstancedGlbPath.string() + "). Cannot run Quadtree Pipeline.");
                }
            }

            // 2. Run Pipeline
            if (!quadtreeInputPath.empty()) {
                // Create a temporary config that points to the new input directory
                ToolConfiguration quadConfig = config;
                quadConfig.inputDirectory = quadtreeInputPath;
                // Output to a subfolder to avoid overwriting standard output
                quadConfig.outputDirectory = OutputPaths::hlodDir(config).string();

                GltfInstancing::logInfo("Starting Quadtree Pipeline...");

                // Ensure Quadtree output directory exists
                std::filesystem::create_directories(quadConfig.outputDirectory);

                QuadtreePipeline::Pipeline pipeline(quadConfig);
                pipeline.run();
                GltfInstancing::logInfo("Quadtree Pipeline Finished. Output at: " + quadConfig.outputDirectory);
            }
        }

        // Experiment 6: Cross-GLB HLOD Comparison (if enabled)
        if (config.enableExperimentMode && config.runCrossGlbHLODExperiment) {
            GltfInstancing::logInfo("Running Experiment 6: Cross-GLB HLOD Comparison...");

            // Prepare input GLB list
            std::vector<std::string> inputGlbs;
            for (const auto& model : loadedModels) {
                inputGlbs.push_back(model.originalPath.string());
            }

            if (inputGlbs.size() >= 2) {
                std::string datasetName = config.experimentDatasetName.empty() ?
                    "multi_glb_dataset" : config.experimentDatasetName;

                std::filesystem::path experimentsBaseDir = std::filesystem::path(config.outputDirectory) / "experiments";
                ExperimentFramework::ExperimentDirectoryManager expManager(experimentsBaseDir);

                Experiment6::runExperiment6(config, loadedModels, inputGlbs, datasetName, expManager);
            } else {
                GltfInstancing::logWarning("Experiment 6 requires at least 2 GLB files. Skipping.");
            }
        }

        // 实验模式：生成端到端实验目录 (04_EndToEnd) - 汇总所有阶段结果
        if (config.enableExperimentMode) {
            std::string datasetName = config.experimentDatasetName.empty() ? "default_dataset" : config.experimentDatasetName;
            std::string strategyId = config.experimentStrategyId.empty() ? "FullPipeline" : config.experimentStrategyId;

            std::filesystem::path experimentsBaseDir = std::filesystem::path(config.outputDirectory) / "experiments";
            ExperimentFramework::ExperimentDirectoryManager expManager(experimentsBaseDir);

            ExperimentFramework::StrategyInfo strategy;
            strategy.id = strategyId;
            strategy.name = "End-to-End Full Pipeline";
            strategy.description = "Complete pipeline: Instancing + LOD + HLOD";
            strategy.parameters["instancing_enabled"] = "true";
            strategy.parameters["lod_enabled"] = std::to_string(config.enableInstanceLodGeneration);
            strategy.parameters["hlod_enabled"] = std::to_string(config.enableQuadtree);
            strategy.parameters["tolerance"] = std::to_string(config.geometryTolerance);

            GltfInstancing::logInfo("Creating experiment structure for END_TO_END, dataset: " + datasetName + ", strategy: " + strategyId);
            auto expDir = expManager.createExperimentStructure(
                ExperimentFramework::ExperimentType::END_TO_END,
                datasetName, strategy);
            GltfInstancing::logInfo("End-to-end experiment directory created at: " + expDir.string());

            // 汇总所有阶段的CSV文件到实验目录（输出文件名标明 instance / non-instance）
            std::vector<std::pair<std::string, std::filesystem::path>> filesToCopy = {
                {"instancing_analysis.csv", OutputPaths::instancingAnalysisCsv(config)},
                {"instance_lod_analysis.csv", OutputPaths::instanceLodAnalysisCsv(config)},
                {"non_instance_lod_analysis.csv", OutputPaths::nonInstanceLodAnalysisCsv(config)},
                {"hlod_analysis.csv", OutputPaths::hlodAnalysisCsv(config)}
            };

            for (const auto& [filename, sourcePath] : filesToCopy) {
                if (std::filesystem::exists(sourcePath)) {
                    std::filesystem::path destPath = expDir / filename;
                    std::filesystem::copy_file(sourcePath, destPath, std::filesystem::copy_options::overwrite_existing);
                    GltfInstancing::logInfo("Copied " + filename + " to end-to-end experiment directory");
                }
            }

            // 写入配置
            std::filesystem::path configPath = expDir / "config.json";
            ExperimentFramework::ConfigGenerator::writeConfigJson(configPath, config, strategy);

            // 生成README
            std::map<std::string, ExperimentFramework::MetricValue> e2eMetrics;
            e2eMetrics["Instancing Enabled"] = {"Instancing", 1.0, "boolean", "GPU instancing enabled"};
            e2eMetrics["LOD Enabled"] = {"LOD", config.enableInstanceLodGeneration ? 1.0 : 0.0, "boolean", "LOD generation enabled"};
            e2eMetrics["HLOD Enabled"] = {"HLOD", config.enableQuadtree ? 1.0 : 0.0, "boolean", "Quadtree HLOD enabled"};
            std::filesystem::path readmePath = expDir / "README.md";
            ExperimentFramework::ReadmeGenerator::writeStrategyReadme(readmePath, strategy, e2eMetrics);
            GltfInstancing::logInfo("End-to-end experiment README written to: " + readmePath.string());
        }

        GltfInstancing::logInfo("GltfInstancingTool finished successfully.");
        return 0;
    }
}
