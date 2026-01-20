#ifndef GLB_READER_H
#define GLB_READER_H

#include "utilities.h" 
#include <vector>
#include <string>
#include <filesystem>
#include <optional>
#include <set>

#include <CesiumGltf/Model.h>
#include <CesiumGltfReader/GltfReader.h>
#include <CesiumJsonReader/JsonReader.h>

namespace GltfInstancing {

   
    struct LoadedGltfModel {
        CesiumGltf::Model model;
        std::filesystem::path originalPath;
        std::string fileHash; // SHA256 hash of the original file for quick identity check
        int uniqueId; // A unique ID assigned to this loaded model for easy reference

        // Default constructor for invalid state
        LoadedGltfModel() : uniqueId(-1) {}
    };

    class GlbReader {
    public:
        GlbReader();

        std::optional<LoadedGltfModel> readGlb(const std::filesystem::path& glbPath, int modelId);

        std::vector<std::filesystem::path> extractGlbPathsFromTileset(
            const std::filesystem::path& tilesetPath);

        std::set<std::filesystem::path> discoverGlbFiles(
            const std::filesystem::path& directoryPath,
            bool recursive = true);

        std::vector<LoadedGltfModel> loadGltfModels(
            const std::set<std::filesystem::path>& glbPaths);

    private:
        CesiumGltfReader::GltfReader _gltfReader;
    };

} 

#endif 