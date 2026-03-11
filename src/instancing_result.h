#ifndef INSTANCING_RESULT_H
#define INSTANCING_RESULT_H

#include "utilities.h" // InstancedMeshGroup, NonInstancedMeshInfo
#include <vector>
#include <string>

namespace GltfInstancing {

    // Structure to hold the results of the detection process
    struct InstancingDetectionResult {
        std::vector<InstancedMeshGroup> instancedGroups;
        std::vector<NonInstancedMeshInfo> nonInstancedMeshes;

        // Generates a summary report string
        std::string getReport() const;
    };

} // namespace GltfInstancing

#endif // INSTANCING_RESULT_H

