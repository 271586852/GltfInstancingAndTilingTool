#include "glb_writer.h"
#include "utilities.h"
#include <sstream> // For std::ostringstream

#include <CesiumGltfContent\GltfUtilities.h>

#include <CesiumGltf/Model.h>
#include <CesiumGltf/Buffer.h>
#include <CesiumGltf/BufferView.h> // Added
#include <CesiumGltf/Accessor.h>
#include <CesiumGltf/AccessorView.h>
#include <CesiumGltf/Material.h>
#include <CesiumGltf/Texture.h>
#include <CesiumGltf/Sampler.h>
#include <CesiumGltf/Image.h>
#include <CesiumGltf/MeshPrimitive.h>
#include <CesiumGltf/Mesh.h>
#include <CesiumGltf/Node.h>
#include <CesiumGltf/Scene.h>
#include <CesiumGltfWriter/GltfWriter.h>
#include <gsl/span>
#include <nlohmann/json.hpp> // For direct JSON construction for extensions
#include <fstream>
#include <algorithm>
#include <vector> // Ensure included for std::vector usage

// Corrected include for EXT_mesh_gpu_instancing related struct
// Please verify this exact filename and path in your Cesium Native install/source
#include <CesiumGltf/ExtensionExtMeshGpuInstancing.h>
// ExtensionSerialization.h is not used now, we use nlohmann::json directly

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>

namespace GltfInstancing {

    // ... (GlbWriter constructor, reset, getOriginalModelById, addDataToBuffer - keep as corrected before) ...
    // For brevity, I'm showing only the functions with significant changes based on the latest errors.
    // You need to merge these into your existing glb_writer.cpp.

    // --- Start of existing GlbWriter constructor, reset, getOriginalModelById, addDataToBuffer ---
    GlbWriter::GlbWriter() {}

    void GlbWriter::resetInternalState() {
        _outputGltf = CesiumGltf::Model();
        _outputBufferData.clear();

        if (_outputGltf.buffers.empty()) {
            _outputGltf.buffers.emplace_back();
        }

        _outputGltf.asset.version = "2.0";
        _materialRemapping.clear();
        _textureRemapping.clear();
        _samplerRemapping.clear();
        _imageRemapping.clear();
    }

    const CesiumGltf::Model* GlbWriter::getOriginalModelById(
        const std::vector<LoadedGltfModel>& originalModels,
        int modelId) const {



        for (const auto& loadedModel : originalModels) {
            if (loadedModel.uniqueId == modelId) {
                return &loadedModel.model;
            }
        }
        logError("Could not find original model with ID: " + std::to_string(modelId));
        return nullptr;
    }

   int32_t GlbWriter::addDataToBuffer(const gsl::span<const std::byte>& data, int32_t byteStrideOptional, bool isVertexBuffer) {
        if (_outputGltf.buffers.empty()) {
            logError("addDataToBuffer called before main buffer was initialized.");
            return -1;
        }
        size_t currentOffset = _outputBufferData.size();
        size_t padding = (4 - (currentOffset % 4)) % 4;
        _outputBufferData.insert(_outputBufferData.end(), padding, std::byte(0));
        currentOffset = _outputBufferData.size();
        _outputBufferData.insert(_outputBufferData.end(), data.begin(), data.end());

        CesiumGltf::BufferView& bv = _outputGltf.bufferViews.emplace_back();
        bv.buffer = 0;
        bv.byteOffset = static_cast<int64_t>(currentOffset);
        bv.byteLength = static_cast<int64_t>(data.size());
        if (isVertexBuffer && byteStrideOptional > 0) {
            bv.byteStride = byteStrideOptional;
        }
        return static_cast<int32_t>(_outputGltf.bufferViews.size() - 1);
    }
    // --- End of existing GlbWriter constructor, reset, getOriginalModelById, addDataToBuffer ---


    // --- copyImage, copySampler, copyTexture ---
    // These should be largely okay if the std::string::empty() was a red herring.
    // Ensure they are within the namespace.
    int32_t GlbWriter::copyImage(const CesiumGltf::Model& oldModel, int32_t oldImageIndex, int oldModelId, ResourceRemapping& remapping) {
        auto key = std::make_pair(oldModelId, oldImageIndex);
        if (remapping.images.count(key)) { return remapping.images[key]; }
        if (oldImageIndex < 0 || static_cast<size_t>(oldImageIndex) >= oldModel.images.size()) { return -1; }
        const auto& oldImage = oldModel.images[oldImageIndex];
        CesiumGltf::Image newImage = oldImage;
        if (newImage.bufferView >= 0) {
            newImage.bufferView = copyBufferView(oldModel, oldImage.bufferView, oldModelId, remapping);
            if (newImage.bufferView < 0) return -1;
        }
        else if (newImage.uri && !newImage.uri->empty()) { // Image::uri is std::string
            // The "+" operator error E0349 was likely a cascade. This string concat should be fine.
            logMessage(
                "Image " + std::to_string(oldImageIndex) +
                " uses URI: " +
                (newImage.uri ? *newImage.uri : "[no uri]") +
                ". Ensure handling."
            );
        }
        _outputGltf.images.push_back(std::move(newImage));
        int32_t newIndex = static_cast<int32_t>(_outputGltf.images.size() - 1);
        remapping.images[key] = newIndex;
        return newIndex;
    }

    int32_t GlbWriter::copySampler(const CesiumGltf::Model& oldModel, int32_t oldSamplerIndex, int oldModelId, ResourceRemapping& remapping) {
        auto key = std::make_pair(oldModelId, oldSamplerIndex);
        if (remapping.samplers.count(key)) { return remapping.samplers[key]; }
        if (oldSamplerIndex < 0 || static_cast<size_t>(oldSamplerIndex) >= oldModel.samplers.size()) { return -1; }
        _outputGltf.samplers.push_back(oldModel.samplers[oldSamplerIndex]);
        int32_t newIndex = static_cast<int32_t>(_outputGltf.samplers.size() - 1);
        remapping.samplers[key] = newIndex;
        return newIndex;
    }

    int32_t GlbWriter::copyTexture(const CesiumGltf::Model& oldModel, int32_t oldTextureIndex, int oldModelId, ResourceRemapping& remapping) {
        auto key = std::make_pair(oldModelId, oldTextureIndex);
        if (remapping.textures.count(key)) { return remapping.textures[key]; }
        if (oldTextureIndex < 0 || static_cast<size_t>(oldTextureIndex) >= oldModel.textures.size()) { return -1; }
        const auto& oldTexture = oldModel.textures[oldTextureIndex];
        CesiumGltf::Texture newTexture = oldTexture;
        if (oldTexture.sampler >= 0) { newTexture.sampler = copySampler(oldModel, oldTexture.sampler, oldModelId, remapping); }
        if (oldTexture.source >= 0) {
            newTexture.source = copyImage(oldModel, oldTexture.source, oldModelId, remapping);
            if (newTexture.source < 0) return -1;
        }
        _outputGltf.textures.push_back(std::move(newTexture));
        int32_t newIndex = static_cast<int32_t>(_outputGltf.textures.size() - 1);
        remapping.textures[key] = newIndex;
        return newIndex;
    }

    int32_t GlbWriter::copyMaterial(
        const CesiumGltf::Model& oldModel,
        int32_t oldMaterialIndex,
        int oldModelId,
        ResourceRemapping& remapping)
    {
        auto key = std::make_pair(oldModelId, oldMaterialIndex);
        if (remapping.materials.count(key)) {
            return remapping.materials[key];
        }
        if (oldMaterialIndex < 0 || static_cast<size_t>(oldMaterialIndex) >= oldModel.materials.size()) {
            return -1;
        }

        const auto& oldMaterial = oldModel.materials[oldMaterialIndex];
        CesiumGltf::Material newMaterial = oldMaterial;

        // When copying a material, check if it uses any extensions. If so, ensure those extensions
        // are declared in the top-level extensionsUsed list.
        for (const auto& extPair : newMaterial.extensions) {
            bool found = false;
            for (const auto& usedExt : _outputGltf.extensionsUsed) {
                if (usedExt == extPair.first) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                _outputGltf.extensionsUsed.push_back(extPair.first);
            }
        }

        // 1. 普通 TextureInfo
        auto copyTextureInfoLambda =
            [&](std::optional<CesiumGltf::TextureInfo>& newOptTexInfo,
                const std::optional<CesiumGltf::TextureInfo>& oldOptTexInfo) -> bool {
            if (oldOptTexInfo.has_value()) {
                if (!newOptTexInfo.has_value()) {
                    newOptTexInfo.emplace();
                }
                CesiumGltf::TextureInfo& newTexInfoRef = newOptTexInfo.value();
                const CesiumGltf::TextureInfo& oldTexInfoRef = oldOptTexInfo.value();

                newTexInfoRef.extras = oldTexInfoRef.extras;
                newTexInfoRef.extensions = oldTexInfoRef.extensions;
                newTexInfoRef.texCoord = oldTexInfoRef.texCoord;
                if (oldTexInfoRef.index >= 0) {
                    newTexInfoRef.index = copyTexture(oldModel, oldTexInfoRef.index, oldModelId, remapping);
                    if (newTexInfoRef.index < 0) return false;
                }
                else {
                    newTexInfoRef.index = -1;
                }
            }
            else {
                newOptTexInfo.reset();
            }
            return true;
        };

        // 2. MaterialNormalTextureInfo
        auto copyMaterialNormalTextureInfoLambda =
            [&](std::optional<CesiumGltf::MaterialNormalTextureInfo>& newOptTexInfo,
                const std::optional<CesiumGltf::MaterialNormalTextureInfo>& oldOptTexInfo) -> bool {
            if (oldOptTexInfo.has_value()) {
                if (!newOptTexInfo.has_value()) {
                    newOptTexInfo.emplace();
                }
                auto& newTexInfoRef = newOptTexInfo.value();
                const auto& oldTexInfoRef = oldOptTexInfo.value();

                newTexInfoRef.extras = oldTexInfoRef.extras;
                newTexInfoRef.extensions = oldTexInfoRef.extensions;
                newTexInfoRef.texCoord = oldTexInfoRef.texCoord;
                newTexInfoRef.scale = oldTexInfoRef.scale;
                if (oldTexInfoRef.index >= 0) {
                    newTexInfoRef.index = copyTexture(oldModel, oldTexInfoRef.index, oldModelId, remapping);
                    if (newTexInfoRef.index < 0) return false;
                }
                else {
                    newTexInfoRef.index = -1;
                }
            }
            else {
                newOptTexInfo.reset();
            }
            return true;
        };

        // 3. MaterialOcclusionTextureInfo
        auto copyMaterialOcclusionTextureInfoLambda =
            [&](std::optional<CesiumGltf::MaterialOcclusionTextureInfo>& newOptTexInfo,
                const std::optional<CesiumGltf::MaterialOcclusionTextureInfo>& oldOptTexInfo) -> bool {
            if (oldOptTexInfo.has_value()) {
                if (!newOptTexInfo.has_value()) {
                    newOptTexInfo.emplace();
                }
                auto& newTexInfoRef = newOptTexInfo.value();
                const auto& oldTexInfoRef = oldOptTexInfo.value();

                newTexInfoRef.extras = oldTexInfoRef.extras;
                newTexInfoRef.extensions = oldTexInfoRef.extensions;
                newTexInfoRef.texCoord = oldTexInfoRef.texCoord;
                newTexInfoRef.strength = oldTexInfoRef.strength;
                if (oldTexInfoRef.index >= 0) {
                    newTexInfoRef.index = copyTexture(oldModel, oldTexInfoRef.index, oldModelId, remapping);
                    if (newTexInfoRef.index < 0) return false;
                }
                else {
                    newTexInfoRef.index = -1;
                }
            }
            else {
                newOptTexInfo.reset();
            }
            return true;
        };

        // pbrMetallicRoughness
        if (oldMaterial.pbrMetallicRoughness.has_value()) {
            if (!newMaterial.pbrMetallicRoughness.has_value()) {
                newMaterial.pbrMetallicRoughness.emplace();
            }
            auto& newPbr = newMaterial.pbrMetallicRoughness.value();
            const auto& oldPbr = oldMaterial.pbrMetallicRoughness.value();

            if (!copyTextureInfoLambda(newPbr.baseColorTexture, oldPbr.baseColorTexture)) return -1;
            if (!copyTextureInfoLambda(newPbr.metallicRoughnessTexture, oldPbr.metallicRoughnessTexture)) return -1;
        }
        else {
            newMaterial.pbrMetallicRoughness.reset();
        }

        // 这里分别用不同的 lambda
        if (!copyMaterialNormalTextureInfoLambda(newMaterial.normalTexture, oldMaterial.normalTexture)) return -1;
        if (!copyMaterialOcclusionTextureInfoLambda(newMaterial.occlusionTexture, oldMaterial.occlusionTexture)) return -1;
        if (!copyTextureInfoLambda(newMaterial.emissiveTexture, oldMaterial.emissiveTexture)) return -1;

        _outputGltf.materials.push_back(std::move(newMaterial));
        int32_t newIndex = static_cast<int32_t>(_outputGltf.materials.size() - 1);
        remapping.materials[key] = newIndex;
        return newIndex;
    }

    
int32_t GlbWriter::copyBufferView(
    const CesiumGltf::Model& oldModel,
    int32_t oldBufferViewIndex,
    int oldModelId, // Ensure this matches the header declaration
    GltfInstancing::ResourceRemapping& remapping) {

    auto key = std::make_pair(oldModelId, oldBufferViewIndex);
    if (remapping.bufferViews.count(key)) {
        return remapping.bufferViews[key];
    }

    if (oldBufferViewIndex < 0 || static_cast<size_t>(oldBufferViewIndex) >= oldModel.bufferViews.size()) {
        logError("Invalid oldBufferViewIndex in copyBufferView: " + std::to_string(oldBufferViewIndex));
        return -1;
    }

    const auto& oldBufferView = oldModel.bufferViews[oldBufferViewIndex];
    if (oldBufferView.buffer < 0 || static_cast<size_t>(oldBufferView.buffer) >= oldModel.buffers.size()) {
        logError("Invalid buffer index in oldBufferView " + std::to_string(oldBufferViewIndex) + ": " + std::to_string(oldBufferView.buffer));
        return -1;
    }

    const auto& oldBuffer = oldModel.buffers[oldBufferView.buffer];
    gsl::span<const std::byte> oldDataSpan;

    //int64_t bvByteLength = oldBufferView.byteLength.value_or(0);
    int64_t bvByteLength = oldBufferView.byteLength;

    if (!oldBuffer.cesium.data.empty()) {
        if (oldBufferView.byteOffset + bvByteLength > static_cast<int64_t>(oldBuffer.cesium.data.size())) {
            std::ostringstream oss_bv_err; // Use ostringstream for safer concatenation
            oss_bv_err << "BufferView " << oldBufferViewIndex << " (offset " << oldBufferView.byteOffset
                       << ", length " << bvByteLength << ") extends beyond buffer size " << oldBuffer.cesium.data.size();
            logError(oss_bv_err.str());
            return -1;
        }
        oldDataSpan = gsl::span<const std::byte>(
            oldBuffer.cesium.data.data() + oldBufferView.byteOffset,
            static_cast<size_t>(bvByteLength)
        );
    } else if (oldBuffer.uri && !oldBuffer.uri->empty()) {
        std::ostringstream oss_bv_uri_err;
        //oss_bv_uri_err << "BufferView " << oldBufferViewIndex << " references unhandled URI: " << oldBuffer.uri;
        oss_bv_uri_err << "BufferView " << oldBufferViewIndex << " references unhandled URI: "
            << (oldBuffer.uri ? *oldBuffer.uri : "[no uri]");

        logError(oss_bv_uri_err.str());
        return -1;
    } else {
        logError("BufferView " + std::to_string(oldBufferViewIndex) + " references buffer with no data and no URI.");
        return -1;
    }

    int32_t strideForAddData = static_cast<int32_t>(oldBufferView.byteStride.value_or(0));
    int32_t newBufferViewIndex = addDataToBuffer(oldDataSpan, strideForAddData, false); // 不是顶点属性
    if (newBufferViewIndex < 0) {
        return -1;
    }

    if (oldBufferView.target.has_value()) {
        // Ensure newBufferViewIndex is valid before accessing _outputGltf.bufferViews
        if (static_cast<size_t>(newBufferViewIndex) < _outputGltf.bufferViews.size()) {
             _outputGltf.bufferViews[newBufferViewIndex].target = oldBufferView.target.value();
        } else {
            logError("newBufferViewIndex out of bounds in copyBufferView.");
            // This case should ideally not happen if addDataToBuffer works correctly
            return -1;
        }
    }
    remapping.bufferViews[key] = newBufferViewIndex;
    return newBufferViewIndex;
}

int32_t GlbWriter::copyAccessor(const CesiumGltf::Model& oldModel, int32_t oldAccessorIndex, int oldModelId, ResourceRemapping& remapping, bool skipBufferViewRemap) {
    auto key = std::make_pair(oldModelId, oldAccessorIndex);
    if (remapping.accessors.count(key)) { return remapping.accessors[key]; }
    if (oldAccessorIndex < 0 || static_cast<size_t>(oldAccessorIndex) >= oldModel.accessors.size()) {
        logError("copyAccessor: Invalid oldAccessorIndex: " + std::to_string(oldAccessorIndex));
        return -1;
    }

    const auto& oldAccessor = oldModel.accessors[oldAccessorIndex];
    CesiumGltf::Accessor newAccessor = oldAccessor; // Copy metadata

    if (!skipBufferViewRemap) {
        if (oldAccessor.bufferView >= 0) { // Accessor has a buffer view
            const CesiumGltf::BufferView* pOldBv = CesiumGltf::Model::getSafe(&oldModel.bufferViews, oldAccessor.bufferView);
            if (!pOldBv) {
                logError("copyAccessor: Accessor " + std::to_string(oldAccessorIndex) + " references invalid bufferView " + std::to_string(oldAccessor.bufferView));
                return -1; // Cannot copy data without a valid BufferView
            }

            const CesiumGltf::Buffer* pOldBuffer = CesiumGltf::Model::getSafe(&oldModel.buffers, pOldBv->buffer);
            if (!pOldBuffer || pOldBuffer->cesium.data.empty()) {
                logError("copyAccessor: BufferView " + std::to_string(oldAccessor.bufferView) + " references invalid or empty buffer " + std::to_string(pOldBv->buffer));
                return -1; // Cannot copy data without a valid Buffer
            }

            // Calculate the total byte length of the accessor's data
            // This is Accessor.count * (size of one element)
            int64_t elementByteLength = oldAccessor.computeByteSizeOfComponent() * oldAccessor.computeNumberOfComponents();
            if (elementByteLength == 0 && oldAccessor.count > 0) { // Avoid division by zero or issues if component/type is weird
                logError("copyAccessor: Accessor " + std::to_string(oldAccessorIndex) + " has zero element byte length with count > 0.");
                return -1;
            }
            int64_t totalAccessorByteLength = oldAccessor.count * elementByteLength;

            // Calculate the actual start of the accessor data within the buffer's data
            int64_t accessorStartOffsetInBuffer = pOldBv->byteOffset + oldAccessor.byteOffset;

            // Boundary checks
            if (accessorStartOffsetInBuffer < 0 ||
                accessorStartOffsetInBuffer + totalAccessorByteLength > static_cast<int64_t>(pOldBuffer->cesium.data.size())) {
                logError("copyAccessor: Accessor " + std::to_string(oldAccessorIndex) + " data (offset " + std::to_string(accessorStartOffsetInBuffer) +
                    ", length " + std::to_string(totalAccessorByteLength) + ") is out of bounds for buffer " + std::to_string(pOldBv->buffer) +
                    " (size " + std::to_string(pOldBuffer->cesium.data.size()) + ")");
                return -1;
            }

            // If the accessor is interleaved, AccessorView is still the best way to get de-interleaved data.
            // However, our goal here is to copy the raw *referenced* data segment.
            // For non-interleaved data (bufferView.byteStride is 0 or elementByteLength), we can directly copy.
            // For interleaved data, we MUST use AccessorView to read element by element.
            // Let's reconsider. The `AccessorView<std::byte>` was an attempt to get a view of the raw bytes
            // *as defined by the accessor*, which means it should handle stride correctly.
            // The issue was `sizeof(T)` check.

            // Correct approach for copying raw data using AccessorView by iterating:
            // This will correctly handle strides and de-interleave data if necessary.
            std::vector<std::byte> accessorDataBytes;
            accessorDataBytes.reserve(static_cast<size_t>(totalAccessorByteLength));

            // We need to pick a T for AccessorView that matches the component type and size.
            // This is getting complicated. Let's simplify: copy the raw segment from BufferView.
            // This assumes AccessorView is NOT strictly needed if we just want the bytes
            // that the accessor *could* point to within its buffer view segment.
            // This is only correct if there's no interleaving (i.e., accessor.byteStride == elementSize).
            // If there IS interleaving, we must read element-by-element.

            // Let's go back to using AccessorView<std::byte> but understand its limitations.
            // The status check is key. Why is it failing?
            // The `create` function in AccessorView.h has:
            // `if (sizeof(T) != accessorBytesPerStride)`
            // For `AccessorView<std::byte>`, `sizeof(T)` is 1.
            // `accessorBytesPerStride` is `accessorComponentElements * accessorComponentBytes`.
            // So, this will only be `Valid` if `accessorComponentElements * accessorComponentBytes == 1`.
            // This means it only works for SCALAR of BYTE/UNSIGNED_BYTE. This is too restrictive.

            // *** Revised strategy for copyAccessor's data copying: ***
            // We need to get the raw bytes that this accessor's data occupies.
            // If the data is interleaved (bufferView.byteStride > elementSize), we must
            // read it element by element and pack it.
            // If it's not interleaved, we can copy a contiguous block.

            const CesiumGltf::BufferView& bufferView = *pOldBv; // We know pOldBv is valid
            const std::vector<std::byte>& bufferData = pOldBuffer->cesium.data;

            int64_t actualStride = oldAccessor.computeByteStride(oldModel); // This considers bufferView.byteStride
            std::vector<std::byte> collectedBytes;
            collectedBytes.reserve(static_cast<size_t>(totalAccessorByteLength));

            for (int64_t i = 0; i < oldAccessor.count; ++i) {
                const std::byte* pElementStart = bufferData.data() + bufferView.byteOffset + oldAccessor.byteOffset + i * actualStride;
                // Boundary check for each element read
                if (pElementStart < bufferData.data() || (pElementStart + elementByteLength) >(bufferData.data() + bufferData.size())) {
                    logError("copyAccessor: Element " + std::to_string(i) + " of accessor " + std::to_string(oldAccessorIndex) + " is out of buffer bounds.");
                    return -1;
                }
                collectedBytes.insert(collectedBytes.end(), pElementStart, pElementStart + elementByteLength);
            }

            gsl::span<const std::byte> data_to_copy(collectedBytes.data(), collectedBytes.size());
            int32_t newBufferViewIdx = addDataToBuffer(data_to_copy, static_cast<int32_t>(elementByteLength), false); // 不是顶点属性

            if (newBufferViewIdx < 0) return -1;
            newAccessor.bufferView = newBufferViewIdx;
            newAccessor.byteOffset = 0;
            _outputGltf.accessors.push_back(std::move(newAccessor));
        }
        else { // Accessor without a bufferView
            if (oldAccessor.count > 0 && oldAccessor.extensions.find("KHR_draco_mesh_compression") == oldAccessor.extensions.end()) {
                logMessage("Warning: Accessor " + std::to_string(oldAccessorIndex) + " has no bufferView but count > 0. Copying definition only.");
            }
            _outputGltf.accessors.push_back(std::move(newAccessor));
        }
    }
    else { // skipBufferViewRemap is true
        _outputGltf.accessors.push_back(std::move(newAccessor));
    }

    int32_t newIndex = static_cast<int32_t>(_outputGltf.accessors.size() - 1);
    remapping.accessors[key] = newIndex;
    return newIndex;
}
    // ... (copyMeshDefinition should be mostly fine if copyAccessor is fixed) ...
    // Paste your copyMeshDefinition here, ensure it's in the namespace
    int32_t GlbWriter::copyMeshDefinition(
        const CesiumGltf::Model& originalModel,
        int32_t originalMeshIndex,
        int originalModelId,
        ResourceRemapping& remapping) {
        if (originalMeshIndex < 0 || static_cast<size_t>(originalMeshIndex) >= originalModel.meshes.size()) { return -1; }
        const auto& oldMesh = originalModel.meshes[originalMeshIndex];
        CesiumGltf::Mesh newMesh;
        newMesh.name = oldMesh.name;

        for (const auto& oldPrimitive : oldMesh.primitives) {
            CesiumGltf::MeshPrimitive newPrimitive;
            newPrimitive.mode = oldPrimitive.mode;
            if (oldPrimitive.material >= 0) {
                newPrimitive.material = copyMaterial(originalModel, oldPrimitive.material, originalModelId, remapping);
                if (newPrimitive.material < 0 && oldPrimitive.material >= 0) { return -1; }
            }
            else { newPrimitive.material = -1; }

            if (oldPrimitive.indices >= 0) {
                newPrimitive.indices = copyAccessor(originalModel, oldPrimitive.indices, originalModelId, remapping);
                if (newPrimitive.indices < 0) return -1;
                const auto& idxAccessor = _outputGltf.accessors[newPrimitive.indices];
                if (idxAccessor.bufferView >= 0 && static_cast<size_t>(idxAccessor.bufferView) < _outputGltf.bufferViews.size()) {
                    _outputGltf.bufferViews[idxAccessor.bufferView].target = CesiumGltf::BufferView::Target::ELEMENT_ARRAY_BUFFER;
                }
            }
            for (const auto& oldAttrPair : oldPrimitive.attributes) {
                int32_t newAccessorIdx = copyAccessor(originalModel, oldAttrPair.second, originalModelId, remapping);
                if (newAccessorIdx < 0) return -1;
                newPrimitive.attributes[oldAttrPair.first] = newAccessorIdx;
                const auto& attrAccessor = _outputGltf.accessors[newAccessorIdx];
                if (attrAccessor.bufferView >= 0 && static_cast<size_t>(attrAccessor.bufferView) < _outputGltf.bufferViews.size()) {
                    _outputGltf.bufferViews[attrAccessor.bufferView].target = CesiumGltf::BufferView::Target::ARRAY_BUFFER;
                }
            }
            if (!oldPrimitive.targets.empty()) {
                newPrimitive.targets.resize(oldPrimitive.targets.size());
                for (size_t i = 0; i < oldPrimitive.targets.size(); ++i) {
                    for (const auto& oldTargetAttrPair : oldPrimitive.targets[i]) {
                        int32_t newAccessorIdx = copyAccessor(originalModel, oldTargetAttrPair.second, originalModelId, remapping);
                        if (newAccessorIdx < 0) return -1;
                        newPrimitive.targets[i][oldTargetAttrPair.first] = newAccessorIdx;
                    }
                }
                if (oldMesh.weights.empty() && !newPrimitive.targets.empty()) {
                    newMesh.weights.assign(newPrimitive.targets.size(), 0.0);
                }
                else { newMesh.weights = oldMesh.weights; }
            }
            newMesh.primitives.push_back(std::move(newPrimitive));
        }
        _outputGltf.meshes.push_back(std::move(newMesh));
        return static_cast<int32_t>(_outputGltf.meshes.size() - 1);
    }


    void GlbWriter::createInstanceTRS_Accessors(
        const std::vector<MeshInstanceInfo>& instances,
        int32_t& translationAccessorIndex,
        int32_t& rotationAccessorIndex,
        int32_t& scaleAccessorIndex) {
        std::vector<float> translationData;
        std::vector<float> rotationData;
        std::vector<float> scaleData;

        translationData.reserve(instances.size() * 3);
        rotationData.reserve(instances.size() * 4);
        scaleData.reserve(instances.size() * 3);

        for (const auto& instance : instances) {
            translationData.push_back(static_cast<float>(instance.transform.translation.x));
            translationData.push_back(static_cast<float>(instance.transform.translation.y));
            translationData.push_back(static_cast<float>(instance.transform.translation.z));

            rotationData.push_back(static_cast<float>(instance.transform.rotation.x));
            rotationData.push_back(static_cast<float>(instance.transform.rotation.y));
            rotationData.push_back(static_cast<float>(instance.transform.rotation.z));
            rotationData.push_back(static_cast<float>(instance.transform.rotation.w));

            scaleData.push_back(static_cast<float>(instance.transform.scale.x));
            scaleData.push_back(static_cast<float>(instance.transform.scale.y));
            scaleData.push_back(static_cast<float>(instance.transform.scale.z));
        }

        translationAccessorIndex = -1; rotationAccessorIndex = -1; scaleAccessorIndex = -1;

        if (!translationData.empty()) {
            gsl::span<const std::byte> trans_span(reinterpret_cast<const std::byte*>(translationData.data()), translationData.size() * sizeof(float));
            int32_t transBvIdx = addDataToBuffer(trans_span, 0, false);
            CesiumGltf::Accessor& transAcc = _outputGltf.accessors.emplace_back();
            transAcc.bufferView = transBvIdx;
            transAcc.componentType = CesiumGltf::Accessor::ComponentType::FLOAT;
            transAcc.type = CesiumGltf::Accessor::Type::VEC3;
            transAcc.count = static_cast<int64_t>(instances.size());
            translationAccessorIndex = static_cast<int32_t>(_outputGltf.accessors.size() - 1);
        }
        if (!rotationData.empty()) {
            gsl::span<const std::byte> rot_span(reinterpret_cast<const std::byte*>(rotationData.data()), rotationData.size() * sizeof(float));
            int32_t rotBvIdx = addDataToBuffer(rot_span, 0, false);
            CesiumGltf::Accessor& rotAcc = _outputGltf.accessors.emplace_back();
            rotAcc.bufferView = rotBvIdx;
            rotAcc.componentType = CesiumGltf::Accessor::ComponentType::FLOAT;
            rotAcc.type = CesiumGltf::Accessor::Type::VEC4;
            rotAcc.count = static_cast<int64_t>(instances.size());
            rotationAccessorIndex = static_cast<int32_t>(_outputGltf.accessors.size() - 1);
        }
        if (!scaleData.empty()) {
            gsl::span<const std::byte> scale_span(reinterpret_cast<const std::byte*>(scaleData.data()), scaleData.size() * sizeof(float));
            int32_t scaleBvIdx = addDataToBuffer(scale_span, 0, false);
            CesiumGltf::Accessor& scaleAcc = _outputGltf.accessors.emplace_back();
            scaleAcc.bufferView = scaleBvIdx;
            scaleAcc.componentType = CesiumGltf::Accessor::ComponentType::FLOAT;
            scaleAcc.type = CesiumGltf::Accessor::Type::VEC3;
            scaleAcc.count = static_cast<int64_t>(instances.size());
            scaleAccessorIndex = static_cast<int32_t>(_outputGltf.accessors.size() - 1);
        }
    }

    int32_t GlbWriter::createInstancedNode(
        int32_t meshIndexInOutputGltf,
        const std::vector<MeshInstanceInfo>& instances,
        const std::string& representativeMeshName) {
        CesiumGltf::Node newNode;
        newNode.mesh = meshIndexInOutputGltf;
        if (!representativeMeshName.empty()) {
            newNode.name = representativeMeshName;
        } else {
            newNode.name = "instanced_node_mesh_" + std::to_string(meshIndexInOutputGltf);
        }

        int32_t transAccIdx = -1, rotAccIdx = -1, scaleAccIdx = -1;
        createInstanceTRS_Accessors(instances, transAccIdx, rotAccIdx, scaleAccIdx);

        if (transAccIdx != -1 || rotAccIdx != -1 || scaleAccIdx != -1) {
            CesiumGltf::ExtensionExtMeshGpuInstancing instancingExtensionData; 

            if (transAccIdx != -1) instancingExtensionData.attributes["TRANSLATION"] = transAccIdx;
            if (rotAccIdx != -1)   instancingExtensionData.attributes["ROTATION"] = rotAccIdx;
            if (scaleAccIdx != -1) instancingExtensionData.attributes["SCALE"] = scaleAccIdx;

            newNode.extensions["EXT_mesh_gpu_instancing"] = instancingExtensionData;

            // Add to extensionsUsed if not already present
            bool foundExtUsed = false;
            for (const auto& extName : _outputGltf.extensionsUsed) {
                if (extName == "EXT_mesh_gpu_instancing") {
                    foundExtUsed = true;
                    break;
                }
            }
            if (!foundExtUsed) { 
                _outputGltf.extensionsUsed.push_back("EXT_mesh_gpu_instancing");
                // Optionally add to extensionsRequired if all clients must support it
                // _outputGltf.extensionsRequired.push_back("EXT_mesh_gpu_instancing");
            }
        }

        _outputGltf.nodes.push_back(std::move(newNode));
        return static_cast<int32_t>(_outputGltf.nodes.size() - 1);
    }

    // ... (createNonInstancedNode should be fine with previous corrections) ...
    int32_t GlbWriter::createNonInstancedNode(
        int32_t meshIndexInOutputGltf,
        const TransformComponents& transform) {
        CesiumGltf::Node newNode;
        newNode.mesh = meshIndexInOutputGltf;
        
        // Node TRS are std::vector<double>
        // Only set if not default values
        const double EPSILON = 1e-10;
        
        // Check if translation is not [0, 0, 0]
        if (std::abs(transform.translation.x) > EPSILON || 
            std::abs(transform.translation.y) > EPSILON || 
            std::abs(transform.translation.z) > EPSILON) {
            newNode.translation = { transform.translation.x, transform.translation.y, transform.translation.z };
        }
        
        // Check if rotation is not [0, 0, 0, 1] (identity quaternion)
        // Note: glm::dquat is (w, x, y, z), but in glTF it's [x, y, z, w]
        if (std::abs(transform.rotation.x) > EPSILON || 
            std::abs(transform.rotation.y) > EPSILON || 
            std::abs(transform.rotation.z) > EPSILON || 
            std::abs(transform.rotation.w - 1.0) > EPSILON) {
            newNode.rotation = { transform.rotation.x, transform.rotation.y, transform.rotation.z, transform.rotation.w };
        }
        
        // Check if scale is not [1, 1, 1]
        if (std::abs(transform.scale.x - 1.0) > EPSILON || 
            std::abs(transform.scale.y - 1.0) > EPSILON || 
            std::abs(transform.scale.z - 1.0) > EPSILON) {
            newNode.scale = { transform.scale.x, transform.scale.y, transform.scale.z };
        }
        
        _outputGltf.nodes.push_back(std::move(newNode));
        return static_cast<int32_t>(_outputGltf.nodes.size() - 1);
    }


    // ... (writeInstancedGlb - ensure Model::scene is used, and GltfWriterResult is handled) ...
    std::optional<std::pair<std::filesystem::path, BoundingBox>> GlbWriter::writeInstancedGlb(
        const std::vector<LoadedGltfModel>& originalModels,
        const InstancingDetectionResult& detectionResult,
        const std::filesystem::path& outputPath) {
        logMessage("Starting GLB generation: " + outputPath.string());
        resetInternalState();
        ResourceRemapping remapping;
        std::vector<int32_t> rootNodeIndices;
        BoundingBox overallBoundingBox;

        // Process Instanced Groups
        for (const auto& group : detectionResult.instancedGroups) {
            if (group.instances.empty()) continue;
            const CesiumGltf::Model* representativeModel = getOriginalModelById(originalModels, group.representativeGltfModelIndex);
            if (!representativeModel) { continue; } // Error already logged
            int32_t newMeshIndex = copyMeshDefinition(*representativeModel, group.representativeMeshIndexInModel, group.representativeGltfModelIndex, remapping);
            if (newMeshIndex < 0) { continue; } // Error already logged
            int32_t instancedNodeIndex = createInstancedNode(newMeshIndex, group.instances, group.representativeMeshName);
            if (instancedNodeIndex >= 0) {
                rootNodeIndices.push_back(instancedNodeIndex);
                if (static_cast<size_t>(newMeshIndex) < _outputGltf.meshes.size()) { // Bounds check
                    BoundingBox meshLocalBox = getMeshBoundingBox(_outputGltf, _outputGltf.meshes[newMeshIndex]);
                    if (meshLocalBox.isValid()) {
                        for (const auto& instanceInfo : group.instances) {
                            BoundingBox instanceBox = meshLocalBox;
                            instanceBox.transform(instanceInfo.transform.toMat4());
                            overallBoundingBox.merge(instanceBox);
                        }
                    }
                }
            }
        }

        // Process Non-Instanced Meshes
        for (const auto& niMeshInfo : detectionResult.nonInstancedMeshes) {
            const CesiumGltf::Model* originalModel = getOriginalModelById(originalModels, niMeshInfo.originalGltfModelIndex);
            if (!originalModel) { continue; }
            int32_t newMeshIndex = copyMeshDefinition(*originalModel, niMeshInfo.originalMeshIndexInModel, niMeshInfo.originalGltfModelIndex, remapping);
            if (newMeshIndex < 0) { continue; }
            int32_t regularNodeIndex = createNonInstancedNode(newMeshIndex, niMeshInfo.transform);
            if (regularNodeIndex >= 0) {
                rootNodeIndices.push_back(regularNodeIndex);
                if (static_cast<size_t>(newMeshIndex) < _outputGltf.meshes.size()) { // Bounds check
                    BoundingBox meshLocalBox = getMeshBoundingBox(_outputGltf, _outputGltf.meshes[newMeshIndex]);
                    if (meshLocalBox.isValid()) {
                        meshLocalBox.transform(niMeshInfo.transform.toMat4());
                        overallBoundingBox.merge(meshLocalBox);
                    }
                }
            }
        }

        if (rootNodeIndices.empty() && _outputGltf.meshes.empty()) {
            logMessage("No meshes or nodes were processed. Output GLB will be empty or invalid.");
        }

        if (!rootNodeIndices.empty()) {
            CesiumGltf::Scene& scene = _outputGltf.scenes.emplace_back();
            scene.nodes = rootNodeIndices;
            _outputGltf.scene = static_cast<int32_t>(_outputGltf.scenes.size() - 1); // Use Model::scene for default scene index
        }
        else if (!_outputGltf.meshes.empty()) {
            logMessage("Warning: Output GLB has meshes but no nodes in the scene.");
        }

        if (!_outputGltf.buffers.empty()) {
            _outputGltf.buffers[0].byteLength = static_cast<int64_t>(_outputBufferData.size());
        }
        else if (!_outputBufferData.empty()) {
            logError("Output buffer data exists, but no buffer definition in glTF model!");
            return std::nullopt;
        }

        CesiumGltfContent::GltfUtilities::removeUnusedAccessors(_outputGltf);
        CesiumGltfContent::GltfUtilities::removeUnusedBufferViews(_outputGltf);
        CesiumGltfContent::GltfUtilities::removeUnusedBuffers(_outputGltf);

        CesiumGltfWriter::GltfWriterOptions writerOptions;
        CesiumGltfWriter::GltfWriterResult writerResult = _gltfWriter.writeGlb(_outputGltf, _outputBufferData, writerOptions);

        if (writerResult.gltfBytes.empty() && !writerResult.errors.empty()) { // Check if gltfBytes is empty AND there are errors
            logError("Failed to serialize GLB.");
            for (const auto& err : writerResult.errors) logError("Writer Error: " + err);
            for (const auto& warn : writerResult.warnings) logMessage("Writer Warning: " + warn); // Also log warnings
            return std::nullopt;
        }

        // Even if there are warnings, gltfBytes might still be valid if not empty.
        // If gltfBytes is empty but no errors, it's also a failure.
        if (writerResult.gltfBytes.empty()) {
            logError("Failed to serialize GLB: GltfWriter result has empty gltfBytes, but no explicit errors reported by it (or errors were already logged).");
            for (const auto& warn : writerResult.warnings) logMessage("Writer Warning: " + warn);
            return std::nullopt;
        }

        std::vector<std::byte> glbBytes = std::move(writerResult.gltfBytes);

        std::ofstream outFile(outputPath, std::ios::binary);
        if (!outFile) { /* ... error handling ... */ return std::nullopt; }
        outFile.write(reinterpret_cast<const char*>(glbBytes.data()), glbBytes.size());
        outFile.close();
        if (!outFile) { /* ... error handling ... */ return std::nullopt; }

        logMessage("Successfully wrote instanced GLB to: " + outputPath.string());
        return std::make_pair(outputPath, overallBoundingBox);
    }

    // 新增：只输出实例化的mesh
    std::optional<std::pair<std::filesystem::path, BoundingBox>> GlbWriter::writeInstancedMeshesOnly(
        const std::vector<LoadedGltfModel>& originalModels,
        const InstancingDetectionResult& detectionResult,
        const std::filesystem::path& outputPath) {
        
        logMessage("Starting GLB generation (instanced meshes only): " + outputPath.string());
        resetInternalState();
        ResourceRemapping remapping;
        std::vector<int32_t> rootNodeIndices;
        BoundingBox overallBoundingBox;

        // 只处理实例化组
        for (const auto& group : detectionResult.instancedGroups) {
            if (group.instances.empty()) continue;
            const CesiumGltf::Model* representativeModel = getOriginalModelById(originalModels, group.representativeGltfModelIndex);
            if (!representativeModel) { continue; }
            int32_t newMeshIndex = copyMeshDefinition(*representativeModel, group.representativeMeshIndexInModel, group.representativeGltfModelIndex, remapping);
            if (newMeshIndex < 0) { continue; }
            int32_t instancedNodeIndex = createInstancedNode(newMeshIndex, group.instances, group.representativeMeshName);
            if (instancedNodeIndex >= 0) {
                rootNodeIndices.push_back(instancedNodeIndex);
                if (static_cast<size_t>(newMeshIndex) < _outputGltf.meshes.size()) {
                    BoundingBox meshLocalBox = getMeshBoundingBox(_outputGltf, _outputGltf.meshes[newMeshIndex]);
                    if (meshLocalBox.isValid()) {
                        for (const auto& instanceInfo : group.instances) {
                            BoundingBox instanceBox = meshLocalBox;
                            instanceBox.transform(instanceInfo.transform.toMat4());
                            overallBoundingBox.merge(instanceBox);
                        }
                    } else {
                        logMessage("  Invalid bounding box!");
                    }
                }
            }
        }

        if (rootNodeIndices.empty() && _outputGltf.meshes.empty()) {
            logMessage("No instanced meshes were processed.");
        }

        if (!rootNodeIndices.empty()) {
            CesiumGltf::Scene& scene = _outputGltf.scenes.emplace_back();
            scene.nodes = rootNodeIndices;
            _outputGltf.scene = static_cast<int32_t>(_outputGltf.scenes.size() - 1);
        }

        if (!_outputGltf.buffers.empty()) {
            _outputGltf.buffers[0].byteLength = static_cast<int64_t>(_outputBufferData.size());
        }

        // 清理未使用的对象
        CesiumGltfContent::GltfUtilities::removeUnusedAccessors(_outputGltf);
        CesiumGltfContent::GltfUtilities::removeUnusedBufferViews(_outputGltf);
        CesiumGltfContent::GltfUtilities::removeUnusedBuffers(_outputGltf);

        CesiumGltfWriter::GltfWriterOptions writerOptions;
        CesiumGltfWriter::GltfWriterResult writerResult = _gltfWriter.writeGlb(_outputGltf, _outputBufferData, writerOptions);

        if (writerResult.gltfBytes.empty()) {
            logError("Failed to serialize instanced GLB.");
            return std::nullopt;
        }

        std::vector<std::byte> glbBytes = std::move(writerResult.gltfBytes);
        std::ofstream outFile(outputPath, std::ios::binary);
        if (!outFile) { return std::nullopt; }
        outFile.write(reinterpret_cast<const char*>(glbBytes.data()), glbBytes.size());
        outFile.close();

        logMessage("Successfully wrote instanced GLB to: " + outputPath.string());
        return std::make_pair(outputPath, overallBoundingBox);
    }

    // 新增：只输出非实例化的mesh
    std::optional<std::pair<std::filesystem::path, BoundingBox>> GlbWriter::writeNonInstancedMeshesOnly(
        const std::vector<LoadedGltfModel>& originalModels,
        const InstancingDetectionResult& detectionResult,
        const std::filesystem::path& outputPath) {
        
        logMessage("Starting GLB generation (non-instanced meshes only): " + outputPath.string());
        resetInternalState();
        ResourceRemapping remapping;
        std::vector<int32_t> rootNodeIndices;
        BoundingBox overallBoundingBox;

        // 只处理非实例化的Mesh
        for (const auto& niMeshInfo : detectionResult.nonInstancedMeshes) {
            const CesiumGltf::Model* originalModel = getOriginalModelById(originalModels, niMeshInfo.originalGltfModelIndex);
            if (!originalModel) { continue; }
            int32_t newMeshIndex = copyMeshDefinition(*originalModel, niMeshInfo.originalMeshIndexInModel, niMeshInfo.originalGltfModelIndex, remapping);
            if (newMeshIndex < 0) { continue; }
            int32_t regularNodeIndex = createNonInstancedNode(newMeshIndex, niMeshInfo.transform);
            if (regularNodeIndex >= 0) {
                rootNodeIndices.push_back(regularNodeIndex);
                if (static_cast<size_t>(newMeshIndex) < _outputGltf.meshes.size()) {
                    BoundingBox meshLocalBox = getMeshBoundingBox(_outputGltf, _outputGltf.meshes[newMeshIndex]);
                    if (meshLocalBox.isValid()) {
                        meshLocalBox.transform(niMeshInfo.transform.toMat4());
                        overallBoundingBox.merge(meshLocalBox);
                    } else {
                        logMessage("  Invalid bounding box!");
                    }
                }
            }
        }

        if (rootNodeIndices.empty() && _outputGltf.meshes.empty()) {
            logMessage("No non-instanced meshes were processed.");
        }

        if (!rootNodeIndices.empty()) {
            CesiumGltf::Scene& scene = _outputGltf.scenes.emplace_back();
            scene.nodes = rootNodeIndices;
            _outputGltf.scene = static_cast<int32_t>(_outputGltf.scenes.size() - 1);
        }

        if (!_outputGltf.buffers.empty()) {
            _outputGltf.buffers[0].byteLength = static_cast<int64_t>(_outputBufferData.size());
        }

        // 清理未使用的对象
        CesiumGltfContent::GltfUtilities::removeUnusedAccessors(_outputGltf);
        CesiumGltfContent::GltfUtilities::removeUnusedBufferViews(_outputGltf);
        CesiumGltfContent::GltfUtilities::removeUnusedBuffers(_outputGltf);

        CesiumGltfWriter::GltfWriterOptions writerOptions;
        CesiumGltfWriter::GltfWriterResult writerResult = _gltfWriter.writeGlb(_outputGltf, _outputBufferData, writerOptions);

        if (writerResult.gltfBytes.empty()) {
            logError("Failed to serialize non-instanced GLB.");
            return std::nullopt;
        }

        std::vector<std::byte> glbBytes = std::move(writerResult.gltfBytes);
        std::ofstream outFile(outputPath, std::ios::binary);
        if (!outFile) { return std::nullopt; }
        outFile.write(reinterpret_cast<const char*>(glbBytes.data()), glbBytes.size());
        outFile.close();

        logMessage("Successfully wrote non-instanced GLB to: " + outputPath.string());
        return std::make_pair(outputPath, overallBoundingBox);
    }
    
    bool GlbWriter::writeMeshesAsSeparateGlbs(
        const std::vector<LoadedGltfModel>& sourceModels,
        const std::filesystem::path& outputDirectory) {
        bool overallSuccess = true;

        for (size_t modelIdx = 0; modelIdx < sourceModels.size(); ++modelIdx) {
            const auto& loadedModel = sourceModels[modelIdx];
            const CesiumGltf::Model* originalGltf = &loadedModel.model; 

            if (!originalGltf) {
                GltfInstancing::logError("Internal error with loaded model at index " + std::to_string(modelIdx) + ". Skipping segmentation for this model.");
                overallSuccess = false;
                continue;
            }
            
            if (originalGltf->meshes.empty()) {
                logMessage("Source model " + loadedModel.originalPath.string() + " has no meshes to segment.");
                continue;
            }

            logMessage("Segmenting meshes from: " + loadedModel.originalPath.string());

            for (size_t meshIdx = 0; meshIdx < originalGltf->meshes.size(); ++meshIdx) {
                resetInternalState(); 
                ResourceRemapping remapping; 

                const CesiumGltf::Mesh& currentOriginalMesh = originalGltf->meshes[meshIdx];
                std::string originalMeshNameInfo = currentOriginalMesh.name.empty() ? "" : " (name: " + currentOriginalMesh.name + ")";

                logMessage("Processing mesh " + std::to_string(meshIdx) + originalMeshNameInfo + " for segmentation.");

                int32_t newMeshIndexInOutput = copyMeshDefinition(*originalGltf, static_cast<int32_t>(meshIdx), static_cast<int>(modelIdx), remapping);

                if (newMeshIndexInOutput < 0) {
                    logError("Failed to copy mesh definition for model " + loadedModel.originalPath.stem().string() + 
                             ", mesh index " + std::to_string(meshIdx) + originalMeshNameInfo);
                    overallSuccess = false;
                    continue; 
                }
            
                CesiumGltf::Node nodeForExportedGlb;
                nodeForExportedGlb.mesh = newMeshIndexInOutput; 
                if (!currentOriginalMesh.name.empty()) {
                    nodeForExportedGlb.name = currentOriginalMesh.name; // Use mesh name for the node
                } else {
                    // Fallback name if original mesh had no name
                    nodeForExportedGlb.name = loadedModel.originalPath.stem().string() + "_mesh_" + std::to_string(meshIdx);
                }

                // Try to find and apply TRS from the original node using this mesh
                // bool appliedOriginalNodeTransform = false; // Original variable, now replaced by more specific pointers
                const CesiumGltf::Node* pOriginalNodeProvidingTransform = nullptr;
                const CesiumGltf::Node* pOriginalNodeProvidingInstancing = nullptr;

                // First, try to find a node that both references this mesh AND uses instancing for it.
                for (const auto& candidateNode : originalGltf->nodes) {
                    if (candidateNode.mesh == static_cast<int32_t>(meshIdx) && 
                        candidateNode.extensions.count("EXT_mesh_gpu_instancing")) {
                        pOriginalNodeProvidingInstancing = &candidateNode;
                        break; 
                    }
                }

                // If no instancing node was found directly referencing this mesh,
                // find any node that directly references this mesh to get its standard TRS.
                if (!pOriginalNodeProvidingInstancing) {
                    for (const auto& candidateNode : originalGltf->nodes) {
                        if (candidateNode.mesh == static_cast<int32_t>(meshIdx)) {
                            pOriginalNodeProvidingTransform = &candidateNode;
                            
                            nodeForExportedGlb.translation = pOriginalNodeProvidingTransform->translation;
                            nodeForExportedGlb.rotation    = pOriginalNodeProvidingTransform->rotation;
                            nodeForExportedGlb.scale       = pOriginalNodeProvidingTransform->scale;
                            nodeForExportedGlb.matrix      = pOriginalNodeProvidingTransform->matrix;
                            break; 
                        }
                    }
                }
                
                // Now, specifically handle EXT_mesh_gpu_instancing if the original node (pOriginalNodeProvidingInstancing) had it.
                if (pOriginalNodeProvidingInstancing) {
                    // If we found an instancing node, its extension data takes precedence.
                    // Clear any TRS that might have been tentatively copied from a non-instancing node.
                    // (This path is less likely if pOriginalNodeProvidingInstancing is found first, but good for clarity)
                    // Actually, the logic above ensures if pOriginalNodeProvidingInstancing is set, pOriginalNodeProvidingTransform path is skipped.
                    // So, nodeForExportedGlb's TRS should be default/empty here unless explicitly set by pOriginalNodeProvidingTransform.
                    // If we want to be absolutely sure that the exported node for an instanced mesh has identity TRS (because TRS comes from extension)
                    // we can clear it here.
                    nodeForExportedGlb.translation.clear();
                    nodeForExportedGlb.rotation.clear();
                    nodeForExportedGlb.scale.clear();
                    nodeForExportedGlb.matrix.clear();
                    
                    auto instancingIt = pOriginalNodeProvidingInstancing->extensions.find("EXT_mesh_gpu_instancing");
                    if (instancingIt != pOriginalNodeProvidingInstancing->extensions.end()) { // Check if the key exists
                        try {
                            // Cast the std::any payload to nlohmann::json const reference
                            const nlohmann::json& originalInstancingJson = std::any_cast<const nlohmann::json&>(instancingIt->second);

                            if (originalInstancingJson.is_object()) {
                                nlohmann::json newInstancingAttributesJson; // For storing remapped attributes

                                // Check for "attributes" field in the extension JSON
                                if (originalInstancingJson.count("attributes") && originalInstancingJson.at("attributes").is_object()) {
                                    const nlohmann::json& originalAttributes = originalInstancingJson.at("attributes");

                                    // Iterate through the attributes (e.g., TRANSLATION, ROTATION, SCALE)
                                    for (auto attrIt = originalAttributes.items().begin(); attrIt != originalAttributes.items().end(); ++attrIt) {
                                        const std::string& attributeName = attrIt.key();
                                        const nlohmann::json& attributeValue = attrIt.value();

                                        if (attributeValue.is_number_integer()) {
                                            int32_t oldAccessorIndex = attributeValue.get<int32_t>(); // Use get<int32_t>() for direct conversion
                                            
                                            int32_t newAccessorIndex = copyAccessor(*originalGltf, oldAccessorIndex, static_cast<int>(modelIdx), remapping, false);

                                            if (newAccessorIndex >= 0) {
                                                newInstancingAttributesJson[attributeName] = newAccessorIndex;
                                            } else {
                                                logError("Failed to copy accessor " + std::to_string(oldAccessorIndex) + 
                                                         " for EXT_mesh_gpu_instancing attribute " + attributeName + 
                                                         " while segmenting mesh " + std::to_string(meshIdx) + " from node " + pOriginalNodeProvidingInstancing->name);
                                            }
                                        }
                                    }
                                }

                                if (!newInstancingAttributesJson.empty()) {
                                    CesiumGltf::ExtensionExtMeshGpuInstancing newGpuInstancingExtensionStruct;
                                    // Populate attributes from newInstancingAttributesJson (which is nlohmann::json)
                                    for (auto it = newInstancingAttributesJson.items().begin(); it != newInstancingAttributesJson.items().end(); ++it) {
                                        if (it.value().is_number_integer()) {
                                            newGpuInstancingExtensionStruct.attributes[it.key()] = it.value().get<int32_t>();
                                        } else {
                                            logError("EXT_mesh_gpu_instancing: Attribute '" + it.key() + "' for mesh " + std::to_string(meshIdx) + " has non-integer value '" + it.value().dump() + "' during struct conversion. Skipping attribute.");
                                        }
                                    }

                                    // Only add the extension if attributes were successfully populated
                                    if (!newGpuInstancingExtensionStruct.attributes.empty()) {
                                        // The if block for originalInstancingJson.count("extras") is removed here.
                                        
                                        nodeForExportedGlb.extensions["EXT_mesh_gpu_instancing"] = newGpuInstancingExtensionStruct; 

                                        // Add to extensionsUsed and extensionsRequired
                                        bool foundExtUsed = false;
                                        for (const auto& extName : _outputGltf.extensionsUsed) {
                                            if (extName == "EXT_mesh_gpu_instancing") {
                                                foundExtUsed = true;
                                                break;
                                            }
                                        }
                                        if (!foundExtUsed) {
                                            _outputGltf.extensionsUsed.push_back("EXT_mesh_gpu_instancing");
                                            // Check originalGltf->extensionsRequired too
                                            for(const auto& reqExt : originalGltf->extensionsRequired){
                                                if(reqExt == "EXT_mesh_gpu_instancing"){
                                                    bool alreadyRequired = false;
                                                    for(const auto& outReqExt : _outputGltf.extensionsRequired){
                                                        if(outReqExt == "EXT_mesh_gpu_instancing"){
                                                            alreadyRequired = true;
                                                            break;
                                                        }
                                                    }
                                                    if(!alreadyRequired){
                                                        _outputGltf.extensionsRequired.push_back("EXT_mesh_gpu_instancing");
                                                    }
                                                    break; 
                                                }
                                            }
                                        }
                                    } else if (!newInstancingAttributesJson.empty()) {
                                         // This case means newInstancingAttributesJson was not empty initially, but all attributes failed conversion or were skipped.
                                    } else if (originalInstancingJson.count("attributes") && !originalInstancingJson.at("attributes").empty()) {
                                         // This case means newInstancingAttributesJson was empty, but original had attributes.
                                         // This implies all accessor copies failed for the attributes before this stage.
                                    }
                                } else {
                                     // This case means originalInstancingJson didn't have attributes or they were empty, and newInstancingAttributesJson is consequently empty.
                                }
                            } else {
                            }
                        } catch (const std::bad_any_cast& e) {
                            logError("Failed to cast EXT_mesh_gpu_instancing extension content for mesh " + std::to_string(meshIdx) + " from node " + pOriginalNodeProvidingInstancing->name + ". Error: " + std::string(e.what()));
                        } catch (const nlohmann::json::exception& e) {
                            logError("JSON processing error for EXT_mesh_gpu_instancing for mesh " + std::to_string(meshIdx) + " from node " + pOriginalNodeProvidingInstancing->name + ". Error: " + std::string(e.what()));
                        }
                    } else {
                    }
                } else if (!pOriginalNodeProvidingTransform) { 
                }
                
                _outputGltf.nodes.push_back(std::move(nodeForExportedGlb));
                int32_t nodeIndexInOutput = static_cast<int32_t>(_outputGltf.nodes.size() - 1);

                CesiumGltf::Scene scene;
                scene.nodes.push_back(nodeIndexInOutput);
                if (!currentOriginalMesh.name.empty()) {
                     scene.name = "scene_for_" + currentOriginalMesh.name;
                } else {
                     scene.name = "scene_for_mesh_" + std::to_string(meshIdx);
                }
                _outputGltf.scenes.push_back(std::move(scene));
                _outputGltf.scene = static_cast<int32_t>(_outputGltf.scenes.size() - 1);

                if (!_outputGltf.buffers.empty()) {
                    _outputGltf.buffers[0].byteLength = static_cast<int64_t>(_outputBufferData.size());
                } else {
                    logError("CRITICAL: Output GLTF model has no buffers defined after resetInternalState. Skipping GLB write for mesh " + std::to_string(meshIdx));
                    overallSuccess = false;
                    continue;
                }
                
                std::string meshNamePart = currentOriginalMesh.name;
                if (meshNamePart.empty()) {
                    meshNamePart = "mesh_" + std::to_string(meshIdx);
                } else {
                    std::string sanitizedMeshName;
                    for (char c : meshNamePart) {
                        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.') { 
                            sanitizedMeshName += c;
                        } else {
                            sanitizedMeshName += '_'; 
                        }
                    }
                    meshNamePart = sanitizedMeshName;
                }

                std::filesystem::path originalFileStem = loadedModel.originalPath.stem();
                std::filesystem::path outputFileName = originalFileStem.string() + "_" + meshNamePart + ".glb";
                std::filesystem::path outputPath = outputDirectory / outputFileName;

                logMessage("Serializing segmented GLB for: " + outputPath.string());
                gsl::span<const std::byte> bufferSpan(_outputBufferData);
                
                CesiumGltfWriter::GltfWriterOptions writerOptions; 
                CesiumGltfWriter::GltfWriterResult result = _gltfWriter.writeGlb(_outputGltf, bufferSpan, writerOptions);

                for(const auto& error : result.errors) {
                    GltfInstancing::logError("GLB Writer Error (mesh " + std::to_string(meshIdx) + "): " + error);
                }
                for(const auto& warning : result.warnings) {
                    GltfInstancing::logMessage("WARNING: GLB Writer Warning (mesh " + std::to_string(meshIdx) + "): " + warning);
                }

                if (result.gltfBytes.empty()) { 
                    logError("Failed to serialize GLB (mesh " + std::to_string(meshIdx) + ", GltfWriterResult has empty gltfBytes) for: " + outputPath.string());
                    overallSuccess = false;
                } else {
                    std::vector<std::byte> glbBytes = std::move(result.gltfBytes);
                    std::ofstream outFile(outputPath, std::ios::binary);
                    if (!outFile.is_open()) { 
                        logError("Failed to open file for writing (mesh " + std::to_string(meshIdx) + "): " + outputPath.string() + " - Error: " + strerror(errno));
                        overallSuccess = false;
                    } else {
                        outFile.write(reinterpret_cast<const char*>(glbBytes.data()), static_cast<std::streamsize>(glbBytes.size()));
                        if (outFile.fail()) { 
                            logError("Failed to write all data to file (mesh " + std::to_string(meshIdx) + "): " + outputPath.string() + " - Error: " + strerror(errno));
                            overallSuccess = false;
                        }
                        outFile.close(); 
                        if(overallSuccess && !outFile.fail()){ 
                             logMessage("Successfully wrote segmented GLB: " + outputPath.string());
                        }
                    }
                }
            }
        }
        return overallSuccess;
    }
} // namespace GltfInstancing