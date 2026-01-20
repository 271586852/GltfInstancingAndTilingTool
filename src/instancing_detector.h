#ifndef INSTANCING_DETECTOR_H
#define INSTANCING_DETECTOR_H

#include "utilities.h"     // For MeshInstanceInfo, InstancedMeshGroup, NonInstancedMeshInfo, etc.
#include "glb_reader.h"    // For LoadedGltfModel
#include <vector>
#include <map>
#include <string>
#include <functional> // For std::hash
#include <set> // Required for std::set
#include <glm/gtc/quaternion.hpp>

#include <CesiumGltf/Model.h>
#include <CesiumGltf/Mesh.h>
#include <CesiumGltf/Node.h>
#include <CesiumGltf/Scene.h>

namespace GltfInstancing {

    // Forward declaration
    class GlbReader;

    // Structure to hold the results of the detection process
    struct InstancingDetectionResult {
        std::vector<InstancedMeshGroup> instancedGroups;
        std::vector<NonInstancedMeshInfo> nonInstancedMeshes;
        // Potentially, a mapping from original (modelId, meshId) to new representative mesh or group
    };

    class InstancingDetector {
    public:
        // Constructor now accepts a set of attribute names to skip data hashing in tolerance mode
        // and a specific tolerance for normal attributes.
        InstancingDetector(double tolerance, 
                           const std::set<std::string>& skipAttributes = {},
                           double normalSpecificTolerance = 0.0,
                           int instanceLimit = 0);

        // Main function to detect instancing opportunities
        InstancingDetectionResult detect(const std::vector<LoadedGltfModel>& loadedModels);

    private:
        double geometryTolerance;
        // Set of attribute semantic names whose data should NOT be hashed when geometryTolerance > 0
        // POSITION is implicitly always skipped in tolerance mode regarding data hashing.
        const std::set<std::string> attributesToSkipDataHashInToleranceMode;
        double normalTolerance; // Tolerance for comparing NORMAL attributes, if geometryTolerance > 0 and NORMAL is not skipped
        int _instanceLimit;

        // Calculates a signature for a glTF mesh primitive based on its geometry and material.
        // This signature is used to determine if two primitives are identical.
        size_t calculatePrimitiveSignature(
            const CesiumGltf::Model& model,
            const CesiumGltf::MeshPrimitive& primitive,
            bool logDetailsForThisPrimitive,
            const std::string& meshName
        );

        // Calculates an exact signature for a glTF mesh primitive (used when tolerance is zero)
        size_t calculatePrimitiveSignatureExact(
            const CesiumGltf::Model& model,
            const CesiumGltf::MeshPrimitive& primitive,
            bool logDetailsForThisPrimitive,
            const std::string& meshName
        );

        // Calculates a signature for a glTF mesh based on the signatures of its primitives.
        size_t calculateMeshSignature(
            const CesiumGltf::Model& model,
            const CesiumGltf::Mesh& mesh,
            const std::string& meshName);

        // Traverses the scene graph to collect mesh instances and their transforms
        void traverseNode(
            const LoadedGltfModel& loadedGltf,
            int32_t nodeIndex,
            const glm::dmat4& parentTransform,
            std::map<size_t, InstancedMeshGroup>& potentialInstanceGroups,
            std::vector<NonInstancedMeshInfo>& nonInstancedItems,
            std::map<std::pair<int, int>, size_t>& meshToSignatureCache, // Cache for (modelId, meshIdx) -> signature
            std::vector<int32_t>& parentNodeIndicesChain // For getNodeWorldTransform
        );

        // Helper to combine hashes (used for creating signatures)
        template <class T>
        inline void hash_combine(std::size_t& seed, const T& v) {
            std::hash<T> hasher;
            seed ^= hasher(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        }

    };

} // namespace GltfInstancing

#endif // INSTANCING_DETECTOR_H