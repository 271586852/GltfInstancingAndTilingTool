#include "material_matching.h"
#include <CesiumGltf/Model.h>
#include <CesiumGltf/Mesh.h>
#include <CesiumGltf/Material.h>
#include <sstream>
#include <functional>

namespace GltfInstancing {

    int32_t getMeshMaterialIndex(const CesiumGltf::Model& model, const CesiumGltf::Mesh& mesh) {
        if (mesh.primitives.empty()) return -1;
        const auto& prim = mesh.primitives[0];
        if (prim.material < 0 || static_cast<size_t>(prim.material) >= model.materials.size())
            return -1;
        return prim.material;
    }

    // Hash material properties only (no texture references)
    size_t getMeshMaterialHash(const CesiumGltf::Model& model, const CesiumGltf::Mesh& mesh) {
        if (mesh.primitives.empty()) return 0;
        const auto& prim = mesh.primitives[0];
        int32_t matIdx = prim.material;
        if (matIdx < 0 || static_cast<size_t>(matIdx) >= model.materials.size())
            return 0;

        std::ostringstream ss;
        const auto& mat = model.materials[matIdx];

        // PBR metallic-roughness (factors only, no textures)
        if (mat.pbrMetallicRoughness.has_value()) {
            const auto& pbr = mat.pbrMetallicRoughness.value();
            for (double v : pbr.baseColorFactor) ss << v << ",";
            ss << "m" << pbr.metallicFactor << "r" << pbr.roughnessFactor;
        } else {
            ss << "nopbr";
        }

        // Emissive factor, alphaMode, doubleSided
        for (double v : mat.emissiveFactor) ss << v << ",";
        ss << "a" << mat.alphaMode << "d" << (mat.doubleSided ? 1 : 0);

        std::string s = ss.str();
        return std::hash<std::string>{}(s);
    }

} // namespace GltfInstancing
