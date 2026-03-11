#include "instancing_result.h"
#include <sstream>

namespace GltfInstancing {

    std::string InstancingDetectionResult::getReport() const {
        std::stringstream ss;
        ss << "Instancing Detection Report\n";
        ss << "===========================\n";
        ss << "Instanced Groups Found: " << instancedGroups.size() << "\n";
        ss << "Non-Instanced Meshes: " << nonInstancedMeshes.size() << "\n\n";

        ss << "--- Instanced Groups Details ---\n";
        for (const auto& group : instancedGroups) {
            ss << "Group Signature: " << group.meshSignature << "\n";
            ss << "  Representative Mesh Name: " << group.representativeMeshName << "\n";
            ss << "  Instance Count: " << group.instances.size() << "\n";
            ss << "  Representative Model ID: " << group.representativeGltfModelIndex << "\n";
            ss << "  Representative Mesh Index: " << group.representativeMeshIndexInModel << "\n";
            ss << "\n";
        }

        ss << "--- Non-Instanced Meshes Details (First 50) ---\n";
        int count = 0;
        for (const auto& mesh : nonInstancedMeshes) {
            if (count >= 50) {
                ss << "... and " << (nonInstancedMeshes.size() - 50) << " more.\n";
                break;
            }
            ss << "Mesh (Model: " << mesh.originalGltfModelIndex
               << ", MeshIdx: " << mesh.originalMeshIndexInModel
               << ", NodeIdx: " << mesh.originalNodeIndexInModel << ")\n";
            count++;
        }

        return ss.str();
    }

} // namespace GltfInstancing

