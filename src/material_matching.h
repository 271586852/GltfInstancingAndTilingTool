#ifndef MATERIAL_MATCHING_H
#define MATERIAL_MATCHING_H

#include <cstdint>
#include <cstddef>

namespace CesiumGltf {
    struct Model;
    struct Mesh;
}

namespace GltfInstancing {

    // Get the material index of the first primitive of a mesh. Returns -1 if no valid material.
    int32_t getMeshMaterialIndex(const CesiumGltf::Model& model, const CesiumGltf::Mesh& mesh);

    // Compute a content-based hash of the mesh's material (first primitive) including
    // PBR factors, emissiveFactor, alphaMode, doubleSided. No texture references.
    // Used for material_filter_mode = "hash".
    size_t getMeshMaterialHash(const CesiumGltf::Model& model, const CesiumGltf::Mesh& mesh);

} // namespace GltfInstancing

#endif // MATERIAL_MATCHING_H
