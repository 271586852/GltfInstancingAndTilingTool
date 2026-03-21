#ifndef SEMANTIC_MATERIAL_GEOMETRIC_DETECTOR_H
#define SEMANTIC_MATERIAL_GEOMETRIC_DETECTOR_H

#include "utilities.h"
#include "glb_reader.h"
#include "instancing_result.h"  // InstancingDetectionResult
#include "semantic_parser.h"
#include "hausdorff_similarity.h"
#include "material_matching.h"
#include <vector>
#include <map>
#include <string>
#include <functional> // std::hash
#include <set>

namespace GltfInstancing {

    // Instancing detector: semantic grouping + optional material filter + geometric (Hausdorff) similarity.
    class SemanticMaterialGeometricDetector {
    public:
        // semanticHashFields: comma-separated, e.g. "category,family,type"
        // similarityThreshold: threshold for Hausdorff similarity (0..1)
        // instanceLimit: minimum instances to form a group
        // materialFilterMode: "none", "hash", or "index" - filter Hausdorff comparison by material
        SemanticMaterialGeometricDetector(
            const SemanticParser* semanticParser,
            const std::string& semanticHashFields,
            double similarityThreshold,
            int instanceLimit,
            size_t hausdorffMaxSamplePoints = 2000,
            bool allowUnknownCrossMeshClustering = false,
            const std::string& materialFilterMode = "none",
            bool enableIcpAlignment = false);

        InstancingDetectionResult detect(const std::vector<LoadedGltfModel>& loadedModels);

    private:
        const SemanticParser* _semanticParser;
        std::vector<std::string> _semanticHashFieldNames;  // e.g. ["category","family","type"]
        double _similarityThreshold;
        int _instanceLimit;
        size_t _hausdorffMaxSamplePoints;
        bool _allowUnknownCrossMeshClustering;
        std::string _materialFilterMode;
        bool _enableIcpAlignment;

        std::string buildSemanticHashKey(const std::optional<SemanticInfo>& info) const;
        void traverseNode(
            const LoadedGltfModel& loadedGltf,
            int32_t modelIndexInLoadedModels,
            int32_t nodeIndex,
            const glm::dmat4& parentTransform,
            std::map<std::string, std::vector<std::pair<std::pair<int32_t, int32_t>, MeshInstanceInfo>>>& semanticGroups,
            std::vector<int32_t>& parentNodeIndicesChain);
    };

} // namespace GltfInstancing

#endif // SEMANTIC_MATERIAL_GEOMETRIC_DETECTOR_H
