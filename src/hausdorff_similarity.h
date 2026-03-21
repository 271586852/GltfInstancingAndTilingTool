#ifndef HAUSDORFF_SIMILARITY_H
#define HAUSDORFF_SIMILARITY_H

#include <vector>
#include <glm/glm.hpp>

#include <CesiumGltf/Model.h>
#include <CesiumGltf/Mesh.h>

namespace GltfInstancing {

    // Extracts vertex positions from a glTF mesh (all primitives, POSITION attribute).
    // Returns empty vector if mesh has no valid positions.
    std::vector<glm::dvec3> extractMeshPositions(
        const CesiumGltf::Model& model,
        const CesiumGltf::Mesh& mesh);

    // Normalizes point cloud: translate to center at origin, scale so max extent = 1.
    void normalizePointCloud(std::vector<glm::dvec3>& points);

    // Computes symmetric Hausdorff distance between two point sets.
    // Returns positive distance, or -1.0 if either set is empty.
    double computeHausdorffDistance(
        const std::vector<glm::dvec3>& pointsA,
        const std::vector<glm::dvec3>& pointsB);

    // Converts Hausdorff distance to similarity score in [0, 1].
    // similarity = 1 / (1 + distance); distance=0 -> 1, distance=1 -> 0.5, etc.
    double hausdorffDistanceToSimilarity(double hausdorffDistance);

    // Full pipeline: extract, normalize, [optional AABB coarse filter], [optional ICP align], compute Hausdorff, return similarity.
    // Returns similarity in [0, 1], or -1.0 if either mesh has no valid positions.
    // enableIcpAlignment: if true, run ICP to align ptsB to ptsA before Hausdorff (handles mesh local rotation).
    // enableAabbCoarseFilter: if true, reject pairs whose normalized AABB aspect ratios differ by more than tolerance.
    // aabbAspectRatioTolerance: max allowed |eA[i]-eB[i]| for sorted extents; used only when enableAabbCoarseFilter is true.
    double computeMeshSimilarity(
        const CesiumGltf::Model& modelA,
        const CesiumGltf::Mesh& meshA,
        const CesiumGltf::Model& modelB,
        const CesiumGltf::Mesh& meshB,
        size_t maxSamplePoints = 0,
        bool enableIcpAlignment = false,
        bool enableAabbCoarseFilter = false,
        double aabbAspectRatioTolerance = 0.25);

} // namespace GltfInstancing

#endif // HAUSDORFF_SIMILARITY_H
