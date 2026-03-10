#include "hausdorff_similarity.h"
#include <CesiumGltf/Model.h>
#include <CesiumGltf/Mesh.h>
#include <CesiumGltf/AccessorView.h>
#include <glm/glm.hpp>
#include <algorithm>
#include <limits>
#include <cmath>

namespace GltfInstancing {

    std::vector<glm::dvec3> extractMeshPositions(
        const CesiumGltf::Model& model,
        const CesiumGltf::Mesh& mesh)
    {
        std::vector<glm::dvec3> positions;
        for (const auto& prim : mesh.primitives) {
            auto it = prim.attributes.find("POSITION");
            if (it == prim.attributes.end()) continue;
            int32_t accIdx = it->second;
            if (accIdx < 0 || static_cast<size_t>(accIdx) >= model.accessors.size()) continue;

            CesiumGltf::AccessorView<glm::vec3> view(model, accIdx);
            if (view.status() != CesiumGltf::AccessorViewStatus::Valid) continue;

            for (int64_t i = 0; i < view.size(); ++i) {
                glm::vec3 v = view[i];
                positions.push_back(glm::dvec3(v.x, v.y, v.z));
            }
        }
        return positions;
    }

    void normalizePointCloud(std::vector<glm::dvec3>& points) {
        if (points.empty()) return;

        glm::dvec3 minP(std::numeric_limits<double>::max());
        glm::dvec3 maxP(std::numeric_limits<double>::lowest());
        for (const auto& p : points) {
            minP = glm::min(minP, p);
            maxP = glm::max(maxP, p);
        }
        glm::dvec3 center = (minP + maxP) * 0.5;
        glm::dvec3 extents = maxP - minP;
        double maxExt = std::max({ extents.x, extents.y, extents.z });
        if (maxExt < 1e-12) maxExt = 1.0;

        for (auto& p : points) {
            p = (p - center) / maxExt;
        }
    }

    double computeHausdorffDistance(
        const std::vector<glm::dvec3>& pointsA,
        const std::vector<glm::dvec3>& pointsB)
    {
        if (pointsA.empty() || pointsB.empty()) return -1.0;

        auto directedDistance = [](const std::vector<glm::dvec3>& from, const std::vector<glm::dvec3>& to) -> double {
            double maxMinDist = 0.0;
            for (const auto& a : from) {
                double minDist = std::numeric_limits<double>::max();
                for (const auto& b : to) {
                    double d = glm::distance(a, b);
                    if (d < minDist) minDist = d;
                }
                if (minDist > maxMinDist) maxMinDist = minDist;
            }
            return maxMinDist;
        };

        double hAB = directedDistance(pointsA, pointsB);
        double hBA = directedDistance(pointsB, pointsA);
        return std::max(hAB, hBA);
    }

    double hausdorffDistanceToSimilarity(double hausdorffDistance) {
        if (hausdorffDistance < 0) return -1.0;
        return 1.0 / (1.0 + hausdorffDistance);
    }

    double computeMeshSimilarity(
        const CesiumGltf::Model& modelA,
        const CesiumGltf::Mesh& meshA,
        const CesiumGltf::Model& modelB,
        const CesiumGltf::Mesh& meshB)
    {
        std::vector<glm::dvec3> ptsA = extractMeshPositions(modelA, meshA);
        std::vector<glm::dvec3> ptsB = extractMeshPositions(modelB, meshB);
        if (ptsA.empty() || ptsB.empty()) return -1.0;

        normalizePointCloud(ptsA);
        normalizePointCloud(ptsB);

        double dist = computeHausdorffDistance(ptsA, ptsB);
        if (dist < 0) return -1.0;
        return hausdorffDistanceToSimilarity(dist);
    }

} // namespace GltfInstancing
