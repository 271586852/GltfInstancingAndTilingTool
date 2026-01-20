#ifndef UTILITIES_H
#define UTILITIES_H

#include <string>
#include <vector>
#include <optional>
#include <filesystem> // C++17
#include <variant>    // C++17
#include <limits>     // For std::numeric_limits
#include <array>      // For std::array
#include <cmath>      // For std::abs

// Cesium Native includes
// Assuming CesiumGltf is in the include path correctly
#include "CesiumGltf/Model.h"
#include "CesiumGltf/Node.h"
#include "CesiumGltf/MeshPrimitive.h"
#include "CesiumGltf/AccessorView.h"
#include "CesiumGltf/Accessor.h"

// GLM for math
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>

namespace GltfInstancing {

    // --- Logging System ---
    enum class LogLevel {
        NONE = 0,
        LEVEL_ERROR = 1,    // Renamed from ERROR
        LEVEL_WARNING = 2,  // Renamed from WARNING
        LEVEL_INFO = 3,     // Renamed from INFO
        LEVEL_DEBUG = 4,    // Renamed from DEBUG
        LEVEL_VERBOSE = 5   // Renamed from VERBOSE
    };

    extern LogLevel g_currentLogLevel; // Definition will be in utilities.cpp

    void setLogLevel(LogLevel level);
    LogLevel getLogLevel();

    // Core logging function
    void log(LogLevel level, const std::string& message);

    // Convenience logging functions
    // logError is already declared and will be adapted
    void logWarning(const std::string& warningMessage);
    void logInfo(const std::string& infoMessage);
    void logDebug(const std::string& debugMessage);
    void logVerbose(const std::string& verboseMessage);
    
    // --- Basic Logging (Old - to be phased out or adapted) ---
    void logMessage(const std::string& message); // TODO: Phase out or adapt to new system
    void logError(const std::string& errorMessage);

    // --- File and Path Utilities ---
    std::optional<std::vector<char>> readFileBytes(const std::filesystem::path& filePath);
    std::string calculateFileSHA256(const std::filesystem::path& filePath);

    // Structure to hold transform components
    struct TransformComponents {
        glm::dvec3 translation{ 0.0, 0.0, 0.0 };
        glm::dquat rotation{ 1.0, 0.0, 0.0, 0.0 }; // W, X, Y, Z
        glm::dvec3 scale{ 1.0, 1.0, 1.0 };

        glm::dmat4 toMat4() const;
        static TransformComponents fromMat4(const glm::dmat4& matrix);
    };

    glm::dmat4 getNodeWorldTransform(
        const CesiumGltf::Model& model,
        int32_t nodeIndex,
        const std::vector<int32_t>& parentIndices = {}
    );

    glm::dmat4 getNodeWorldTransform(
        const CesiumGltf::Model& model,
        const CesiumGltf::Node& node
    );

    glm::dmat4 getLocalTransformMatrix(const CesiumGltf::Node& node);

    struct MeshInstanceInfo {
        int32_t originalGltfIndex;
        int32_t originalNodeIndex;
        int32_t originalMeshIndex;
        TransformComponents transform;
    };
    
    struct BoundingBox {
        glm::dvec3 min{
            std::numeric_limits<double>::max(),
            std::numeric_limits<double>::max(),
            std::numeric_limits<double>::max()
        };
        glm::dvec3 max{
            std::numeric_limits<double>::lowest(),
            std::numeric_limits<double>::lowest(),
            std::numeric_limits<double>::lowest()
        };

        void merge(const BoundingBox& other);
        void transform(const glm::dmat4& matrix);
        bool isValid() const;
        std::array<double, 12> toTilesetBoundingVolumeBox() const;
    };

    struct InstancedMeshGroup {
        int32_t representativeGltfModelIndex;
        int32_t representativeMeshIndexInModel;
        std::string representativeMeshName;
        size_t meshSignature;
        std::vector<MeshInstanceInfo> instances;
        std::vector<BoundingBox> representativePrimitiveBoundingBoxes;
    };

    struct NonInstancedMeshInfo {
        int32_t originalGltfModelIndex;
        int32_t originalMeshIndexInModel;
        int32_t originalNodeIndexInModel;
        TransformComponents transform;
    };

    bool compareAccessorData(
        const CesiumGltf::Model& model1, const CesiumGltf::Accessor& accessor1,
        const CesiumGltf::Model& model2, const CesiumGltf::Accessor& accessor2
    );

    bool comparePrimitiveAttributes(
        const CesiumGltf::Model& model1, const CesiumGltf::MeshPrimitive& primitive1,
        const CesiumGltf::Model& model2, const CesiumGltf::MeshPrimitive& primitive2
    );

    BoundingBox getPrimitiveBoundingBox(const CesiumGltf::Model& model, const CesiumGltf::MeshPrimitive& primitive);
    BoundingBox getMeshBoundingBox(const CesiumGltf::Model& model, const CesiumGltf::Mesh& mesh);

    bool areBoundingBoxesSimilar(const BoundingBox& bb1, const BoundingBox& bb2, double tolerance);

} // namespace GltfInstancing

#endif // UTILITIES_H