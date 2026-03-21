#include "hausdorff_similarity.h"
#include <CesiumGltf/Model.h>
#include <CesiumGltf/Mesh.h>
#include <CesiumGltf/AccessorView.h>
#include <glm/glm.hpp>
#include <nanoflann.hpp>
#include <algorithm>
#include <functional>
#include "svd3.h"
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
        void downsampleDeterministicInPlace(std::vector<glm::dvec3>& points, size_t maxPoints) {
            if (maxPoints == 0 || points.size() <= maxPoints) return;
            if (maxPoints == 1) {
                points = { points.front() };
                return;
            }

            std::vector<glm::dvec3> sampled;
            sampled.reserve(maxPoints);

            const double step = static_cast<double>(points.size() - 1) / static_cast<double>(maxPoints - 1);
            for (size_t i = 0; i < maxPoints; ++i) {
                const size_t idx = static_cast<size_t>(std::llround(static_cast<double>(i) * step));
                sampled.push_back(points[std::min(idx, points.size() - 1)]);
            }
            points.swap(sampled);
        }

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

        // ICP: align ptsB to ptsA (rigid: rotation + translation). Modifies ptsB in-place.
        // Both point clouds should already be normalized (centered, scaled).
        void icpAlignPointCloud(
            const std::vector<glm::dvec3>& ptsA,
            std::vector<glm::dvec3>& ptsB,
            int maxIterations = 20,
            double convergenceThreshold = 1e-6)
        {
            if (ptsA.empty() || ptsB.empty()) return;

            PointCloudAdaptor adaptA;
            adaptA.pts = &ptsA;

            for (int iter = 0; iter < maxIterations; ++iter) {
                KDTree treeA(3, adaptA, nanoflann::KDTreeSingleIndexAdaptorParams(10));
                treeA.buildIndex();

                // Find correspondences: for each b_i, nearest in A
                glm::dvec3 meanA(0), meanB(0);
                std::vector<glm::dvec3> corrA(ptsB.size());
                std::vector<size_t> idx(1);
                std::vector<double> distSq(1);

                for (size_t i = 0; i < ptsB.size(); ++i) {
                    const double query[3] = { ptsB[i].x, ptsB[i].y, ptsB[i].z };
                    nanoflann::KNNResultSet<double> resultSet(1);
                    resultSet.init(&idx[0], &distSq[0]);
                    treeA.findNeighbors(resultSet, query, nanoflann::SearchParameters());
                    corrA[i] = ptsA[idx[0]];
                    meanA += corrA[i];
                    meanB += ptsB[i];
                }
                meanA /= static_cast<double>(ptsB.size());
                meanB /= static_cast<double>(ptsB.size());

                // H = sum (b_i - meanB) * (a_i - meanA)^T
                float h11 = 0, h12 = 0, h13 = 0, h21 = 0, h22 = 0, h23 = 0, h31 = 0, h32 = 0, h33 = 0;
                for (size_t i = 0; i < ptsB.size(); ++i) {
                    glm::dvec3 db = ptsB[i] - meanB;
                    glm::dvec3 da = corrA[i] - meanA;
                    h11 += static_cast<float>(db.x * da.x); h12 += static_cast<float>(db.x * da.y); h13 += static_cast<float>(db.x * da.z);
                    h21 += static_cast<float>(db.y * da.x); h22 += static_cast<float>(db.y * da.y); h23 += static_cast<float>(db.y * da.z);
                    h31 += static_cast<float>(db.z * da.x); h32 += static_cast<float>(db.z * da.y); h33 += static_cast<float>(db.z * da.z);
                }

                float u11, u12, u13, u21, u22, u23, u31, u32, u33;
                float s11, s12, s13, s21, s22, s23, s31, s32, s33;
                float v11, v12, v13, v21, v22, v23, v31, v32, v33;
                svd3_impl::svd(h11, h12, h13, h21, h22, h23, h31, h32, h33,
                    u11, u12, u13, u21, u22, u23, u31, u32, u33,
                    s11, s12, s13, s21, s22, s23, s31, s32, s33,
                    v11, v12, v13, v21, v22, v23, v31, v32, v33);

                // R = V * U^T (rotation from B to A frame)
                float r11 = v11 * u11 + v12 * u21 + v13 * u31;
                float r12 = v11 * u12 + v12 * u22 + v13 * u32;
                float r13 = v11 * u13 + v12 * u23 + v13 * u33;
                float r21 = v21 * u11 + v22 * u21 + v23 * u31;
                float r22 = v21 * u12 + v22 * u22 + v23 * u32;
                float r23 = v21 * u13 + v22 * u23 + v23 * u33;
                float r31 = v31 * u11 + v32 * u21 + v33 * u31;
                float r32 = v31 * u12 + v32 * u22 + v33 * u32;
                float r33 = v31 * u13 + v32 * u23 + v33 * u33;

                // Ensure proper rotation (det=1)
                float det = r11 * (r22 * r33 - r23 * r32) - r12 * (r21 * r33 - r23 * r31) + r13 * (r21 * r32 - r22 * r31);
                if (det < 0) {
                    r31 = -r31; r32 = -r32; r33 = -r33;
                }

                glm::dvec3 t = meanA - glm::dvec3(
                    r11 * meanB.x + r12 * meanB.y + r13 * meanB.z,
                    r21 * meanB.x + r22 * meanB.y + r23 * meanB.z,
                    r31 * meanB.x + r32 * meanB.y + r33 * meanB.z);

                double maxDelta = 0;
                for (size_t i = 0; i < ptsB.size(); ++i) {
                    glm::dvec3 oldP = ptsB[i];
                    ptsB[i] = glm::dvec3(
                        r11 * oldP.x + r12 * oldP.y + r13 * oldP.z + t.x,
                        r21 * oldP.x + r22 * oldP.y + r23 * oldP.z + t.y,
                        r31 * oldP.x + r32 * oldP.y + r33 * oldP.z + t.z);
                    double d = glm::length(ptsB[i] - oldP);
                    if (d > maxDelta) maxDelta = d;
                }
                if (maxDelta < convergenceThreshold) break;
            }
        }

        // Returns sorted extents (descending) from normalized point cloud. Returns empty if degenerate.
        void getSortedExtents(const std::vector<glm::dvec3>& points, double outExtents[3]) {
            if (points.empty()) { outExtents[0] = outExtents[1] = outExtents[2] = 0; return; }
            glm::dvec3 minP(std::numeric_limits<double>::max());
            glm::dvec3 maxP(std::numeric_limits<double>::lowest());
            for (const auto& p : points) {
                minP = glm::min(minP, p);
                maxP = glm::max(maxP, p);
            }
            glm::dvec3 ext = maxP - minP;
            double arr[3] = { ext.x, ext.y, ext.z };
            std::sort(arr, arr + 3, std::greater<double>());
            outExtents[0] = arr[0]; outExtents[1] = arr[1]; outExtents[2] = arr[2];
        }

        // AABB coarse filter: reject if sorted extents differ by more than tolerance. Returns true to reject (not similar).
        bool aabbCoarseFilterReject(
            const std::vector<glm::dvec3>& ptsA,
            const std::vector<glm::dvec3>& ptsB,
            double tolerance)
        {
            double extA[3], extB[3];
            getSortedExtents(ptsA, extA);
            getSortedExtents(ptsB, extB);
            double maxExt = std::max({ extA[0], extB[0], 1e-9 });
            if (maxExt < 1e-9) return false;  // degenerate, skip filter
            for (int i = 0; i < 3; ++i) {
                if (std::abs(extA[i] - extB[i]) > tolerance) return true;
            }
            return false;
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
        const CesiumGltf::Mesh& meshB,
        size_t maxSamplePoints,
        bool enableIcpAlignment,
        bool enableAabbCoarseFilter,
        double aabbAspectRatioTolerance)
    {
        std::vector<glm::dvec3> ptsA = extractMeshPositions(modelA, meshA);
        std::vector<glm::dvec3> ptsB = extractMeshPositions(modelB, meshB);
        if (ptsA.empty() || ptsB.empty()) return -1.0;

        downsampleDeterministicInPlace(ptsA, maxSamplePoints);
        downsampleDeterministicInPlace(ptsB, maxSamplePoints);

        normalizePointCloud(ptsA);
        normalizePointCloud(ptsB);

        if (enableAabbCoarseFilter && aabbCoarseFilterReject(ptsA, ptsB, aabbAspectRatioTolerance))
            return -1.0;

        if (enableIcpAlignment) {
            icpAlignPointCloud(ptsA, ptsB);
        }

        double dist = computeHausdorffDistance(ptsA, ptsB);
        if (dist < 0) return -1.0;
        return hausdorffDistanceToSimilarity(dist);
    }

} // namespace GltfInstancing
