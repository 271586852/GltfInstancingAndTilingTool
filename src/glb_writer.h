#ifndef GLB_WRITER_H
#define GLB_WRITER_H

#include "utilities.h"
#include "instancing_detector.h" // For InstancingDetectionResult and related structs
#include "glb_reader.h"         // For LoadedGltfModel (to access original model data)

#include <vector>
#include <string>
#include <filesystem>
#include <map>
#include <optional>

#include <CesiumGltf/Model.h>
#include <CesiumGltfWriter/GltfWriter.h>
#include <CesiumGltf/BufferView.h>
#include <CesiumGltf/Accessor.h>
#include <CesiumGltf/Material.h>
#include <CesiumGltf/Texture.h>
#include <CesiumGltf/Sampler.h>
#include <CesiumGltf/Image.h>
#include <CesiumGltf/ExtensionExtMeshGpuInstancing.h> // Added for EXT_mesh_gpu_instancing structure


namespace GltfInstancing {

    // Structure to keep track of copied resources (buffers, materials, etc.)
    // to avoid duplication in the new GLB.
    // Maps (originalModelId, originalResourceIndex) to newResourceIndex in the output GLB.
    struct ResourceRemapping {
        std::map<std::pair<int, int>, int> bufferViews;
        std::map<std::pair<int, int>, int> accessors;
        std::map<std::pair<int, int>, int> materials;
        std::map<std::pair<int, int>, int> textures;
        std::map<std::pair<int, int>, int> samplers;
        std::map<std::pair<int, int>, int> images;
        // Buffers are handled differently as we usually consolidate into one or few large buffers.
        // We'll map (originalModelId, originalBufferIndex) to an offset in a new consolidated buffer.
        // However, for simplicity initially, we might copy entire buffers if they are referenced.
        // Let's refine this: we'll have one main new buffer.
        // map (originalModelId, originalBufferViewIndex) -> new BufferView index which points to new combined buffer
    };


    class GlbWriter {
    public:
        GlbWriter();

        // Main function to generate a new GLB file
        // loadedModels: Vector of original models, needed for accessing mesh/material data.
        // detectionResult: The output from InstancingDetector.
        // outputPath: Path to write the new GLB file.
        // Returns the path to the generated GLB if successful, std::nullopt otherwise.
        // Also returns the overall bounding box of the generated content.
        std::optional<std::pair<std::filesystem::path, BoundingBox>> writeInstancedGlb(
            const std::vector<LoadedGltfModel>& originalModels,
            const InstancingDetectionResult& detectionResult,
            const std::filesystem::path& outputPath);

        // 新增：只输出实例化的mesh
        std::optional<std::pair<std::filesystem::path, BoundingBox>> writeInstancedMeshesOnly(
            const std::vector<LoadedGltfModel>& originalModels,
            const InstancingDetectionResult& detectionResult,
            const std::filesystem::path& outputPath);

        // 新增：只输出非实例化的mesh
        std::optional<std::pair<std::filesystem::path, BoundingBox>> writeNonInstancedMeshesOnly(
            const std::vector<LoadedGltfModel>& originalModels,
            const InstancingDetectionResult& detectionResult,
            const std::filesystem::path& outputPath);

        // New method for mesh segmentation
        bool writeMeshesAsSeparateGlbs(
            const std::vector<LoadedGltfModel>& sourceModels,
            const std::filesystem::path& outputDirectory
        );

    private:
        CesiumGltfWriter::GltfWriter _gltfWriter;
        CesiumGltf::Model _outputGltf; // The glTF model we are building
        std::vector<std::byte> _outputBufferData; // Combined binary buffer for the new GLB
        std::map<std::pair<int, int>, int> _materialRemapping; // modelId, oldMaterialId -> newMaterialId
        std::map<std::pair<int, int>, int> _textureRemapping;  // modelId, oldTextureId -> newTextureId
        std::map<std::pair<int, int>, int> _samplerRemapping;  // modelId, oldSamplerId -> newSamplerId
        std::map<std::pair<int, int>, int> _imageRemapping;    // modelId, oldImageId -> newImageId
        
        // Helper to reset internalState for a new GLB file construction
        void resetInternalState();

        // Helper to add data to the main output buffer and create a bufferView for it
        // Returns the index of the newly created BufferView.
        int32_t addDataToBuffer(const gsl::span<const std::byte>& data, int32_t byteStrideOptional, bool isVertexBuffer);

        // Helpers for copying resources and managing remapping
        int32_t copyBufferView(const CesiumGltf::Model& oldModel, int32_t oldBufferViewIndex, int oldModelId, ResourceRemapping& remapping);
        int32_t copyAccessor(const CesiumGltf::Model& oldModel, int32_t oldAccessorIndex, int oldModelId, ResourceRemapping& remapping, bool skipBufferViewRemap, bool isIndicesAccessor);
        int32_t copyAccessor(const CesiumGltf::Model& oldModel, int32_t oldAccessorIndex, int oldModelId, ResourceRemapping& remapping, bool skipBufferViewRemap = false);
        int32_t copyMaterial(const CesiumGltf::Model& oldModel, int32_t oldMaterialIndex, int oldModelId, ResourceRemapping& remapping);
        int32_t copyTexture(const CesiumGltf::Model& oldModel, int32_t oldTextureIndex, int oldModelId, ResourceRemapping& remapping);
        int32_t copySampler(const CesiumGltf::Model& oldModel, int32_t oldSamplerIndex, int oldModelId, ResourceRemapping& remapping);
        int32_t copyImage(const CesiumGltf::Model& oldModel, int32_t oldImageIndex, int oldModelId, ResourceRemapping& remapping);

        // Helper to copy a mesh definition (primitives, materials, etc.)
        // Returns the index of the newly copied mesh in _outputGltf.
        int32_t copyMeshDefinition(
            const CesiumGltf::Model& originalModel,
            int32_t originalMeshIndex,
            int originalModelId, // To use in remapping keys
            ResourceRemapping& remapping);

        // Creates TRS accessors for EXT_mesh_gpu_instancing
        void createInstanceTRS_Accessors(
            const std::vector<MeshInstanceInfo>& instances,
            int32_t& translationAccessorIndex,
            int32_t& rotationAccessorIndex,
            int32_t& scaleAccessorIndex
            // Potentially Matrix accessor if TRS is not preferred by all engines
            // int32_t& matrixAccessorIndex
        );

        // Helper to create a node with EXT_mesh_gpu_instancing
        int32_t createInstancedNode(
            int32_t meshIndex,
            const std::vector<MeshInstanceInfo>& instances,
            const std::string& representativeMeshName // Added for node name
        );

        // Helper to create a standard node for non-instanced meshes
        int32_t createNonInstancedNode(
            int32_t meshIndex,
            const TransformComponents& transform);

        // Validates if an original model is accessible
        const CesiumGltf::Model* getOriginalModelById(
            const std::vector<LoadedGltfModel>& originalModels,
            int modelId) const;
    };

} // namespace GltfInstancing

#endif // GLB_WRITER_H