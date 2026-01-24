#ifndef MESH_SIMPLIFIER_H
#define MESH_SIMPLIFIER_H

#include <CesiumGltf/Model.h>
#include <filesystem>
#include <vector>
#include <string>

namespace NonInstancingLOD {

    class MeshSimplifier {
    public:
        /**
         * @brief Generates a chain of LODs for a given GLB file using mesh simplification.
         * 
         * @param inputPath Path to the source GLB file.
         * @param outputDir Directory where LOD files will be saved.
         * @param levels Number of LOD levels to generate (e.g., 3).
         * @param ratio Simplification ratio per level (0.0 - 1.0, e.g., 0.5).
         * @param tilesetName Name of the generated tileset JSON file.
         */
        static void generateNonInstancingLodChain(
            const std::filesystem::path& inputPath,
            const std::filesystem::path& outputDir,
            int levels,
            float ratio,
            size_t minSimplifyIndexCount,
            const std::string& tilesetName = "tileset_lod.json"
        );

    private:
        /**
         * @brief Creates a simplified version of the model.
         * 
         * @param source The original model.
         * @param targetRatio The target ratio of triangles to keep (0.0 - 1.0).
         * @return A new CesiumGltf::Model with simplified meshes.
         */
        static CesiumGltf::Model simplifyModel(
            const CesiumGltf::Model& source,
            float targetRatio,
            size_t minSimplifyIndexCount
        );
    };

} // namespace NonInstancingLOD

#endif // MESH_SIMPLIFIER_H

