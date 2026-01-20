#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/transform.hpp>

#include "glb_reader.h"
#include "utilities.h" // For readFileBytes, calculateFileSHA256, logging

#include <CesiumGltfReader/GltfReader.h> // Changed from GltfReaderResult.h
#include <gsl/span>                      // For gsl::span in readGltf
#include <CesiumJsonReader/JsonReader.h> // Used for some type declarations potentially, though nlohmann is primary for parsing
#include <CesiumJsonReader/JsonHandler.h>
#include <nlohmann/json.hpp> // For JSON parsing of tileset.json

#include <filesystem>   // For std::filesystem
#include <string>       // For std::string
#include <vector>       // For std::vector
#include <set>          // For std::set
#include <optional>     // For std::optional
#include <fstream>      // For std::ifstream (used by readFileBytes in utilities.h)
#include <algorithm>    // For std::transform (used in discoverGlbFiles)
#include <iostream>     // For std::cout, std::cerr (used by logging in utilities.h)


namespace GltfInstancing {

    GlbReader::GlbReader() {
        // Initialize any reader options if necessary
    }

    std::optional<LoadedGltfModel> GlbReader::readGlb(const std::filesystem::path& glbPath, int modelId) {
        logMessage("Reading GLB: " + glbPath.string());

        auto bytes = readFileBytes(glbPath);
        if (!bytes) {
            logError("Failed to read bytes from: " + glbPath.string());
            return std::nullopt;
        }

        // Construct gsl::span from the vector's data and size
        gsl::span<const std::byte> byte_span(
            reinterpret_cast<const std::byte*>(bytes->data()),
            bytes->size()
        );
        CesiumGltfReader::GltfReaderResult readerResult = _gltfReader.readGltf(byte_span);

        if (!readerResult.model) {
            logError("Failed to parse GLB: " + glbPath.string());
            for (const auto& error : readerResult.errors) {
                logError("  - " + error);
            }
            return std::nullopt;
        }
        if (!readerResult.warnings.empty()) {
            for (const auto& warn : readerResult.warnings) {
                logMessage("  Warning: " + warn);
            }
        }

        LoadedGltfModel loadedModel;
        loadedModel.model = std::move(*readerResult.model);
        loadedModel.originalPath = glbPath;
        loadedModel.fileHash = calculateFileSHA256(glbPath);
        loadedModel.uniqueId = modelId;

        logMessage("Successfully read GLB: " + glbPath.string() + " (Hash: " + loadedModel.fileHash + ")");
        return loadedModel;
    }

    // Helper to recursively find "uri" or "url" in a nlohmann::json object
    // and resolve them relative to the tileset's path.
    // This function is NOT a member of GlbReader class, it's a free function in this namespace.
    // If it were intended as a private member, it should be declared in glb_reader.h within the class.
    // For now, assuming it's a helper local to this .cpp or within the GltfInstancing namespace.
    void findContentUrisRecursiveHelper(const nlohmann::json& j, const std::filesystem::path& basePath, std::set<std::filesystem::path>& uris) {
        if (j.is_object()) {
            for (auto it = j.begin(); it != j.end(); ++it) {
                if (it.key() == "uri" || it.key() == "url") {
                    if (it.value().is_string()) {
                        std::string uriStr = it.value().get<std::string>();
                        if (!uriStr.empty()) {
                            bool endsWithTarget = false;
                            const std::string suffix_glb = ".glb";
                            const std::string suffix_gltf = ".gltf";

                            // C++17 compatible ends_with check for .glb
                            if (uriStr.length() >= suffix_glb.length()) {
                                if (uriStr.compare(uriStr.length() - suffix_glb.length(), suffix_glb.length(), suffix_glb) == 0) {
                                    endsWithTarget = true;
                                }
                            }

                            // C++17 compatible ends_with check for .gltf (only if not already .glb)
                            if (!endsWithTarget && uriStr.length() >= suffix_gltf.length()) {
                                if (uriStr.compare(uriStr.length() - suffix_gltf.length(), suffix_gltf.length(), suffix_gltf) == 0) {
                                    endsWithTarget = true;
                                }
                            }

                            if (endsWithTarget) {
                                std::filesystem::path contentPath = basePath / uriStr;
                                try {
                                    uris.insert(std::filesystem::weakly_canonical(contentPath));
                                }
                                catch (const std::filesystem::filesystem_error& fs_err) {
                                    logError("Filesystem error when processing path " + contentPath.string() + ": " + fs_err.what());
                                    uris.insert(contentPath); // Insert non-canonicalized as fallback
                                }
                            }
                        }
                    }
                }
                else { // Recursive call for nested objects/arrays
                    findContentUrisRecursiveHelper(it.value(), basePath, uris);
                }
            }
        }
        else if (j.is_array()) {
            for (const auto& item : j) {
                findContentUrisRecursiveHelper(item, basePath, uris);
            }
        }
    }

    std::vector<std::filesystem::path> GlbReader::extractGlbPathsFromTileset(
        const std::filesystem::path& tilesetPath) {
        logMessage("Extracting GLB paths from tileset: " + tilesetPath.string());
        std::set<std::filesystem::path> uniqueGlbPaths;

        auto tilesetBytes = readFileBytes(tilesetPath);
        if (!tilesetBytes) {
            logError("Could not read tileset file: " + tilesetPath.string());
            return {};
        }

        try {
            nlohmann::json tilesetJson = nlohmann::json::parse(
                reinterpret_cast<const char*>(tilesetBytes->data()),
                reinterpret_cast<const char*>(tilesetBytes->data() + tilesetBytes->size())
            );

            std::filesystem::path tilesetDir = tilesetPath.parent_path();
            findContentUrisRecursiveHelper(tilesetJson, tilesetDir, uniqueGlbPaths); // Call the helper

        }
        catch (const nlohmann::json::parse_error& e) {
            logError("Failed to parse tileset JSON: " + tilesetPath.string() + " - " + e.what());
            return {};
        }
        catch (const std::exception& e) {
            logError("Error processing tileset: " + tilesetPath.string() + " - " + e.what());
            return {};
        }

        std::vector<std::filesystem::path> resultPaths(uniqueGlbPaths.begin(), uniqueGlbPaths.end());
        if (!resultPaths.empty()) {
            logMessage("From tileset " + tilesetPath.string() + ", found GLB references:");
            for (const auto& p : resultPaths) {
                logMessage("  - " + p.string());
            }
        }
        return resultPaths;
    }

    std::set<std::filesystem::path> GlbReader::discoverGlbFiles(
        const std::filesystem::path& directoryPath,
        bool recursive) {
        logMessage("Discovering GLB files in directory: " + directoryPath.string() + (recursive ? " (recursive)" : " (non-recursive)"));
        std::set<std::filesystem::path> glbFilePaths;

        if (!std::filesystem::exists(directoryPath) || !std::filesystem::is_directory(directoryPath)) {
            logError("Provided path is not a valid directory: " + directoryPath.string());
            return glbFilePaths;
        }

        // Use a lambda for processing directory entries to avoid code duplication
        auto process_entry = [&](const std::filesystem::path& current_path) {
            if (std::filesystem::is_regular_file(current_path)) {
                std::string extension = current_path.extension().string();
                // Convert extension to lowercase for case-insensitive comparison
                std::transform(extension.begin(), extension.end(), extension.begin(),
                    [](unsigned char c) { return static_cast<char>(::tolower(c)); });

                if (extension == ".glb") {
                    try {
                        glbFilePaths.insert(std::filesystem::weakly_canonical(current_path));
                    }
                    catch (const std::filesystem::filesystem_error& fs_err) {
                        logError("Filesystem error when processing GLB path " + current_path.string() + ": " + fs_err.what());
                        glbFilePaths.insert(current_path); // Insert non-canonicalized
                    }
                }
                else if (extension == ".json") {
                    std::string filename = current_path.filename().string();
                    if (filename.find("tileset.json") != std::string::npos || filename == "tileset.json") {
                        std::vector<std::filesystem::path> pathsFromTileset = extractGlbPathsFromTileset(current_path);
                        for (const auto& p : pathsFromTileset) {
                            if (std::filesystem::exists(p)) {
                                try {
                                    glbFilePaths.insert(std::filesystem::weakly_canonical(p));
                                }
                                catch (const std::filesystem::filesystem_error& fs_err) {
                                    logError("Filesystem error when processing path from tileset " + p.string() + ": " + fs_err.what());
                                    glbFilePaths.insert(p); // Insert non-canonicalized
                                }
                            }
                            else {
                                logMessage("  Warning: GLB referenced in " + current_path.string() + " not found: " + p.string());
                            }
                        }
                    }
                }
            }
        };

        if (recursive) {
            try {
                for (const auto& dir_entry : std::filesystem::recursive_directory_iterator(directoryPath, std::filesystem::directory_options::skip_permission_denied)) {
                    process_entry(dir_entry.path());
                }
            }
            catch (const std::filesystem::filesystem_error& e) {
                logError("Error iterating directory recursively " + directoryPath.string() + ": " + e.what());
            }
        }
        else {
            try {
                for (const auto& dir_entry : std::filesystem::directory_iterator(directoryPath, std::filesystem::directory_options::skip_permission_denied)) {
                    process_entry(dir_entry.path());
                }
            }
            catch (const std::filesystem::filesystem_error& e) {
                logError("Error iterating directory " + directoryPath.string() + ": " + e.what());
            }
        }

        logMessage("Discovered " + std::to_string(glbFilePaths.size()) + " unique GLB files.");
        return glbFilePaths;
    }

    std::vector<LoadedGltfModel> GlbReader::loadGltfModels(
        const std::set<std::filesystem::path>& glbPaths) {
        std::vector<LoadedGltfModel> loadedModels;
        loadedModels.reserve(glbPaths.size());
        int currentModelId = 0;
        for (const auto& path : glbPaths) {
            if (std::filesystem::exists(path)) { // Double check existence before reading
                auto loadedModelOpt = readGlb(path, currentModelId);
                if (loadedModelOpt) {
                    loadedModels.push_back(std::move(*loadedModelOpt));
                    currentModelId++;
                }
            }
            else {
                logError("GLB file path does not exist (or no permission), skipping: " + path.string());
            }
        }
        return loadedModels;
    }

} // namespace GltfInstancing