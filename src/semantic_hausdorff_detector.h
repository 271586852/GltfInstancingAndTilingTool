#ifndef SEMANTIC_HAUSDORFF_DETECTOR_H
#define SEMANTIC_HAUSDORFF_DETECTOR_H

#include "utilities.h"
#include "glb_reader.h"
#include "instancing_result.h"  // InstancingDetectionResult
#include "semantic_parser.h"
#include "hausdorff_similarity.h"
#include <vector>
#include <map>
#include <string>
#include <functional> // std::hash
#include <set>

namespace GltfInstancing {

    // Instancing detector using semantic hash + Hausdorff similarity.
    // Isolated from legacy InstancingDetector; used when instancing_detection_mode = "semantic_hausdorff".
    class SemanticHausdorffInstancingDetector {
    public:
        // semanticHashFields: comma-separated, e.g. "category,family,type"
        // similarityThreshold: threshold for Hausdorff similarity (0..1)
        // instanceLimit: minimum instances to form a group
        SemanticHausdorffInstancingDetector(
            const SemanticParser* semanticParser,
            const std::string& semanticHashFields,
            double similarityThreshold,
            int instanceLimit,
            size_t hausdorffMaxSamplePoints = 2000,
            bool allowUnknownCrossMeshClustering = false);

        InstancingDetectionResult detect(const std::vector<LoadedGltfModel>& loadedModels);

    private:
        const SemanticParser* _semanticParser;
        std::vector<std::string> _semanticHashFieldNames;  // e.g. ["category","family","type"]
        double _similarityThreshold;
        int _instanceLimit;
        size_t _hausdorffMaxSamplePoints;
        bool _allowUnknownCrossMeshClustering;

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

#endif // SEMANTIC_HAUSDORFF_DETECTOR_H
