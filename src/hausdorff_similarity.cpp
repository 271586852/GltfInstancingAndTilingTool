#include "hausdorff_similarity.h"
#include <CesiumGltf/Model.h>
#include <CesiumGltf/Mesh.h>
#include <CesiumGltf/AccessorView.h>
#include <glm/glm.hpp>
#include <nanoflann.hpp>
#include <algorithm>
#include <limits>
#include <cmath>
#include <cstddef>

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

    namespace {
        // Adaptor for nanoflann: wrap std::vector<glm::dvec3> for KD-Tree.
        struct PointCloudAdaptor {
            const std::vector<glm::dvec3>* pts = nullptr;
            inline size_t kdtree_get_point_count() const { return pts ? pts->size() : 0; }
            inline double kdtree_get_pt(const size_t idx, const size_t dim) const {
                const auto& p = (*pts)[idx];
                if (dim == 0) return p.x;
                if (dim == 1) return p.y;
                return p.z;
            }
            template <class BBOX>
            bool kdtree_get_bbox(BBOX&) const { return false; }
        };

        using KDTree = nanoflann::KDTreeSingleIndexAdaptor<
            nanoflann::L2_Adaptor<double, PointCloudAdaptor>,
            PointCloudAdaptor, 3, size_t>;

        // Directed Hausdorff: max over a in From of (min distance from a to any point in To).
        // Uses KD-Tree on To for O(|From| * log |To|) instead of O(|From| * |To|).
        double directedDistanceKdtree(
            const std::vector<glm::dvec3>& from,
            const std::vector<glm::dvec3>& to)
        {
            if (to.empty()) return 0.0;
            PointCloudAdaptor adaptTo;
            adaptTo.pts = &to;
            KDTree tree(3, adaptTo, nanoflann::KDTreeSingleIndexAdaptorParams(10));
            tree.buildIndex();

            double maxMinDist = 0.0;
            std::vector<size_t> idx(1);
            std::vector<double> distSq(1);
            for (const auto& a : from) {
                const double query[3] = { a.x, a.y, a.z };
                nanoflann::KNNResultSet<double> resultSet(1);
                resultSet.init(&idx[0], &distSq[0]);
                tree.findNeighbors(resultSet, query, nanoflann::SearchParameters());
                double d = (distSq[0] > 0.0) ? std::sqrt(distSq[0]) : 0.0;
                if (d > maxMinDist) maxMinDist = d;
            }
            return maxMinDist;
        }
    }

    double computeHausdorffDistance(
        const std::vector<glm::dvec3>& pointsA,
        const std::vector<glm::dvec3>& pointsB)
    {
        if (pointsA.empty() || pointsB.empty()) return -1.0;

        double hAB = directedDistanceKdtree(pointsA, pointsB);
        double hBA = directedDistanceKdtree(pointsB, pointsA);
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
