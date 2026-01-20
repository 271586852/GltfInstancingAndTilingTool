#include "glb_reader.h"
#include "instancing_detector.h"
#include "glb_writer.h"
#include "tileset_writer.h"
#include "utilities.h" // For logging



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
struct ToolConfiguration {
    std::string inputDirectory;
    std::string outputDirectory;
    double geometryTolerance = 0.0;
    double normalTolerance = 0.0;
    std::set<std::string> attributesToSkipDataHash;
    bool mergeAllGlb = false;
    int instanceLimit = 2; // Default to 2 (original behavior: >1 instance is grouped)
    bool meshSegmentation = false; // New flag, default to false
    std::string csvDirectory;
    bool csvDirectorySet = false;

    // Flags to track if a parameter was set, can be useful for merging/override logic
    bool inputDirectorySet = false;
    bool outputDirectorySet = false;
    bool geometryToleranceSet = false;
    bool normalToleranceSet = false;
    bool attributesToSkipDataHashSet = false;
    bool mergeAllGlbSet = false;
    bool instanceLimitSet = false;
    bool meshSegmentationSet = false; // Flag to track if meshSegmentation was set

    // Flags to track if a parameter was set from any source (config or CLI)
    bool inputDirectorySource = false; // True if set by config or CLI
    bool outputDirectorySource = false;
    // Add more ..Set flags if needed for very specific override logic or default application
};

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
            } else {
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

    // --- Instancing Analysis: Before ---
    size_t totalNodesBefore = 0;
    size_t totalMeshesBefore = 0;
    size_t totalInstancesBefore = 0;

    for (const auto& loadedModel : loadedModels) {
        totalNodesBefore += loadedModel.model.nodes.size();
        totalMeshesBefore += loadedModel.model.meshes.size();
        for (const auto& node : loadedModel.model.nodes) {
            auto it = node.extensions.find("EXT_mesh_gpu_instancing");
            if (it != node.extensions.end()) {
                const CesiumGltf::ExtensionExtMeshGpuInstancing* pInstancing = std::any_cast<CesiumGltf::ExtensionExtMeshGpuInstancing>(&it->second);
                if (pInstancing) {
                    auto attr_it = pInstancing->attributes.find("TRANSLATION");
                    if (attr_it != pInstancing->attributes.end()) {
                        int32_t accessorIndex = attr_it->second;
                        if (accessorIndex >= 0 && static_cast<size_t>(accessorIndex) < loadedModel.model.accessors.size()) {
                            totalInstancesBefore += loadedModel.model.accessors[accessorIndex].count;
                        }
                    }
                }
            }
        }
    }
    // ---

    GltfInstancing::logInfo("Stage 1: Detecting instancing opportunities...");
    GltfInstancing::InstancingDetector detector(config.geometryTolerance, config.attributesToSkipDataHash, config.normalTolerance, config.instanceLimit);
    GltfInstancing::InstancingDetectionResult detectionResult = detector.detect(loadedModels);

    // --- Instancing Analysis: After ---
    size_t totalInstancesAfter = 0;
    for (const auto& group : detectionResult.instancedGroups) {
        totalInstancesAfter += group.instances.size();
    }

    size_t meshesAfter = detectionResult.instancedGroups.size() + detectionResult.nonInstancedMeshes.size();
    
    // The number of nodes after processing will be one node for each instanced group, 
    // plus one node for each non-instanced mesh.
    size_t nodesAfter = detectionResult.instancedGroups.size() + detectionResult.nonInstancedMeshes.size();
    size_t totalDisplayedMeshes = totalInstancesAfter + detectionResult.nonInstancedMeshes.size();

    double initialInstancingRatio = 0.0;
    double finalInstancingRatio = 0.0;
    double instancingIncrease = 0.0;

    if (totalDisplayedMeshes > 0) {
        initialInstancingRatio = 100.0 * static_cast<double>(totalInstancesBefore) / totalDisplayedMeshes;
        finalInstancingRatio = 100.0 * static_cast<double>(totalInstancesAfter) / totalDisplayedMeshes;
        instancingIncrease = finalInstancingRatio - initialInstancingRatio;
    }

    GltfInstancing::logInfo("--- Instancing Analysis ---");
    GltfInstancing::logInfo("Initial state:");
    GltfInstancing::logInfo("  Total models loaded: " + std::to_string(loadedModels.size()));
    GltfInstancing::logInfo("  Total nodes: " + std::to_string(totalNodesBefore));
    GltfInstancing::logInfo("  Total meshes: " + std::to_string(totalMeshesBefore));
    GltfInstancing::logInfo("  Total instances (from EXT_mesh_gpu_instancing): " + std::to_string(totalInstancesBefore));

    GltfInstancing::logInfo("Detection result:");
    GltfInstancing::logInfo("  Unique meshes identified for instancing: " + std::to_string(detectionResult.instancedGroups.size()));
    GltfInstancing::logInfo("  Total instances to be created: " + std::to_string(totalInstancesAfter));
    GltfInstancing::logInfo("  Meshes not qualifying for instancing: " + std::to_string(detectionResult.nonInstancedMeshes.size()));

    GltfInstancing::logInfo("Post-processing state (projected):");
    GltfInstancing::logInfo("  Total meshes in output: " + std::to_string(meshesAfter));
    GltfInstancing::logInfo("  Total nodes in output: " + std::to_string(nodesAfter));
    GltfInstancing::logInfo("  Total displayed meshes (instances + non-instanced): " + std::to_string(totalDisplayedMeshes));

    if (totalNodesBefore > 0) {
        double reductionPercentage = 100.0 * (static_cast<double>(totalNodesBefore - nodesAfter) / totalNodesBefore);
        std::stringstream stream;
        stream << std::fixed << std::setprecision(2) << reductionPercentage;
        GltfInstancing::logInfo("Node reduction: " + std::to_string(totalNodesBefore) + " -> " + std::to_string(nodesAfter) + 
                                " (a " + stream.str() + "% reduction)");
    }
    
    std::stringstream initialRatioStream, finalRatioStream, increaseStream;
    initialRatioStream << std::fixed << std::setprecision(2) << initialInstancingRatio;
    finalRatioStream << std::fixed << std::setprecision(2) << finalInstancingRatio;
    increaseStream << std::fixed << std::setprecision(2) << instancingIncrease;

    GltfInstancing::logInfo("Initial Instancing Ratio (initial instances / total displayed): " + initialRatioStream.str() + "%");
    GltfInstancing::logInfo("Final Instancing Ratio (final instances / total displayed): " + finalRatioStream.str() + "%");
    GltfInstancing::logInfo("Instancing Increase (Final Ratio - Initial Ratio): " + increaseStream.str() + "%");

    GltfInstancing::logInfo("--------------------------");
    // ---

    // --- Write Analysis to CSV ---
    std::filesystem::path analysisCsvPath = std::filesystem::path(config.outputDirectory) / "instancing_analysis.csv";
    std::ofstream analysisCsvFile(analysisCsvPath);
    if (analysisCsvFile.is_open()) {
        GltfInstancing::logInfo("Writing instancing analysis to: " + analysisCsvPath.string());

        // Header
        analysisCsvFile << "Input Models,Initial Nodes,Initial Meshes,Initial Instances,"
                        << "Instanced Groups,Final Instances,Non-instanced Meshes,"
                        << "Final Nodes,Final Meshes,Total Displayed Meshes,Node Reduction (%),"
                        << "Initial Instancing Ratio (%),Final Instancing Ratio (%),Instancing Increase (%)\n";

        // Data
        double reductionPercentage = 0.0;
        if (totalNodesBefore > 0) {
            reductionPercentage = 100.0 * (static_cast<double>(totalNodesBefore - nodesAfter) / totalNodesBefore);
        }
        std::stringstream stream;
        stream << std::fixed << std::setprecision(2) << reductionPercentage;

        analysisCsvFile << loadedModels.size() << ","
                        << totalNodesBefore << ","
                        << totalMeshesBefore << ","
                        << totalInstancesBefore << ","
                        << detectionResult.instancedGroups.size() << ","
                        << totalInstancesAfter << ","
                        << detectionResult.nonInstancedMeshes.size() << ","
                        << nodesAfter << ","
                        << meshesAfter << ","
                        << totalDisplayedMeshes << ","
                        << stream.str() << ","
                        << initialRatioStream.str() << ","
                        << finalRatioStream.str() << ","
                        << increaseStream.str() << "\n";

        analysisCsvFile.close();
    } else {
        GltfInstancing::logError("Failed to open for writing: " + analysisCsvPath.string());
    }
    // ---

    if (detectionResult.instancedGroups.empty() && detectionResult.nonInstancedMeshes.empty()) {
        GltfInstancing::logInfo("No meshes found or no instancing opportunities during Stage 1. Nothing to write for instancing/non-instancing separation.");
        // If meshSegmentation is true, we might still want to segment original files if no instancing output was produced.
        // However, the user query implies segmentation AFTER instancing outputs. So if these are empty, there's nothing to segment later.
    }

    GltfInstancing::logInfo("Stage 1: Writing instanced and non-instanced GLB files...");
    GltfInstancing::GlbWriter glbWriter;
    std::filesystem::path instancedGlbFileNameBase = "instanced_meshes";
    std::filesystem::path nonInstancedGlbFileNameBase = "non_instanced_meshes";
    std::vector<std::filesystem::path> stage1_outputGlbs; // Store paths of GLBs generated in stage 1

    std::optional<std::pair<std::filesystem::path, GltfInstancing::BoundingBox>> instancedWriteResult;
    std::optional<std::pair<std::filesystem::path, GltfInstancing::BoundingBox>> nonInstancedWriteResult;

    if (config.mergeAllGlb) {
        GltfInstancing::logDebug("MergeAllGlb is enabled. Writing merged instanced and non-instanced files.");
        std::filesystem::path mergedInstancedGlbPath = std::filesystem::path(config.outputDirectory) / (instancedGlbFileNameBase.string() + ".glb");
        instancedWriteResult = glbWriter.writeInstancedMeshesOnly(loadedModels, detectionResult, mergedInstancedGlbPath);
        if (instancedWriteResult) {
            GltfInstancing::logInfo("Merged Instanced GLB written to: " + instancedWriteResult->first.string());
            stage1_outputGlbs.push_back(instancedWriteResult->first);
        } else {
            GltfInstancing::logError("Failed to write merged instanced GLB.");
        }

        std::filesystem::path mergedNonInstancedGlbPath = std::filesystem::path(config.outputDirectory) / (nonInstancedGlbFileNameBase.string() + ".glb");
        nonInstancedWriteResult = glbWriter.writeNonInstancedMeshesOnly(loadedModels, detectionResult, mergedNonInstancedGlbPath);
        if (nonInstancedWriteResult) {
            GltfInstancing::logInfo("Merged Non-Instanced GLB written to: " + nonInstancedWriteResult->first.string());
            stage1_outputGlbs.push_back(nonInstancedWriteResult->first);
        } else {
            GltfInstancing::logError("Failed to write merged non-instanced GLB.");
        }
    } else {
        GltfInstancing::logDebug("MergeAllGlb is disabled. Processing files individually (if applicable - current writer writes combined files).");
        // Current GlbWriter's writeInstancedMeshesOnly and writeNonInstancedMeshesOnly
        // already combine all input models' results into single output files.
        // If per-input-file output was desired, GlbWriter would need significant changes.
        // For now, assume it behaves like mergeAllGlb=true in terms of output file count for these calls.
        std::filesystem::path instancedGlbPath = std::filesystem::path(config.outputDirectory) / (instancedGlbFileNameBase.string() + ".glb");
        instancedWriteResult = glbWriter.writeInstancedMeshesOnly(loadedModels, detectionResult, instancedGlbPath);
        if (instancedWriteResult) {
            GltfInstancing::logInfo("Instanced GLB written to: " + instancedWriteResult->first.string());
            stage1_outputGlbs.push_back(instancedWriteResult->first);
        } else {
            GltfInstancing::logError("Failed to write instanced GLB.");
        }
    
        std::filesystem::path nonInstancedGlbPath = std::filesystem::path(config.outputDirectory) / (nonInstancedGlbFileNameBase.string() + ".glb");
        nonInstancedWriteResult = glbWriter.writeNonInstancedMeshesOnly(loadedModels, detectionResult, nonInstancedGlbPath);
        if (nonInstancedWriteResult) {
            GltfInstancing::logInfo("Non-Instanced GLB written to: " + nonInstancedWriteResult->first.string());
            stage1_outputGlbs.push_back(nonInstancedWriteResult->first);
        } else {
            GltfInstancing::logError("Failed to write non-instanced GLB.");
        }
    }

    // Stage 1 Tileset Generation
    GltfInstancing::logInfo("Stage 1: Generating 3D Tilesets for Stage 1 outputs...");
    GltfInstancing::TilesetWriter tilesetWriter;

    // Generate tileset for instanced meshes
    if (instancedWriteResult && instancedWriteResult->second.isValid()) {
        std::filesystem::path instancedTilesetPath = std::filesystem::path(config.outputDirectory) / "tileset_instanced.json";
        std::vector<std::filesystem::path> instancedUris = { instancedWriteResult->first };
        
        GltfInstancing::BoundingBox bbox = instancedWriteResult->second;
        glm::dvec3 extents = bbox.max - bbox.min;
        double diagonal = glm::length(extents);
        double rootGeometricError = (diagonal > 0) ? (diagonal * 0.1) : 1.0;
        if (rootGeometricError < 1.0) rootGeometricError = 1.0;

        GltfInstancing::logDebug("Calculated root geometric error for instanced tileset: " + std::to_string(rootGeometricError));
        if (tilesetWriter.writeTileset(instancedUris, instancedTilesetPath, rootGeometricError)) {
            GltfInstancing::logInfo("Successfully wrote instanced tileset to: " + instancedTilesetPath.string());
        } else {
            GltfInstancing::logError("Failed to write the instanced tileset file.");
        }
    } else {
        GltfInstancing::logInfo("Skipping instanced tileset generation: no valid instanced GLB was produced.");
    }

    // Generate tileset for non-instanced meshes
    if (nonInstancedWriteResult && nonInstancedWriteResult->second.isValid()) {
        std::filesystem::path nonInstancedTilesetPath = std::filesystem::path(config.outputDirectory) / "tileset_non_instanced.json";
        std::vector<std::filesystem::path> nonInstancedUris = { nonInstancedWriteResult->first };

        GltfInstancing::BoundingBox bbox = nonInstancedWriteResult->second;
        glm::dvec3 extents = bbox.max - bbox.min;
        double diagonal = glm::length(extents);
        double rootGeometricError = (diagonal > 0) ? (diagonal * 0.1) : 1.0;
        if (rootGeometricError < 1.0) rootGeometricError = 1.0;

        GltfInstancing::logDebug("Calculated root geometric error for non-instanced tileset: " + std::to_string(rootGeometricError));
        if (tilesetWriter.writeTileset(nonInstancedUris, nonInstancedTilesetPath, rootGeometricError)) {
            GltfInstancing::logInfo("Successfully wrote non-instanced tileset to: " + nonInstancedTilesetPath.string());
        } else {
            GltfInstancing::logError("Failed to write the non-instanced tileset file.");
        }
    } else {
        GltfInstancing::logInfo("Skipping non-instanced tileset generation: no valid non-instanced GLB was produced.");
    }

    // Stage 2: Mesh Segmentation (if enabled)
    if (config.meshSegmentation) {
        GltfInstancing::logInfo("Stage 2: Mesh Segmentation enabled. Processing GLBs generated in Stage 1.");
        if (stage1_outputGlbs.empty()) {
            GltfInstancing::logInfo("No GLB files were generated in Stage 1. Skipping mesh segmentation.");
        } else {
            std::filesystem::path segmentationOutputDir = std::filesystem::path(config.outputDirectory) / "segmented_glb_output";
            if (!std::filesystem::exists(segmentationOutputDir)) {
                try {
                    if (std::filesystem::create_directories(segmentationOutputDir)) {
                        GltfInstancing::logInfo("Created directory for segmented GLBs: " + segmentationOutputDir.string());
                    } else if (!std::filesystem::is_directory(segmentationOutputDir)) {
                         GltfInstancing::logError("Failed to create directory for segmented GLBs (or it's not a directory): " + segmentationOutputDir.string());
                         return 1; // Critical error
                    }
                } catch (const std::filesystem::filesystem_error& e) {
                    GltfInstancing::logError("Failed to create directory for segmented GLBs: " + segmentationOutputDir.string() + ". Error: " + e.what());
                    return 1; // Critical error
                }
            } else if (!std::filesystem::is_directory(segmentationOutputDir)) {
                 GltfInstancing::logError("Path for segmented GLBs exists but is not a directory: " + segmentationOutputDir.string());
                 return 1; // Critical error
            }

            GltfInstancing::logInfo("Segmented GLBs will be saved to: " + segmentationOutputDir.string());
            
            // GlbReader for Stage 2 (re-reading Stage 1 outputs)
            GltfInstancing::GlbReader stage2Reader; 
            // GlbWriter for Stage 2 (will use its own internal state, writeMeshesAsSeparateGlbs resets it per mesh)
            // GlbWriter glbWriterForSegmentation; // Re-use glbWriter instance, its state is managed by writeMeshesAsSeparateGlbs

            std::vector<GltfInstancing::LoadedGltfModel> modelsToSegment;
            for (const auto& glbPath : stage1_outputGlbs) {
                if (std::filesystem::exists(glbPath)) {
                    GltfInstancing::logInfo("Loading Stage 1 GLB for segmentation: " + glbPath.string());
                    std::set<std::filesystem::path> singleFileSet;
                    singleFileSet.insert(glbPath);
                    std::vector<GltfInstancing::LoadedGltfModel> loadedSingleModelVec = stage2Reader.loadGltfModels(singleFileSet);
                    
                    if (!loadedSingleModelVec.empty()) {
                        modelsToSegment.insert(modelsToSegment.end(), loadedSingleModelVec.begin(), loadedSingleModelVec.end());
                    } else {
                        GltfInstancing::logWarning("WARNING: Failed to reload GLB for segmentation: " + glbPath.string());
                    }
                } else {
                    GltfInstancing::logWarning("WARNING: Stage 1 output GLB not found, cannot segment: " + glbPath.string());
                }
            }

            if (modelsToSegment.empty()) {
                GltfInstancing::logInfo("No valid Stage 1 GLB models could be loaded for segmentation.");
            } else {
                GltfInstancing::logInfo("Proceeding to segment " + std::to_string(modelsToSegment.size()) + " model(s) (from Stage 1 outputs).");
                // The glbWriter instance is already available.
                // Its writeMeshesAsSeparateGlbs method resets its internal state for each mesh.
                bool segmentationSuccess = glbWriter.writeMeshesAsSeparateGlbs(modelsToSegment, segmentationOutputDir);
                if (segmentationSuccess) {
                    GltfInstancing::logInfo("Stage 2: Mesh segmentation completed successfully.");
                } else {
                    GltfInstancing::logError("Stage 2: Mesh segmentation encountered errors.");
                    // Decide if this is a fatal error for the whole tool run. For now, just log.
                }
            }
        }
    } else {
        GltfInstancing::logInfo("Stage 2: Mesh Segmentation is disabled. Skipping.");
    }

    // Stage 3: CSV Processing
    processCsvAgainstGlb(config);

    GltfInstancing::logInfo("GltfInstancingTool finished successfully.");
    return 0;
}