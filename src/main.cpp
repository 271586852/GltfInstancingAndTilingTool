#include "glb_reader.h"
#include "instancing_detector.h"
#include "glb_writer.h"
#include "tileset_writer.h"
#include "utilities.h" // For logging
#include "ToolConfiguration.h" // Configuration struct
#include "semantic_parser.h" // 新增
#include "instancingLOD_manager.h"     // 新增
#include "NonInstancingLOD_manager.h" // 新增
#include "HLODPipeline.h" // 新增

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
#include <CesiumGltf\ExtensionExtMeshGpuInstancing.h>

// Structure to hold all configuration parameters
// Moved to ToolConfiguration.h


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
            // --- LOD Config Parsing ---
            else if (key == "enable_lod_generation") {
                std::transform(value.begin(), value.end(), value.begin(), ::tolower);
                if (value == "true" || value == "1" || value == "yes") config.enableLodGeneration = true;
                else config.enableLodGeneration = false;
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
            } else if (key == "enable_hlod" || key == "enable_quadtree") {
                std::transform(value.begin(), value.end(), value.begin(), ::tolower);
                if (value == "true" || value == "1" || value == "yes") config.enableQuadtree = true;
                else config.enableQuadtree = false;
            } else if (key == "hlod_max_depth" || key == "quadtree_max_depth") {
                try { config.quadtreeMaxDepth = std::stoi(value); } catch(...) {}
            } else if (key == "hlod_max_objects_per_tile" || key == "quadtree_max_objects_per_tile") {
                try { config.quadtreeMaxObjectsPerTile = std::stoi(value); } catch(...) {}
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
    GltfInstancing::logInfo("  --enable-hlod:                       Enable HLOD pipeline. Default: false.");
    GltfInstancing::logInfo("  --hlod-max-depth <value>:            Max depth for HLOD. Default: 6.");
    GltfInstancing::logInfo("  --hlod-max-objs <value>:             Max objects per tile for HLOD splitting. Default: 50.");
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

    // 3. Ratios
    double nodeReduction = (initialNodes > 0) ? (1.0 - (double)finalNodes / (double)initialNodes) * 100.0 : 0.0;
    double initialInstancingRatio = (totalDisplayedMeshes > 0) ? ((double)initialInstances / (double)totalDisplayedMeshes) * 100.0 : 0.0;
    double finalInstancingRatio = (totalDisplayedMeshes > 0) ? ((double)finalInstances / (double)totalDisplayedMeshes) * 100.0 : 0.0;
    double instancingIncrease = finalInstancingRatio - initialInstancingRatio;

    // 4. Write CSV
    std::filesystem::path csvPath = std::filesystem::path(config.outputDirectory) / "instancing_analysis.csv";
    std::ofstream csvFile(csvPath);
    if (csvFile.is_open()) {
        csvFile << "Input Models,Initial Nodes,Initial Meshes,Initial Instances,Instanced Groups,Final Instances,Non-instanced Meshes,Final Nodes,Final Meshes,Total Displayed Meshes,Node Reduction (%),Initial Instancing Ratio (%),Final Instancing Ratio (%),Instancing Increase (%)\n";
        csvFile << inputModels << ","
                << initialNodes << ","
                << initialMeshes << ","
                << initialInstances << ","
                << instancedGroups << ","
                << finalInstances << ","
                << nonInstancedMeshes << ","
                << finalNodes << ","
                << finalMeshes << ","
                << totalDisplayedMeshes << ","
                << std::fixed << std::setprecision(2) << nodeReduction << ","
                << initialInstancingRatio << ","
                << finalInstancingRatio << ","
                << instancingIncrease << "\n";
        csvFile.close();
        GltfInstancing::logInfo("Instancing analysis CSV written to: " + csvPath.string());
    } else {
        GltfInstancing::logError("Failed to write instancing analysis CSV to: " + csvPath.string());
    }
}

struct LodStats {
    int level;
    double fileSizeMB;
    int uniqueMeshes;
    size_t totalInstances;
    size_t totalVertices;
};

// Helper to write LOD analysis report
void writeLodAnalysisCsv(const ToolConfiguration& config, 
                        const std::vector<LodStats>& stats,
                        double originalFileSizeMB,
                        size_t originalVertices,
                        size_t originalInstances) {
    std::filesystem::path csvPath = std::filesystem::path(config.outputDirectory) / "instancing_lod_output" / "lod_analysis.csv";
    std::ofstream csvFile(csvPath);
    
    if (csvFile.is_open()) {
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
    std::filesystem::path nonInstancedGlbPath = std::filesystem::path(config.outputDirectory) / "non_instanced_meshes.glb";
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
            std::filesystem::path outputCsvPath = std::filesystem::path(config.outputDirectory) / outputFileName;

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
   /* #if _DEBUG
        std::cout << "Waiting for debugger to attach. Press Enter to continue..." << std::endl;
        std::cin.get();
    #endif*/

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
        } else if (arg == "--log-level") {
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

    if(useCustomConfigFile) {
        GltfInstancing::logInfo("Custom configuration file specified: " + customConfigFilePath);
        if (!loadConfigurationFromFile(customConfigFilePath, config)) {
            GltfInstancing::logError("Failed to load specified configuration file: " + customConfigFilePath + ". Exiting.");
            return 1; 
        }
    }

    // 2. Parse all command-line arguments (will override config file settings)
    int argIndex = 1; 
    while(argIndex < argc) {
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
            } else {
                GltfInstancing::logError("--input_directory option (CLI) requires a path."); printUsage(argv[0]); return 1;
            }
        } else if (arg == "--output_directory") {
            if (argIndex + 1 < argc) {
                config.outputDirectory = argv[++argIndex];
                config.outputDirectorySet = true;
            } else {
                GltfInstancing::logError("--output_directory option (CLI) requires a path."); printUsage(argv[0]); return 1;
            }
        } else if (arg == "--tolerance") {
            if (argIndex + 1 < argc) {
                try {
                    config.geometryTolerance = std::stod(argv[++argIndex]);
                    config.geometryToleranceSet = true;
                    GltfInstancing::logDebug("Command-line override: Using geometry tolerance: " + std::to_string(config.geometryTolerance));
                } catch (const std::exception& e) {
                    GltfInstancing::logError("Invalid value for --tolerance (CLI): " + std::string(argv[argIndex]) + ". Error: " + e.what()); printUsage(argv[0]); return 1;
                }
            } else {
                GltfInstancing::logError("--tolerance option (CLI) requires a value."); printUsage(argv[0]); return 1;
            }
        } else if (arg == "--skip-attribute-data-hash") {
            if (argIndex + 1 < argc) {
                config.attributesToSkipDataHash = splitAndTrim(argv[++argIndex], ',');
                config.attributesToSkipDataHashSet = true;
                if (!config.attributesToSkipDataHash.empty()) {
                    std::string attrsLogged = "Command-line override: Tolerance mode will skip data hashing for attributes: ";
                    for (const auto& attr : config.attributesToSkipDataHash) attrsLogged += attr + " ";
                    GltfInstancing::logDebug(attrsLogged);
                }
            } else {
                GltfInstancing::logError("--skip-attribute-data-hash option (CLI) requires a comma-separated list."); printUsage(argv[0]); return 1;
            }
        } else if (arg == "--normal-tolerance") {
            if (argIndex + 1 < argc) {
                try {
                    config.normalTolerance = std::stod(argv[++argIndex]);
                    if (config.normalTolerance < 0.0) {
                        GltfInstancing::logWarning("WARNING (CLI): Normal tolerance cannot be negative. Using 0.0.");
                        config.normalTolerance = 0.0;
                    }
                    config.normalToleranceSet = true;
                    GltfInstancing::logDebug("Command-line override: Using normal tolerance: " + std::to_string(config.normalTolerance));
                } catch (const std::exception& e) {
                    GltfInstancing::logError("Invalid value for --normal-tolerance (CLI): " + std::string(argv[argIndex]) + ". Error: " + e.what()); printUsage(argv[0]); return 1;
                }
            } else {
                GltfInstancing::logError("--normal-tolerance option (CLI) requires a value."); printUsage(argv[0]); return 1;
            }
        } else if (arg == "--merge-all-glb") {
            config.mergeAllGlb = true;
            config.mergeAllGlbSet = true;
            GltfInstancing::logDebug("Command-line override: Merge all GLB outputs enabled.");
        } else if (arg == "--instance-limit") {
            if (argIndex + 1 < argc) {
                try {
                    config.instanceLimit = std::stoi(argv[++argIndex]);
                     if (config.instanceLimit < 1) {
                        GltfInstancing::logWarning("WARNING (CLI): Instance limit must be >= 1. Using default 2.");
                        config.instanceLimit = 2;
                    }
                    config.instanceLimitSet = true;
                    GltfInstancing::logDebug("Command-line override: Using instance limit: " + std::to_string(config.instanceLimit));
                } catch (const std::exception& e) {
                    GltfInstancing::logError("Invalid value for --instance-limit (CLI): " + std::string(argv[argIndex]) + ". Error: " + e.what()); printUsage(argv[0]); return 1;
                }
            } else {
                GltfInstancing::logError("--instance-limit option (CLI) requires a value."); printUsage(argv[0]); return 1;
            }
        } else if (arg == "--mesh-segmentation") {
            config.meshSegmentation = true;
            config.meshSegmentationSet = true;
            GltfInstancing::logDebug("Command-line override: Mesh segmentation enabled (each mesh to a separate GLB).");
        } else if (arg == "--csv-dir") {
            if (argIndex + 1 < argc) {
                config.csvDirectory = argv[++argIndex];
                config.csvDirectorySet = true;
                GltfInstancing::logDebug("Command-line override: CSV processing directory set to: " + config.csvDirectory);
            } else {
                GltfInstancing::logError("--csv-dir option (CLI) requires a path."); printUsage(argv[0]); return 1;
            }
        } else if (arg == "--enable-hlod" || arg == "--enable-quadtree") {
            config.enableQuadtree = true;
            config.enableQuadtreeSet = true;
        } else if (arg == "--hlod-max-depth" || arg == "--quadtree-max-depth") {
            if (argIndex + 1 < argc) {
                try {
                    config.quadtreeMaxDepth = std::stoi(argv[++argIndex]);
                } catch(...) {
                    GltfInstancing::logWarning("Invalid value for --hlod-max-depth. Using default.");
                }
            }
        } else if (arg == "--hlod-max-objs" || arg == "--quadtree-max-objs") {
             if (argIndex + 1 < argc) {
                try {
                    config.quadtreeMaxObjectsPerTile = std::stoi(argv[++argIndex]);
                } catch(...) {
                    GltfInstancing::logWarning("Invalid value for --hlod-max-objs. Using default.");
                }
            }
        } else { // An unknown option
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

    // 5. Validate crucial final configuration & create output directory
    if (!std::filesystem::is_directory(config.inputDirectory)) {
        GltfInstancing::logError("Final input directory does not exist or is not a directory: " + config.inputDirectory);
        return 1;
    }
    if (!std::filesystem::exists(config.outputDirectory)) {
        try {
            if (std::filesystem::create_directories(config.outputDirectory)) {
                GltfInstancing::logInfo("Created output directory: " + config.outputDirectory);
            } else if (!std::filesystem::is_directory(config.outputDirectory)) {
                 GltfInstancing::logError("Failed to create output directory (or it's not a directory): " + config.outputDirectory);
                 return 1;
            }
        } catch (const std::filesystem::filesystem_error& e) {
            GltfInstancing::logError("Failed to create output directory: " + config.outputDirectory + ". Error: " + e.what());
            return 1;
        }
    } else if (!std::filesystem::is_directory(config.outputDirectory)) {
        GltfInstancing::logError("Output path exists but is not a directory: " + config.outputDirectory);
        return 1;
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
    GltfInstancing::InstancingDetector detector(config.geometryTolerance, config.attributesToSkipDataHash, config.normalTolerance, config.instanceLimit);
    GltfInstancing::InstancingDetectionResult detectionResult = detector.detect(loadedModels);

    GltfInstancing::GlbWriter glbWriter;
    GltfInstancing::TilesetWriter tilesetWriter;

    // --- REPORT GENERATION (Restored - Text) ---
    std::string reportContent = detectionResult.getReport();
    std::filesystem::path reportPath = std::filesystem::path(config.outputDirectory) / "instancing_analysis.txt";
    std::ofstream reportFile(reportPath);
    if (reportFile.is_open()) {
        reportFile << reportContent;
        reportFile.close();
        // GltfInstancing::logInfo("Instancing analysis report written to: " + reportPath.string());
    } else {
        GltfInstancing::logError("Failed to write instancing analysis report to: " + reportPath.string());
    }

    // --- STANDARD OUTPUT GENERATION (Always run) ---
    // (Generate CSV Report first)
    writeAnalysisCsv(config, loadedModels, detectionResult);
    
    std::filesystem::path instancedGlbFileNameBase = "instanced_meshes";
    std::filesystem::path nonInstancedGlbFileNameBase = "non_instanced_meshes";
    std::vector<std::filesystem::path> stage1_outputGlbs;

    std::optional<std::pair<std::filesystem::path, GltfInstancing::BoundingBox>> instancedWriteResult;
    std::optional<std::pair<std::filesystem::path, GltfInstancing::BoundingBox>> nonInstancedWriteResult;

    if (config.mergeAllGlb) {
        std::filesystem::path mergedInstancedGlbPath = std::filesystem::path(config.outputDirectory) / (instancedGlbFileNameBase.string() + ".glb");
        instancedWriteResult = glbWriter.writeInstancedMeshesOnly(loadedModels, detectionResult, mergedInstancedGlbPath);
        if (instancedWriteResult) stage1_outputGlbs.push_back(instancedWriteResult->first);

        std::filesystem::path mergedNonInstancedGlbPath = std::filesystem::path(config.outputDirectory) / (nonInstancedGlbFileNameBase.string() + ".glb");
        nonInstancedWriteResult = glbWriter.writeNonInstancedMeshesOnly(loadedModels, detectionResult, mergedNonInstancedGlbPath);
        if (nonInstancedWriteResult) stage1_outputGlbs.push_back(nonInstancedWriteResult->first);
    } else {
        std::filesystem::path instancedGlbPath = std::filesystem::path(config.outputDirectory) / (instancedGlbFileNameBase.string() + ".glb");
        instancedWriteResult = glbWriter.writeInstancedMeshesOnly(loadedModels, detectionResult, instancedGlbPath);
        if (instancedWriteResult) stage1_outputGlbs.push_back(instancedWriteResult->first);
    
        std::filesystem::path nonInstancedGlbPath = std::filesystem::path(config.outputDirectory) / (nonInstancedGlbFileNameBase.string() + ".glb");
        nonInstancedWriteResult = glbWriter.writeNonInstancedMeshesOnly(loadedModels, detectionResult, nonInstancedGlbPath);
        if (nonInstancedWriteResult) stage1_outputGlbs.push_back(nonInstancedWriteResult->first);
    }

    if (instancedWriteResult && instancedWriteResult->second.isValid()) {
        std::filesystem::path instancedTilesetPath = std::filesystem::path(config.outputDirectory) / "tileset_instanced.json";
        std::vector<std::filesystem::path> instancedUris = { instancedWriteResult->first };
        GltfInstancing::BoundingBox bbox = instancedWriteResult->second;
        glm::dvec3 extents = bbox.max - bbox.min;
        double diagonal = glm::length(extents);
        double rootGeometricError = (diagonal > 0) ? (diagonal * 0.1) : 1.0;
        if (rootGeometricError < 1.0) rootGeometricError = 1.0;
        tilesetWriter.writeTileset(instancedUris, instancedTilesetPath, rootGeometricError);
    }

    if (nonInstancedWriteResult && nonInstancedWriteResult->second.isValid()) {
        std::filesystem::path nonInstancedTilesetPath = std::filesystem::path(config.outputDirectory) / "tileset_non_instanced.json";
        std::vector<std::filesystem::path> nonInstancedUris = { nonInstancedWriteResult->first };
        GltfInstancing::BoundingBox bbox = nonInstancedWriteResult->second;
        glm::dvec3 extents = bbox.max - bbox.min;
        double diagonal = glm::length(extents);
        double rootGeometricError = (diagonal > 0) ? (diagonal * 0.1) : 1.0;
        if (rootGeometricError < 1.0) rootGeometricError = 1.0;
        tilesetWriter.writeTileset(nonInstancedUris, nonInstancedTilesetPath, rootGeometricError);
    }

    // --- Non-Instanced LOD Generation with Post-LOD Instancing ---
    if (config.enableNonInstancedLodGeneration && nonInstancedWriteResult) {
         GltfInstancing::logInfo("Generating LODs for non-instanced meshes with post-process instancing...");
         std::filesystem::path lodOutputDir = std::filesystem::path(config.outputDirectory) / "non_instancing_lod_output";
         
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
                 } else {
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
                 
                 GltfInstancing::InstancingDetector lodDetector(config.geometryTolerance, config.attributesToSkipDataHash, config.normalTolerance, config.instanceLimit);

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
                     } else if (levelContents.size() == 1) {
                         finalUri = levelContents[0].filename().string();
                     } else {
                         std::string wrapperName = baseName + "_wrapper.json";
                         std::filesystem::path wrapperPath = instancedOutputDir / wrapperName;
                         
                         if (tilesetWriter.writeWrapperTileset(levelContents, wrapperPath, levelInfo.geometricError)) {
                             finalUri = wrapperName;
                         } else {
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
                         } else {
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
            std::filesystem::path segmentationOutputDir = std::filesystem::path(config.outputDirectory) / "segmented_glb_output";
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
    if (config.enableLodGeneration) {
        GltfInstancing::logInfo("LOD Generation Enabled. Loading semantic data...");
        
        GltfInstancing::SemanticParser semanticParser;
        if (!config.semanticDataPath.empty() && std::filesystem::exists(config.semanticDataPath)) {
            semanticParser.parse(config.semanticDataPath);
        } else {
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

        std::filesystem::path lodOutputDir = std::filesystem::path(config.outputDirectory) / "instancing_lod_output";
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
                } catch (...) { stats.fileSizeMB = 0.0; }
                
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
             try { originalFileSizeMB += (double)std::filesystem::file_size(p) / (1024.0 * 1024.0); } catch(...) {}
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
                } else {
                    currentNode->children.push_back(levelNodes[l]);
                    currentNode = &currentNode->children.back();
                }
            }
        }

        if (firstNodeFound) {
            std::filesystem::path tilesetPath = lodOutputDir / "tileset.json";
            tilesetWriter.writeHierarchicalTileset(rootNode, tilesetPath);
            GltfInstancing::logInfo("LOD processing complete. Tileset written to: " + tilesetPath.string());
        } else {
            GltfInstancing::logError("LOD generation failed to produce any valid levels.");
        }

    }

    // Stage 3: CSV Processing (Always run if configured)
    processCsvAgainstGlb(config);

    // --- HLOD Pipeline Execution ---
    // HLOD is now executed AFTER standard processing, using the results of Stage 1/2.
    // NOTE: It requires separated GLB files. 
    // If meshSegmentation was NOT enabled by user, we perform a temporary segmentation here for HLOD.
    if (config.enableQuadtree) {
        GltfInstancing::logInfo("HLOD Pipeline Enabled (Post-Processing Stage).");
        
        std::string quadtreeInputPath = "";
        bool tempInput = false;

        // 1. Determine Input Source
        if (config.meshSegmentation) {
             // User already generated segmented files, use them directly
             quadtreeInputPath = (std::filesystem::path(config.outputDirectory) / "segmented_glb_output").string();
             GltfInstancing::logInfo("Using existing segmented output for HLOD input: " + quadtreeInputPath);
        } else {
             // We need to generate separated files from the non-instanced result of Stage 1
             GltfInstancing::logInfo("Mesh segmentation was not enabled. Generating temporary separated GLBs for HLOD input...");
             
             // Locate Non-Instanced Output from Stage 1
             std::filesystem::path nonInstancedGlbPath = std::filesystem::path(config.outputDirectory) / "non_instanced_meshes.glb";
             
             if (std::filesystem::exists(nonInstancedGlbPath)) {
                 std::filesystem::path tempOutputDir = std::filesystem::path(config.outputDirectory) / "HLOD_temp_input";
                 std::filesystem::create_directories(tempOutputDir);
                 
                 GltfInstancing::GlbReader splitReader;
                 std::set<std::filesystem::path> fileSet = { nonInstancedGlbPath };
                 auto modelsToSplit = splitReader.loadGltfModels(fileSet);
                 
                 GltfInstancing::GlbWriter splitWriter;
                 if (!modelsToSplit.empty()) {
                     if (splitWriter.writeMeshesAsSeparateGlbs(modelsToSplit, tempOutputDir)) {
                         quadtreeInputPath = tempOutputDir.string();
                         tempInput = true; 
                         GltfInstancing::logInfo("Generated temporary HLOD input at: " + quadtreeInputPath);
                     } else {
                         GltfInstancing::logError("Failed to generate temporary separated GLBs.");
                     }
                 } else {
                      GltfInstancing::logError("Failed to load non-instanced GLB for splitting: " + nonInstancedGlbPath.string());
                 }
             } else {
                 GltfInstancing::logError("Non-instanced GLB not found (" + nonInstancedGlbPath.string() + "). Cannot run HLOD Pipeline.");
             }
        }

        // 2. Run Pipeline
        if (!quadtreeInputPath.empty()) {
            // Create a temporary config that points to the new input directory
            ToolConfiguration quadConfig = config;
            quadConfig.inputDirectory = quadtreeInputPath;
            // Output to a subfolder to avoid overwriting standard output
            quadConfig.outputDirectory = (std::filesystem::path(config.outputDirectory) / "HLOD_output").string();
            
            GltfInstancing::logInfo("Starting HLOD Pipeline...");
            
            // Ensure HLOD output directory exists
            std::filesystem::create_directories(quadConfig.outputDirectory);

            HLOD::Pipeline pipeline(quadConfig);
            pipeline.run();
            GltfInstancing::logInfo("HLOD Pipeline Finished. Output at: " + quadConfig.outputDirectory);
            
            // Optional: Cleanup temp
             if (tempInput) {
                 // std::filesystem::remove_all(quadtreeInputPath); 
                 // Keeping it might be useful for debug
             }
        }
    }

    GltfInstancing::logInfo("GltfInstancingTool finished successfully.");
    return 0;
}
