#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/transform.hpp>
#include <glm/gtx/norm.hpp> // For glm::distance2 if needed, or use component-wise subtraction

#include "utilities.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip> // For std::setw, std::setfill with hashes
#include <functional> // For std::hash
#include <gsl/span>

// For SHA256, you might need a library or implement it.
// For simplicity, this example will use a placeholder for SHA256.
// In a real project, use OpenSSL, Crypto++, or a similar library.
// As Cesium Native itself uses MbedTLS for some crypto, one could potentially leverage that
// if its headers/libs are exposed, but that adds complexity.
// For now, we'll fake it for demonstration.
#ifdef USE_OPENSSL_SHA256 // Example preprocessor directive
#include <openssl/sha.h>
#endif

#include <CesiumGltf/AccessorView.h>
#include <CesiumGltf/Model.h>
#include <CesiumGltf/BufferView.h>
#include <CesiumGltf/Buffer.h>


namespace GltfInstancing {

    // Initialize the global log level
    LogLevel g_currentLogLevel = LogLevel::LEVEL_INFO; // Default log level, Renamed from INFO

    void setLogLevel(LogLevel level) {
        g_currentLogLevel = level;
    }

    LogLevel getLogLevel() {
        return g_currentLogLevel;
    }

    std::string logLevelToString(LogLevel level) {
        switch (level) {
            case LogLevel::NONE:    return "NONE";
            case LogLevel::LEVEL_ERROR:   return "ERROR";     // Renamed from ERROR
            case LogLevel::LEVEL_WARNING: return "WARNING";   // Renamed from WARNING
            case LogLevel::LEVEL_INFO:    return "INFO";      // Renamed from INFO
            case LogLevel::LEVEL_DEBUG:   return "DEBUG";     // Renamed from DEBUG
            case LogLevel::LEVEL_VERBOSE: return "VERBOSE";   // Renamed from VERBOSE
            default:                return "UNKNOWN";
        }
    }

    void log(LogLevel level, const std::string& message) {
        if (level == LogLevel::NONE) {
            return; // Do not log anything if level is NONE
        }
        // For ERROR, always print. For other levels, print if they are <= g_currentLogLevel.
        if (level <= g_currentLogLevel) {
            std::string prefix = "[" + logLevelToString(level) + "] ";
            if (level == LogLevel::LEVEL_ERROR) { // Renamed from ERROR
                std::cerr << prefix << message << std::endl;
            } else {
                std::cout << prefix << message << std::endl;
            }
        }
    }

    // --- Basic Logging (Old - to be phased out or adapted) ---
    void logMessage(const std::string& message) {
        // std::cout << "[INFO] " << message << std::endl;
        log(LogLevel::LEVEL_INFO, message); // Forward to new system, Renamed from INFO
    }

    void logError(const std::string& errorMessage) {
        // std::cerr << "[ERROR] " << errorMessage << std::endl;
        log(LogLevel::LEVEL_ERROR, errorMessage); // Forward to new system, Renamed from ERROR
    }

    // Convenience logging functions
    void logWarning(const std::string& warningMessage) {
        log(LogLevel::LEVEL_WARNING, warningMessage); // Renamed from WARNING
    }

    void logInfo(const std::string& infoMessage) {
        log(LogLevel::LEVEL_INFO, infoMessage); // Renamed from INFO
    }

    void logDebug(const std::string& debugMessage) {
        log(LogLevel::LEVEL_DEBUG, debugMessage); // Renamed from DEBUG
    }

    void logVerbose(const std::string& verboseMessage) {
        log(LogLevel::LEVEL_VERBOSE, verboseMessage); // Renamed from VERBOSE
    }

    // --- File and Path Utilities ---
    std::optional<std::vector<char>> readFileBytes(const std::filesystem::path& filePath) {
        std::ifstream file(filePath, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            logError("Failed to open file: " + filePath.string());
            return std::nullopt;
        }

        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);

        std::vector<char> buffer(static_cast<size_t>(size));
        if (file.read(buffer.data(), size)) {
            return buffer;
        }
        else {
            logError("Failed to read file: " + filePath.string());
            return std::nullopt;
        }
    }

    std::string calculateFileSHA256(const std::filesystem::path& filePath) {
        // IMPORTANT: This is a placeholder. Replace with a real SHA256 implementation.
        // For a real implementation, you'd read the file and hash its contents.
        std::ifstream file(filePath, std::ios::binary);
        if (!file) {
            logError("calculateFileSHA256: Could not open file " + filePath.string());
            return "";
        }

#ifdef USE_OPENSSL_SHA256 // Example using OpenSSL (compile with -lcrypto)
        SHA256_CTX sha256;
        SHA256_Init(&sha256);
        char buffer[BUFSIZ];
        while (file.good()) {
            file.read(buffer, BUFSIZ);
            SHA256_Update(&sha256, buffer, file.gcount());
        }
        unsigned char hash[SHA256_DIGEST_LENGTH];
        SHA256_Final(hash, &sha256);

        std::stringstream ss;
        for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
            ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
        }
        return ss.str();

#else // Placeholder implementation
        // Using file size and name as a very, very crude pseudo-hash for demonstration.
        // DO NOT USE THIS IN PRODUCTION FOR ACTUAL HASHING.
        try {
            auto fileSize = std::filesystem::file_size(filePath);
            std::hash<std::string> str_hasher;
            size_t name_hash = str_hasher(filePath.filename().string());
            return "pseudo_sha256_" + std::to_string(fileSize) + "_" + std::to_string(name_hash);
        }
        catch (const std::filesystem::filesystem_error& e) {
            logError("calculateFileSHA256 (placeholder): " + std::string(e.what()));
            return "";
        }
#endif
    }


    // --- GLTF Specific Utilities ---

    glm::dmat4 TransformComponents::toMat4() const {
        glm::dmat4 m = glm::translate(glm::dmat4(1.0), translation);
        m *= glm::mat4_cast(rotation); // glm::dmat4_cast for dquat
        m = glm::scale(m, scale);
        return m;
    }

    TransformComponents TransformComponents::fromMat4(const glm::dmat4& matrix) {
        TransformComponents tc;
        glm::dvec3 skew;
        glm::dvec4 perspective;
        glm::decompose(matrix, tc.scale, tc.rotation, tc.translation, skew, perspective);
        // Ensure rotation quaternion is normalized
        tc.rotation = glm::normalize(tc.rotation);
        
        // 验证分解是否正确
        #ifdef DEBUG
        glm::dmat4 reconstructed = tc.toMat4();
        const double EPSILON = 1e-6;
        bool decompositionValid = true;
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                if (std::abs(matrix[i][j] - reconstructed[i][j]) > EPSILON) {
                    decompositionValid = false;
                    break;
                }
            }
        }
        if (!decompositionValid) {
            logError("Matrix decomposition may be inaccurate!");
            logMessage("Original matrix translation: " + 
                       std::to_string(matrix[3][0]) + ", " + 
                       std::to_string(matrix[3][1]) + ", " + 
                       std::to_string(matrix[3][2]));
            logMessage("Decomposed translation: " + 
                       std::to_string(tc.translation.x) + ", " + 
                       std::to_string(tc.translation.y) + ", " + 
                       std::to_string(tc.translation.z));
        }
        #endif
        
        return tc;
    }


  

    // Recursive helper for getNodeWorldTransform
    void calculateNodeHierarchyTransform(
        const CesiumGltf::Model& model,
        int32_t nodeIndex,
        const std::vector<int32_t>& parentNodeIndices,
        std::vector<bool>& visited,
        std::vector<glm::dmat4>& worldTransforms) {

        if (nodeIndex < 0 || static_cast<size_t>(nodeIndex) >= model.nodes.size()) {
            logError("Invalid node index in calculateNodeHierarchyTransform: " + std::to_string(nodeIndex));
            return;
        }

        if (visited[nodeIndex]) { // Already computed
            return;
        }

        const CesiumGltf::Node& node = model.nodes[nodeIndex];
        glm::dmat4 localTransform = getLocalTransformMatrix(node);
        glm::dmat4 parentWorldTransform(1.0);

        // Find parent node index:
        // This is tricky if the full parent path isn't provided.
        // We need to search through all nodes' children lists.
        // For simplicity, the public getNodeWorldTransform now asks for parentIndices path.

        if (!parentNodeIndices.empty()) {
            int32_t directParentIndex = parentNodeIndices.back();
            if (!visited[directParentIndex]) {
                // This should not happen if we traverse from root.
                // For robustness, could trigger calculation for parent.
                // calculateNodeHierarchyTransform(model, directParentIndex, /* parents of parent */, visited, worldTransforms);
            }
            parentWorldTransform = worldTransforms[directParentIndex];
        }

        worldTransforms[nodeIndex] = parentWorldTransform * localTransform;
        visited[nodeIndex] = true;

        // This version doesn't recurse downwards, it expects to be called for each node
        // or assumes parents are processed first.
    }


    // This function computes the world transform for a specific node by traversing up to the root.
    // This is less efficient if called many times, better to precompute all world transforms once.
    glm::dmat4 getNodeWorldTransform(
        const CesiumGltf::Model& model,
        int32_t targetNodeIndex,
        const std::vector<int32_t>& parentNodeIndicesChain // Path from a scene root to targetNodeIndex's parent
    ) {
        if (targetNodeIndex < 0 || static_cast<size_t>(targetNodeIndex) >= model.nodes.size()) {
            logError("Invalid targetNodeIndex in getNodeWorldTransform: " + std::to_string(targetNodeIndex));
            return glm::dmat4(1.0);
        }

        glm::dmat4 worldTransform(1.0);

        // Accumulate transforms from the root of the provided chain
        for (int32_t parentIdx : parentNodeIndicesChain) {
            if (parentIdx < 0 || static_cast<size_t>(parentIdx) >= model.nodes.size()) {
                logError("Invalid parent index in chain: " + std::to_string(parentIdx));
                continue; // Or handle error more strictly
            }
            worldTransform = worldTransform * getLocalTransformMatrix(model.nodes[parentIdx]);
        }

        // Add the target node's local transform
        worldTransform = worldTransform * getLocalTransformMatrix(model.nodes[targetNodeIndex]);

        return worldTransform;
    }

    // This version is hard to make efficient without full model traversal state.
    // It's better to pre-calculate all world transforms if needed, or use the index-based one with path.
    // For now, this is a placeholder or a simplified version assuming node is a root or we don't need parents.
    glm::dmat4 getNodeWorldTransform(
        const CesiumGltf::Model& /*model*/,
        const CesiumGltf::Node& node
    ) {
        // Simplified: returns only local transform.
        // A full implementation would require searching for this node in model.nodes,
        // then finding its parent, and so on, up to the root. This is inefficient.
        // Prefer using the index-based version with a known parent chain or pre-calculating all transforms.
        logMessage("Warning: getNodeWorldTransform(model, node) called, returns local transform. For world transform, use index-based version with parent path or precompute transforms.");
        return getLocalTransformMatrix(node);
    }

    glm::dmat4 getLocalTransformMatrix(const CesiumGltf::Node& node) {
        // Check if any TRS properties are present and non-default
        // Cesium loads empty TRS as empty vectors, default TRS as identity values.
        // A more robust check might be needed if "default" TRS values (e.g., scale [1,1,1]) 
        // should be differentiated from "not present".
        // For now, we assume if the vectors are not empty, they are user-specified.
        bool hasTranslation = !node.translation.empty();
        bool hasRotation = !node.rotation.empty();
        bool hasScale = !node.scale.empty();

        if (hasTranslation || hasRotation || hasScale) {
            glm::dvec3 translation(0.0, 0.0, 0.0);
            if (hasTranslation && node.translation.size() == 3) {
                translation = glm::dvec3(node.translation[0], node.translation[1], node.translation[2]);
            }

            glm::dquat rotation = glm::dquat(1.0, 0.0, 0.0, 0.0); // w, x, y, z
            if (hasRotation && node.rotation.size() == 4) {
                // glTF stores quaternion as [x, y, z, w]
                // glm::dquat constructor expects (w, x, y, z)
                rotation = glm::normalize(glm::dquat(node.rotation[3], node.rotation[0], node.rotation[1], node.rotation[2]));
            }

            glm::dvec3 scale(1.0, 1.0, 1.0);
            if (hasScale && node.scale.size() == 3) {
                scale = glm::dvec3(node.scale[0], node.scale[1], node.scale[2]);
            }

            glm::dmat4 translationMatrix = glm::translate(glm::dmat4(1.0), translation);
            glm::dmat4 rotationMatrix = glm::mat4_cast(rotation);
            glm::dmat4 scaleMatrix = glm::scale(glm::dmat4(1.0), scale);
            
            return translationMatrix * rotationMatrix * scaleMatrix;

        } else if (!node.matrix.empty()) {
            // Ensure matrix has 16 elements before accessing
            if (node.matrix.size() == 16) {
                const auto& m = node.matrix;
                // Assuming m is column-major as per glTF spec for matrices.
                // glm::dmat4 constructor from 16 doubles also expects column-major.
                return glm::dmat4(
                    m[0], m[1], m[2], m[3],    // Column 0
                    m[4], m[5], m[6], m[7],    // Column 1
                    m[8], m[9], m[10], m[11],  // Column 2
                    m[12], m[13], m[14], m[15]  // Column 3
                );
            } else {
                logError("Node matrix is present but not 16 elements long. Node name: " + node.name);
                return glm::dmat4(1.0); // Return identity if matrix is malformed
            }
        }
        
        // Default to identity matrix if no transform properties are found
        return glm::dmat4(1.0);
    }

    // Helper to compare raw buffer data
    bool compareBufferData(gsl::span<const std::byte> data1, gsl::span<const std::byte> data2) {
        if (data1.size() != data2.size()) {
            return false;
        }
        return std::memcmp(data1.data(), data2.data(), data1.size()) == 0;
    }


    bool compareAccessorData(
        const CesiumGltf::Model& model1, const CesiumGltf::Accessor& accessor1,
        const CesiumGltf::Model& model2, const CesiumGltf::Accessor& accessor2
    ) {
        if (accessor1.type != accessor2.type ||
            accessor1.componentType != accessor2.componentType ||
            accessor1.count != accessor2.count ||
            accessor1.normalized != accessor2.normalized) {
            return false;
        }

        CesiumGltf::AccessorView<std::byte> view1(model1, accessor1);
        CesiumGltf::AccessorView<std::byte> view2(model2, accessor2);

        if (view1.status() != CesiumGltf::AccessorViewStatus::Valid ||
            view2.status() != CesiumGltf::AccessorViewStatus::Valid) {
            return view1.status() == view2.status();
        }

        // For AccessorView<std::byte>, size() returns the number of bytes.
        int64_t byteCount1 = view1.size();
        int64_t byteCount2 = view2.size();

        if (byteCount1 != byteCount2) {
            return false;
        }

        // If both are empty (size 0), they are considered equal.
        if (byteCount1 == 0) {
            return true;
        }

        // data() returns const std::byte* pointing to the start of the view's data.
        const std::byte* pData1 = view1.data();
        const std::byte* pData2 = view2.data();

        return std::memcmp(pData1, pData2, static_cast<size_t>(byteCount1)) == 0;
    }


    bool comparePrimitiveAttributes(
        const CesiumGltf::Model& model1, const CesiumGltf::MeshPrimitive& primitive1,
        const CesiumGltf::Model& model2, const CesiumGltf::MeshPrimitive& primitive2
    ) {
        if (primitive1.mode != primitive2.mode) {
            return false;
        }
        if (primitive1.material != primitive2.material) {
            return false; // Material ID must match
        }

        // Compare indices
        if (primitive1.indices >= 0 && primitive2.indices >= 0) {
            if (!compareAccessorData(model1, model1.accessors[primitive1.indices], model2, model2.accessors[primitive2.indices])) {
                return false;
            }
        }
        else if (primitive1.indices >= 0 || primitive2.indices >= 0) {
            return false; // One has indices, the other doesn't
        }

        // Compare attributes
        if (primitive1.attributes.size() != primitive2.attributes.size()) {
            return false;
        }

        for (const auto& pair1 : primitive1.attributes) {
            const std::string& attributeName = pair1.first;
            int32_t accessorIndex1 = pair1.second;

            auto it2 = primitive2.attributes.find(attributeName);
            if (it2 == primitive2.attributes.end()) {
                return false; // Attribute name mismatch
            }
            int32_t accessorIndex2 = it2->second;

            if (!compareAccessorData(model1, model1.accessors[accessorIndex1], model2, model2.accessors[accessorIndex2])) {
                return false;
            }
        }
        // Note: This assumes attributes are iterated in the same order if map iteration order is consistent,
        // or that all attributes from primitive1 are found in primitive2. The check for size handles the reverse.

        // TODO: Compare morph targets if they are used. For now, assume no morph targets or they must be identical.
        if (!primitive1.targets.empty() || !primitive2.targets.empty()) {
            // For simplicity, if either has targets, we'd need a more complex comparison.
            // Assuming for now that meshes with targets are not instanced or require identical targets.
            if (primitive1.targets.size() != primitive2.targets.size()) return false;
            for (size_t i = 0; i < primitive1.targets.size(); ++i) {
                const auto& target1 = primitive1.targets[i];
                const auto& target2 = primitive2.targets[i];
                if (target1.size() != target2.size()) return false;
                for (const auto& t_pair1 : target1) {
                    auto t_it2 = target2.find(t_pair1.first);
                    if (t_it2 == target2.end()) return false;
                    if (!compareAccessorData(model1, model1.accessors[t_pair1.second], model2, model2.accessors[t_it2->second])) {
                        return false;
                    }
                }
            }
        }

        return true;
    }

    void BoundingBox::merge(const BoundingBox& other) {
        if (!other.isValid()) return;
        if (!isValid()) {
            *this = other;
            return;
        }
        min.x = glm::min(min.x, other.min.x);
        min.y = glm::min(min.y, other.min.y);
        min.z = glm::min(min.z, other.min.z);
        max.x = glm::max(max.x, other.max.x);
        max.y = glm::max(max.y, other.max.y);
        max.z = glm::max(max.z, other.max.z);
    }

    void BoundingBox::transform(const glm::dmat4& matrix) {
        if (!isValid()) return;

        glm::dvec3 corners[8] = {
            glm::dvec3(min.x, min.y, min.z),
            glm::dvec3(max.x, min.y, min.z),
            glm::dvec3(min.x, max.y, min.z),
            glm::dvec3(min.x, min.y, max.z),
            glm::dvec3(max.x, max.y, min.z),
            glm::dvec3(max.x, min.y, max.z),
            glm::dvec3(min.x, max.y, max.z),
            glm::dvec3(max.x, max.y, max.z),
        };

        BoundingBox transformedBox; // Start with an invalid box
        transformedBox.min = glm::dvec3(std::numeric_limits<double>::max());
        transformedBox.max = glm::dvec3(std::numeric_limits<double>::lowest());


        for (int i = 0; i < 8; ++i) {
            glm::dvec3 transformedCorner = glm::dvec3(matrix * glm::dvec4(corners[i], 1.0));
            transformedBox.min.x = glm::min(transformedBox.min.x, transformedCorner.x);
            transformedBox.min.y = glm::min(transformedBox.min.y, transformedCorner.y);
            transformedBox.min.z = glm::min(transformedBox.min.z, transformedCorner.z);
            transformedBox.max.x = glm::max(transformedBox.max.x, transformedCorner.x);
            transformedBox.max.y = glm::max(transformedBox.max.y, transformedCorner.y);
            transformedBox.max.z = glm::max(transformedBox.max.z, transformedCorner.z);
        }
        *this = transformedBox;
    }

    bool BoundingBox::isValid() const {
        return min.x <= max.x && min.y <= max.y && min.z <= max.z;
    }


    //Converts the bounding box to the 12-double array format required by 3D Tiles:
    // center_x, center_y, center_z,
    // x_half_axis_x, x_half_axis_y, x_half_axis_z, (defines X-axis direction and half-length)
    // y_half_axis_x, y_half_axis_y, y_half_axis_z, (defines Y-axis direction and half-length)
    // z_half_axis_x, z_half_axis_y, z_half_axis_z  (defines Z-axis direction and half-length)
    std::array<double, 12> BoundingBox::toTilesetBoundingVolumeBox() const {
        if (!isValid()) {
            // Return a zero bounding box or handle error
            return { 0,0,0, 0,0,0, 0,0,0, 0,0,0 };
        }
        glm::dvec3 center = (min + max) * 0.5;
        glm::dvec3 halfExtents = (max - min) * 0.5;

        return {
            center.x, center.y, center.z,
            halfExtents.x, 0.0, 0.0, // X-axis along world X
            0.0, halfExtents.y, 0.0, // Y-axis along world Y
            0.0, 0.0, halfExtents.z  // Z-axis along world Z
        };
    }


    BoundingBox getPrimitiveBoundingBox(const CesiumGltf::Model& model, const CesiumGltf::MeshPrimitive& primitive) {
        BoundingBox box;
        auto attributes_it = primitive.attributes.find("POSITION");
        if (attributes_it == primitive.attributes.end()) {
            return box; // Invalid box if no position
        }

        int32_t positionAccessorIndex = attributes_it->second;
        if (positionAccessorIndex < 0 || static_cast<size_t>(positionAccessorIndex) >= model.accessors.size()) {
            return box;
        }

        const CesiumGltf::Accessor& positionAccessor = model.accessors[positionAccessorIndex];
        
        // Check if accessor itself has min/max, which is more direct and efficient
        if (!positionAccessor.min.empty() && !positionAccessor.max.empty() && 
            positionAccessor.min.size() >= 3 && positionAccessor.max.size() >= 3) {
            box.min = glm::dvec3(positionAccessor.min[0], positionAccessor.min[1], positionAccessor.min[2]);
            box.max = glm::dvec3(positionAccessor.max[0], positionAccessor.max[1], positionAccessor.max[2]);
            return box;
        }
        
        // Fallback: compute from actual vertex data
        CesiumGltf::AccessorView<glm::vec3> positionView(model, positionAccessor);

        if (positionView.status() != CesiumGltf::AccessorViewStatus::Valid || positionView.size() == 0) {
            return box;
        }

        // Initialize with the first vertex
        if (positionView.size() > 0) {
            glm::dvec3 firstPos(positionView[0]);
            box.min = firstPos;
            box.max = firstPos;
        }

        // Process all vertices
        for (int64_t i = 1; i < positionView.size(); ++i) {
            glm::dvec3 pos(positionView[i]); // Convert float to double for box
            box.min.x = glm::min(box.min.x, pos.x);
            box.min.y = glm::min(box.min.y, pos.y);
            box.min.z = glm::min(box.min.z, pos.z);
            box.max.x = glm::max(box.max.x, pos.x);
            box.max.y = glm::max(box.max.y, pos.y);
            box.max.z = glm::max(box.max.z, pos.z);
        }

        return box;
    }

    BoundingBox getMeshBoundingBox(const CesiumGltf::Model& model, const CesiumGltf::Mesh& mesh) {
        BoundingBox combinedBox;
        for (const auto& primitive : mesh.primitives) {
            combinedBox.merge(getPrimitiveBoundingBox(model, primitive));
        }
        return combinedBox;
    }

    bool isTransformEffectivelyIdentity(const TransformComponents& transform) {
        // Implement the logic to determine if a transform is effectively identity
        // This is a placeholder and should be replaced with the actual implementation
        return false;
    }

    // Helper to compare two bounding boxes with a tolerance
    // Returns true if the differences in min and max extents are within the tolerance for all axes.
    bool areBoundingBoxesSimilar(const BoundingBox& bb1, const BoundingBox& bb2, double tolerance) {
        if (!bb1.isValid() || !bb2.isValid()) {
            // If one or both boxes are invalid, they are not considered similar
            // unless both are invalid in the same way (which isValid() doesn't distinguish here).
            // For simplicity, if either is invalid, consider them not similar.
            return false; 
        }

        // Compare min points
        if (std::abs(bb1.min.x - bb2.min.x) > tolerance ||
            std::abs(bb1.min.y - bb2.min.y) > tolerance ||
            std::abs(bb1.min.z - bb2.min.z) > tolerance) {
            return false;
        }

        // Compare max points
        if (std::abs(bb1.max.x - bb2.max.x) > tolerance ||
            std::abs(bb1.max.y - bb2.max.y) > tolerance ||
            std::abs(bb1.max.z - bb2.max.z) > tolerance) {
            return false;
        }

        return true;
    }

} // namespace GltfInstancing