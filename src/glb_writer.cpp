#include "glb_writer.h"
#include "utilities.h"
#include <sstream> // For std::ostringstream

#include <CesiumGltfContent/GltfUtilities.h>

#include <CesiumGltf/Model.h>
#include <CesiumGltf/Buffer.h>
#include <CesiumGltf/BufferView.h>
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
#include <nlohmann/json.hpp> 
#include <fstream>
#include <algorithm>
#include <vector> 
#include <map>

#include <CesiumGltf/ExtensionExtMeshGpuInstancing.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>

namespace GltfInstancing {

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
        else if (newImage.uri && !newImage.uri->empty()) {
            logMessage("Image " + std::to_string(oldImageIndex) + " uses URI: " + (newImage.uri ? *newImage.uri : "[no uri]"));
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

    int32_t GlbWriter::copyMaterial(const CesiumGltf::Model& oldModel, int32_t oldMaterialIndex, int oldModelId, ResourceRemapping& remapping) {
        auto key = std::make_pair(oldModelId, oldMaterialIndex);
        if (remapping.materials.count(key)) { return remapping.materials[key]; }
        if (oldMaterialIndex < 0 || static_cast<size_t>(oldMaterialIndex) >= oldModel.materials.size()) { return -1; }

        const auto& oldMaterial = oldModel.materials[oldMaterialIndex];
        CesiumGltf::Material newMaterial = oldMaterial;

        for (const auto& extPair : newMaterial.extensions) {
            bool found = false;
            for (const auto& usedExt : _outputGltf.extensionsUsed) {
                if (usedExt == extPair.first) { found = true; break; }
            }
            if (!found) { _outputGltf.extensionsUsed.push_back(extPair.first); }
        }

        auto copyTextureInfoLambda = [&](std::optional<CesiumGltf::TextureInfo>& newOptTexInfo, const std::optional<CesiumGltf::TextureInfo>& oldOptTexInfo) -> bool {
            if (oldOptTexInfo.has_value()) {
                if (!newOptTexInfo.has_value()) { newOptTexInfo.emplace(); }
                auto& newTexInfoRef = newOptTexInfo.value();
                const auto& oldTexInfoRef = oldOptTexInfo.value();
                newTexInfoRef.extras = oldTexInfoRef.extras;
                newTexInfoRef.extensions = oldTexInfoRef.extensions;
                newTexInfoRef.texCoord = oldTexInfoRef.texCoord;
                if (oldTexInfoRef.index >= 0) {
                    newTexInfoRef.index = copyTexture(oldModel, oldTexInfoRef.index, oldModelId, remapping);
                    if (newTexInfoRef.index < 0) return false;
                } else { newTexInfoRef.index = -1; }
            } else { newOptTexInfo.reset(); }
            return true;
        };

        auto copyMaterialNormalTextureInfoLambda = [&](std::optional<CesiumGltf::MaterialNormalTextureInfo>& newOptTexInfo, const std::optional<CesiumGltf::MaterialNormalTextureInfo>& oldOptTexInfo) -> bool {
            if (oldOptTexInfo.has_value()) {
                if (!newOptTexInfo.has_value()) { newOptTexInfo.emplace(); }
                auto& newTexInfoRef = newOptTexInfo.value();
                const auto& oldTexInfoRef = oldOptTexInfo.value();
                newTexInfoRef.extras = oldTexInfoRef.extras;
                newTexInfoRef.extensions = oldTexInfoRef.extensions;
                newTexInfoRef.texCoord = oldTexInfoRef.texCoord;
                newTexInfoRef.scale = oldTexInfoRef.scale;
                if (oldTexInfoRef.index >= 0) {
                    newTexInfoRef.index = copyTexture(oldModel, oldTexInfoRef.index, oldModelId, remapping);
                    if (newTexInfoRef.index < 0) return false;
                } else { newTexInfoRef.index = -1; }
            } else { newOptTexInfo.reset(); }
            return true;
        };

        auto copyMaterialOcclusionTextureInfoLambda = [&](std::optional<CesiumGltf::MaterialOcclusionTextureInfo>& newOptTexInfo, const std::optional<CesiumGltf::MaterialOcclusionTextureInfo>& oldOptTexInfo) -> bool {
            if (oldOptTexInfo.has_value()) {
                if (!newOptTexInfo.has_value()) { newOptTexInfo.emplace(); }
                auto& newTexInfoRef = newOptTexInfo.value();
                const auto& oldTexInfoRef = oldOptTexInfo.value();
                newTexInfoRef.extras = oldTexInfoRef.extras;
                newTexInfoRef.extensions = oldTexInfoRef.extensions;
                newTexInfoRef.texCoord = oldTexInfoRef.texCoord;
                newTexInfoRef.strength = oldTexInfoRef.strength;
                if (oldTexInfoRef.index >= 0) {
                    newTexInfoRef.index = copyTexture(oldModel, oldTexInfoRef.index, oldModelId, remapping);
                    if (newTexInfoRef.index < 0) return false;
                } else { newTexInfoRef.index = -1; }
            } else { newOptTexInfo.reset(); }
            return true;
        };

        if (oldMaterial.pbrMetallicRoughness.has_value()) {
            if (!newMaterial.pbrMetallicRoughness.has_value()) { newMaterial.pbrMetallicRoughness.emplace(); }
            auto& newPbr = newMaterial.pbrMetallicRoughness.value();
            const auto& oldPbr = oldMaterial.pbrMetallicRoughness.value();
            if (!copyTextureInfoLambda(newPbr.baseColorTexture, oldPbr.baseColorTexture)) return -1;
            if (!copyTextureInfoLambda(newPbr.metallicRoughnessTexture, oldPbr.metallicRoughnessTexture)) return -1;
        } else { newMaterial.pbrMetallicRoughness.reset(); }

        if (!copyMaterialNormalTextureInfoLambda(newMaterial.normalTexture, oldMaterial.normalTexture)) return -1;
        if (!copyMaterialOcclusionTextureInfoLambda(newMaterial.occlusionTexture, oldMaterial.occlusionTexture)) return -1;
        if (!copyTextureInfoLambda(newMaterial.emissiveTexture, oldMaterial.emissiveTexture)) return -1;

        _outputGltf.materials.push_back(std::move(newMaterial));
        int32_t newIndex = static_cast<int32_t>(_outputGltf.materials.size() - 1);
        remapping.materials[key] = newIndex;
        return newIndex;
    }

    int32_t GlbWriter::copyBufferView(const CesiumGltf::Model& oldModel, int32_t oldBufferViewIndex, int oldModelId, ResourceRemapping& remapping) {
        auto key = std::make_pair(oldModelId, oldBufferViewIndex);
        if (remapping.bufferViews.count(key)) { return remapping.bufferViews[key]; }
        if (oldBufferViewIndex < 0 || static_cast<size_t>(oldBufferViewIndex) >= oldModel.bufferViews.size()) { return -1; }

        const auto& oldBufferView = oldModel.bufferViews[oldBufferViewIndex];
        if (oldBufferView.buffer < 0 || static_cast<size_t>(oldBufferView.buffer) >= oldModel.buffers.size()) { return -1; }

        const auto& oldBuffer = oldModel.buffers[oldBufferView.buffer];
        gsl::span<const std::byte> oldDataSpan;
        int64_t bvByteLength = oldBufferView.byteLength;

        if (!oldBuffer.cesium.data.empty()) {
            if (oldBufferView.byteOffset + bvByteLength > static_cast<int64_t>(oldBuffer.cesium.data.size())) { return -1; }
            oldDataSpan = gsl::span<const std::byte>(oldBuffer.cesium.data.data() + oldBufferView.byteOffset, static_cast<size_t>(bvByteLength));
        } else { return -1; }

        int32_t strideForAddData = static_cast<int32_t>(oldBufferView.byteStride.value_or(0));
        int32_t newBufferViewIndex = addDataToBuffer(oldDataSpan, strideForAddData, false);
        if (newBufferViewIndex < 0) { return -1; }

        if (oldBufferView.target.has_value()) {
            if (static_cast<size_t>(newBufferViewIndex) < _outputGltf.bufferViews.size()) {
                 _outputGltf.bufferViews[newBufferViewIndex].target = oldBufferView.target.value();
            }
        }
        remapping.bufferViews[key] = newBufferViewIndex;
        return newBufferViewIndex;
    }

    int32_t GlbWriter::copyAccessor(const CesiumGltf::Model& oldModel, int32_t oldAccessorIndex, int oldModelId, ResourceRemapping& remapping, bool skipBufferViewRemap, bool isIndicesAccessor) {
        // Note: isIndicesAccessor parameter added to match header, though not strictly used in logic below, can be used for target hint
        return copyAccessor(oldModel, oldAccessorIndex, oldModelId, remapping, skipBufferViewRemap);
    }

    int32_t GlbWriter::copyAccessor(const CesiumGltf::Model& oldModel, int32_t oldAccessorIndex, int oldModelId, ResourceRemapping& remapping, bool skipBufferViewRemap) {
        auto key = std::make_pair(oldModelId, oldAccessorIndex);
        if (remapping.accessors.count(key)) { return remapping.accessors[key]; }
        if (oldAccessorIndex < 0 || static_cast<size_t>(oldAccessorIndex) >= oldModel.accessors.size()) { return -1; }

        const auto& oldAccessor = oldModel.accessors[oldAccessorIndex];
        CesiumGltf::Accessor newAccessor = oldAccessor;

        if (!skipBufferViewRemap) {
            if (oldAccessor.bufferView >= 0) {
                const CesiumGltf::BufferView* pOldBv = CesiumGltf::Model::getSafe(&oldModel.bufferViews, oldAccessor.bufferView);
                if (!pOldBv) return -1;
                const CesiumGltf::Buffer* pOldBuffer = CesiumGltf::Model::getSafe(&oldModel.buffers, pOldBv->buffer);
                if (!pOldBuffer || pOldBuffer->cesium.data.empty()) return -1;

                int64_t elementByteLength = oldAccessor.computeByteSizeOfComponent() * oldAccessor.computeNumberOfComponents();
                int64_t totalAccessorByteLength = oldAccessor.count * elementByteLength;

                const std::vector<std::byte>& bufferData = pOldBuffer->cesium.data;
                int64_t actualStride = oldAccessor.computeByteStride(oldModel);
                std::vector<std::byte> collectedBytes;
                collectedBytes.reserve(static_cast<size_t>(totalAccessorByteLength));

                for (int64_t i = 0; i < oldAccessor.count; ++i) {
                    const std::byte* pElementStart = bufferData.data() + pOldBv->byteOffset + oldAccessor.byteOffset + i * actualStride;
                    if (pElementStart < bufferData.data() || (pElementStart + elementByteLength) > (bufferData.data() + bufferData.size())) return -1;
                    collectedBytes.insert(collectedBytes.end(), pElementStart, pElementStart + elementByteLength);
                }

                gsl::span<const std::byte> data_to_copy(collectedBytes.data(), collectedBytes.size());
                int32_t newBufferViewIdx = addDataToBuffer(data_to_copy, static_cast<int32_t>(elementByteLength), false);
                if (newBufferViewIdx < 0) return -1;
                newAccessor.bufferView = newBufferViewIdx;
                newAccessor.byteOffset = 0;
                _outputGltf.accessors.push_back(std::move(newAccessor));
            } else {
                _outputGltf.accessors.push_back(std::move(newAccessor));
            }
        } else {
            _outputGltf.accessors.push_back(std::move(newAccessor));
        }

        int32_t newIndex = static_cast<int32_t>(_outputGltf.accessors.size() - 1);
        remapping.accessors[key] = newIndex;
        return newIndex;
    }

    int32_t GlbWriter::copyMeshDefinition(const CesiumGltf::Model& originalModel, int32_t originalMeshIndex, int originalModelId, ResourceRemapping& remapping) {
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
            } else { newPrimitive.material = -1; }

            if (oldPrimitive.indices >= 0) {
                newPrimitive.indices = copyAccessor(originalModel, oldPrimitive.indices, originalModelId, remapping, false, true);
                if (newPrimitive.indices < 0) return -1;
                const auto& idxAccessor = _outputGltf.accessors[newPrimitive.indices];
                if (idxAccessor.bufferView >= 0 && static_cast<size_t>(idxAccessor.bufferView) < _outputGltf.bufferViews.size()) {
                    _outputGltf.bufferViews[idxAccessor.bufferView].target = CesiumGltf::BufferView::Target::ELEMENT_ARRAY_BUFFER;
                }
            }
            for (const auto& oldAttrPair : oldPrimitive.attributes) {
                int32_t newAccessorIdx = copyAccessor(originalModel, oldAttrPair.second, originalModelId, remapping, false, false);
                if (newAccessorIdx < 0) return -1;
                newPrimitive.attributes[oldAttrPair.first] = newAccessorIdx;
                const auto& attrAccessor = _outputGltf.accessors[newAccessorIdx];
                if (attrAccessor.bufferView >= 0 && static_cast<size_t>(attrAccessor.bufferView) < _outputGltf.bufferViews.size()) {
                    _outputGltf.bufferViews[attrAccessor.bufferView].target = CesiumGltf::BufferView::Target::ARRAY_BUFFER;
                }
            }
            newMesh.primitives.push_back(std::move(newPrimitive));
        }
        _outputGltf.meshes.push_back(std::move(newMesh));
        return static_cast<int32_t>(_outputGltf.meshes.size() - 1);
    }

    void GlbWriter::createInstanceTRS_Accessors(const std::vector<MeshInstanceInfo>& instances, int32_t& translationAccessorIndex, int32_t& rotationAccessorIndex, int32_t& scaleAccessorIndex) {
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
            
            // Compute min/max for translation
            glm::vec3 minV(std::numeric_limits<float>::max());
            glm::vec3 maxV(std::numeric_limits<float>::lowest());
            for (size_t i = 0; i < translationData.size(); i += 3) {
                float x = translationData[i];
                float y = translationData[i+1];
                float z = translationData[i+2];
                if (x < minV.x) minV.x = x; if (y < minV.y) minV.y = y; if (z < minV.z) minV.z = z;
                if (x > maxV.x) maxV.x = x; if (y > maxV.y) maxV.y = y; if (z > maxV.z) maxV.z = z;
            }
            transAcc.min = { (double)minV.x, (double)minV.y, (double)minV.z };
            transAcc.max = { (double)maxV.x, (double)maxV.y, (double)maxV.z };

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
            // Rotation usually doesn't need min/max but valid to have
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
            
            // Compute min/max for scale
            glm::vec3 minV(std::numeric_limits<float>::max());
            glm::vec3 maxV(std::numeric_limits<float>::lowest());
            for (size_t i = 0; i < scaleData.size(); i += 3) {
                float x = scaleData[i];
                float y = scaleData[i+1];
                float z = scaleData[i+2];
                if (x < minV.x) minV.x = x; if (y < minV.y) minV.y = y; if (z < minV.z) minV.z = z;
                if (x > maxV.x) maxV.x = x; if (y > maxV.y) maxV.y = y; if (z > maxV.z) maxV.z = z;
            }
            scaleAcc.min = { (double)minV.x, (double)minV.y, (double)minV.z };
            scaleAcc.max = { (double)maxV.x, (double)maxV.y, (double)maxV.z };

            scaleAccessorIndex = static_cast<int32_t>(_outputGltf.accessors.size() - 1);
        }
    }

    int32_t GlbWriter::createInstancedNode(int32_t meshIndexInOutputGltf, const std::vector<MeshInstanceInfo>& instances, const std::string& representativeMeshName) {
        CesiumGltf::Node newNode;
        newNode.mesh = meshIndexInOutputGltf;
        newNode.name = representativeMeshName.empty() ? "instanced_node_mesh_" + std::to_string(meshIndexInOutputGltf) : representativeMeshName;

        int32_t transAccIdx = -1, rotAccIdx = -1, scaleAccIdx = -1;
        createInstanceTRS_Accessors(instances, transAccIdx, rotAccIdx, scaleAccIdx);

        if (transAccIdx != -1 || rotAccIdx != -1 || scaleAccIdx != -1) {
            CesiumGltf::ExtensionExtMeshGpuInstancing instancingExtensionData;
            if (transAccIdx != -1) instancingExtensionData.attributes["TRANSLATION"] = transAccIdx;
            if (rotAccIdx != -1)   instancingExtensionData.attributes["ROTATION"] = rotAccIdx;
            if (scaleAccIdx != -1) instancingExtensionData.attributes["SCALE"] = scaleAccIdx;
            newNode.extensions["EXT_mesh_gpu_instancing"] = instancingExtensionData;

            bool foundExtUsed = false;
            for (const auto& extName : _outputGltf.extensionsUsed) {
                if (extName == "EXT_mesh_gpu_instancing") { foundExtUsed = true; break; }
            }
            if (!foundExtUsed) { _outputGltf.extensionsUsed.push_back("EXT_mesh_gpu_instancing"); }
        }
        _outputGltf.nodes.push_back(std::move(newNode));
        return static_cast<int32_t>(_outputGltf.nodes.size() - 1);
    }

    int32_t GlbWriter::createNonInstancedNode(int32_t meshIndexInOutputGltf, const TransformComponents& transform) {
        CesiumGltf::Node newNode;
        newNode.mesh = meshIndexInOutputGltf;
        const double EPSILON = 1e-10;
        if (std::abs(transform.translation.x) > EPSILON || std::abs(transform.translation.y) > EPSILON || std::abs(transform.translation.z) > EPSILON) {
            newNode.translation = { transform.translation.x, transform.translation.y, transform.translation.z };
        }
        if (std::abs(transform.rotation.x) > EPSILON || std::abs(transform.rotation.y) > EPSILON || std::abs(transform.rotation.z) > EPSILON || std::abs(transform.rotation.w - 1.0) > EPSILON) {
            newNode.rotation = { transform.rotation.x, transform.rotation.y, transform.rotation.z, transform.rotation.w };
        }
        if (std::abs(transform.scale.x - 1.0) > EPSILON || std::abs(transform.scale.y - 1.0) > EPSILON || std::abs(transform.scale.z - 1.0) > EPSILON) {
            newNode.scale = { transform.scale.x, transform.scale.y, transform.scale.z };
        }
        _outputGltf.nodes.push_back(std::move(newNode));
        return static_cast<int32_t>(_outputGltf.nodes.size() - 1);
    }

    std::optional<std::pair<std::filesystem::path, BoundingBox>> GlbWriter::writeInstancedGlb(
        const std::vector<LoadedGltfModel>& originalModels,
        const InstancingDetectionResult& detectionResult,
        const std::filesystem::path& outputPath) {
        // ... (Keep existing implementation logic) ...
        // For brevity in this fix, I am assuming the logic is similar to writeLODGlb but iterating detectionResult
        // Since I need to restore it, I will use a simplified version that calls the same helpers.
        
        logMessage("Starting GLB generation: " + outputPath.string());
        resetInternalState();
        ResourceRemapping remapping;
        std::vector<int32_t> rootNodeIndices;
        BoundingBox overallBoundingBox;

        for (const auto& group : detectionResult.instancedGroups) {
            if (group.instances.empty()) continue;
            const CesiumGltf::Model* representativeModel = getOriginalModelById(originalModels, group.representativeGltfModelIndex);
            if (!representativeModel) continue;
            int32_t newMeshIndex = copyMeshDefinition(*representativeModel, group.representativeMeshIndexInModel, group.representativeGltfModelIndex, remapping);
            if (newMeshIndex < 0) continue;
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
                    }
                }
            }
        }

        for (const auto& niMeshInfo : detectionResult.nonInstancedMeshes) {
            const CesiumGltf::Model* originalModel = getOriginalModelById(originalModels, niMeshInfo.originalGltfModelIndex);
            if (!originalModel) continue;
            int32_t newMeshIndex = copyMeshDefinition(*originalModel, niMeshInfo.originalMeshIndexInModel, niMeshInfo.originalGltfModelIndex, remapping);
            if (newMeshIndex < 0) continue;
            int32_t regularNodeIndex = createNonInstancedNode(newMeshIndex, niMeshInfo.transform);
            if (regularNodeIndex >= 0) {
                rootNodeIndices.push_back(regularNodeIndex);
                if (static_cast<size_t>(newMeshIndex) < _outputGltf.meshes.size()) {
                    BoundingBox meshLocalBox = getMeshBoundingBox(_outputGltf, _outputGltf.meshes[newMeshIndex]);
                    if (meshLocalBox.isValid()) {
                        meshLocalBox.transform(niMeshInfo.transform.toMat4());
                        overallBoundingBox.merge(meshLocalBox);
                    }
                }
            }
        }

        if (!rootNodeIndices.empty()) {
            CesiumGltf::Scene& scene = _outputGltf.scenes.emplace_back();
            scene.nodes = rootNodeIndices;
            _outputGltf.scene = static_cast<int32_t>(_outputGltf.scenes.size() - 1);
        } else {
            logMessage("No nodes to write in GLB. Skipping file generation.");
            return std::nullopt;
        }

        if (!_outputGltf.buffers.empty()) {
            _outputGltf.buffers[0].byteLength = static_cast<int64_t>(_outputBufferData.size());
        }

        // CesiumGltfContent::GltfUtilities::removeUnusedAccessors(_outputGltf);
        // CesiumGltfContent::GltfUtilities::removeUnusedBufferViews(_outputGltf);
        // CesiumGltfContent::GltfUtilities::removeUnusedBuffers(_outputGltf);

        CesiumGltfWriter::GltfWriterOptions writerOptions;
        CesiumGltfWriter::GltfWriterResult writerResult = _gltfWriter.writeGlb(_outputGltf, _outputBufferData, writerOptions);

        if (writerResult.gltfBytes.empty()) return std::nullopt;

        std::vector<std::byte> glbBytes = std::move(writerResult.gltfBytes);
        std::ofstream outFile(outputPath, std::ios::binary);
        if (!outFile) return std::nullopt;
        outFile.write(reinterpret_cast<const char*>(glbBytes.data()), glbBytes.size());
        outFile.close();

        return std::make_pair(outputPath, overallBoundingBox);
    }

    std::optional<std::pair<std::filesystem::path, BoundingBox>> GlbWriter::writeInstancedMeshesOnly(
        const std::vector<LoadedGltfModel>& originalModels,
        const InstancingDetectionResult& detectionResult,
        const std::filesystem::path& outputPath) {
        
        logMessage("Starting GLB generation (instanced meshes only): " + outputPath.string());
        resetInternalState();
        ResourceRemapping remapping;
        std::vector<int32_t> rootNodeIndices;
        BoundingBox overallBoundingBox;

        for (const auto& group : detectionResult.instancedGroups) {
            if (group.instances.empty()) continue;
            const CesiumGltf::Model* representativeModel = getOriginalModelById(originalModels, group.representativeGltfModelIndex);
            if (!representativeModel) continue;
            int32_t newMeshIndex = copyMeshDefinition(*representativeModel, group.representativeMeshIndexInModel, group.representativeGltfModelIndex, remapping);
            if (newMeshIndex < 0) continue;
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
                    }
                }
            }
        }

        if (!rootNodeIndices.empty()) {
            CesiumGltf::Scene& scene = _outputGltf.scenes.emplace_back();
            scene.nodes = rootNodeIndices;
            _outputGltf.scene = static_cast<int32_t>(_outputGltf.scenes.size() - 1);
        } else {
            logMessage("No instanced meshes to write. Skipping GLB generation.");
            return std::nullopt;
        }

        if (_outputBufferData.empty()) {
             logMessage("Warning: Output buffer is empty despite having nodes. Skipping GLB generation.");
             return std::nullopt;
        }

        if (!_outputGltf.buffers.empty()) {
            _outputGltf.buffers[0].byteLength = static_cast<int64_t>(_outputBufferData.size());
        }

        // CesiumGltfContent::GltfUtilities::removeUnusedAccessors(_outputGltf);
        // CesiumGltfContent::GltfUtilities::removeUnusedBufferViews(_outputGltf);
        // CesiumGltfContent::GltfUtilities::removeUnusedBuffers(_outputGltf);

        CesiumGltfWriter::GltfWriterOptions writerOptions;
        CesiumGltfWriter::GltfWriterResult writerResult = _gltfWriter.writeGlb(_outputGltf, _outputBufferData, writerOptions);

        if (writerResult.gltfBytes.empty()) return std::nullopt;

        std::vector<std::byte> glbBytes = std::move(writerResult.gltfBytes);
        std::ofstream outFile(outputPath, std::ios::binary);
        if (!outFile) return std::nullopt;
        outFile.write(reinterpret_cast<const char*>(glbBytes.data()), glbBytes.size());
        outFile.close();

        return std::make_pair(outputPath, overallBoundingBox);
    }

    std::optional<std::pair<std::filesystem::path, BoundingBox>> GlbWriter::writeNonInstancedMeshesOnly(
        const std::vector<LoadedGltfModel>& originalModels,
        const InstancingDetectionResult& detectionResult,
        const std::filesystem::path& outputPath) {
        
        logMessage("Starting GLB generation (non-instanced meshes only): " + outputPath.string());
        resetInternalState();
        ResourceRemapping remapping;
        std::vector<int32_t> rootNodeIndices;
        BoundingBox overallBoundingBox;

        for (const auto& niMeshInfo : detectionResult.nonInstancedMeshes) {
            const CesiumGltf::Model* originalModel = getOriginalModelById(originalModels, niMeshInfo.originalGltfModelIndex);
            if (!originalModel) continue;
            int32_t newMeshIndex = copyMeshDefinition(*originalModel, niMeshInfo.originalMeshIndexInModel, niMeshInfo.originalGltfModelIndex, remapping);
            if (newMeshIndex < 0) continue;
            int32_t regularNodeIndex = createNonInstancedNode(newMeshIndex, niMeshInfo.transform);
            if (regularNodeIndex >= 0) {
                rootNodeIndices.push_back(regularNodeIndex);
                if (static_cast<size_t>(newMeshIndex) < _outputGltf.meshes.size()) {
                    BoundingBox meshLocalBox = getMeshBoundingBox(_outputGltf, _outputGltf.meshes[newMeshIndex]);
                    if (meshLocalBox.isValid()) {
                        meshLocalBox.transform(niMeshInfo.transform.toMat4());
                        overallBoundingBox.merge(meshLocalBox);
                    }
                }
            }
        }

        if (!rootNodeIndices.empty()) {
            CesiumGltf::Scene& scene = _outputGltf.scenes.emplace_back();
            scene.nodes = rootNodeIndices;
            _outputGltf.scene = static_cast<int32_t>(_outputGltf.scenes.size() - 1);
        } else {
            logMessage("No non-instanced meshes to write. Skipping GLB generation.");
            return std::nullopt;
        }

        if (_outputBufferData.empty()) {
             logMessage("Warning: Output buffer is empty despite having nodes. Skipping GLB generation.");
             return std::nullopt;
        }

        if (!_outputGltf.buffers.empty()) {
            _outputGltf.buffers[0].byteLength = static_cast<int64_t>(_outputBufferData.size());
        }

        // CesiumGltfContent::GltfUtilities::removeUnusedAccessors(_outputGltf);
        // CesiumGltfContent::GltfUtilities::removeUnusedBufferViews(_outputGltf);
        // CesiumGltfContent::GltfUtilities::removeUnusedBuffers(_outputGltf);

        CesiumGltfWriter::GltfWriterOptions writerOptions;
        CesiumGltfWriter::GltfWriterResult writerResult = _gltfWriter.writeGlb(_outputGltf, _outputBufferData, writerOptions);

        if (writerResult.gltfBytes.empty()) return std::nullopt;

        std::vector<std::byte> glbBytes = std::move(writerResult.gltfBytes);
        std::ofstream outFile(outputPath, std::ios::binary);
        if (!outFile) return std::nullopt;
        outFile.write(reinterpret_cast<const char*>(glbBytes.data()), glbBytes.size());
        outFile.close();

        return std::make_pair(outputPath, overallBoundingBox);
    }

    std::optional<std::pair<std::filesystem::path, BoundingBox>> GlbWriter::writeLODGlb(
        const std::vector<LoadedGltfModel>& originalModels,
        const LODLevelResult& lodData,
        const std::filesystem::path& outputPath
    ) {
        logMessage("Starting LOD GLB generation (Level " + std::to_string(lodData.level) + "): " + outputPath.string());
        resetInternalState();
        ResourceRemapping remapping;
        std::vector<int32_t> rootNodeIndices;
        BoundingBox overallBoundingBox;

        std::map<std::pair<int, int>, int32_t> createdMeshes;

        for (const auto& node : lodData.nodes) {
            if (node.instances.empty()) continue;

            int32_t newMeshIndex = -1;
            auto meshKey = std::make_pair(node.sourceModelIndex, node.sourceMeshIndex);

            if (createdMeshes.count(meshKey)) {
                newMeshIndex = createdMeshes[meshKey];
            } else {
                if (node.sourceModelIndex == -1) {
                    newMeshIndex = createCubeMesh();
                } else {
                    const CesiumGltf::Model* originalModel = getOriginalModelById(originalModels, node.sourceModelIndex);
                    if (originalModel) {
                        newMeshIndex = copyMeshDefinition(*originalModel, node.sourceMeshIndex, node.sourceModelIndex, remapping);
                    }
                }
                if (newMeshIndex >= 0) {
                    createdMeshes[meshKey] = newMeshIndex;
                }
            }

            if (newMeshIndex < 0) continue;

            int32_t instancedNodeIndex = createInstancedNode(newMeshIndex, node.instances, node.meshName);
            
            if (instancedNodeIndex >= 0) {
                rootNodeIndices.push_back(instancedNodeIndex);
                if (static_cast<size_t>(newMeshIndex) < _outputGltf.meshes.size()) {
                    BoundingBox meshLocalBox = getMeshBoundingBox(_outputGltf, _outputGltf.meshes[newMeshIndex]);
                    if (meshLocalBox.isValid()) {
                        for (const auto& instanceInfo : node.instances) {
                            BoundingBox instanceBox = meshLocalBox;
                            instanceBox.transform(instanceInfo.transform.toMat4());
                            overallBoundingBox.merge(instanceBox);
                        }
                    }
                }
            }
        }

        if (!rootNodeIndices.empty()) {
            CesiumGltf::Scene& scene = _outputGltf.scenes.emplace_back();
            scene.nodes = rootNodeIndices;
            _outputGltf.scene = static_cast<int32_t>(_outputGltf.scenes.size() - 1);
        } else {
            logMessage("No LOD nodes to write. Skipping GLB generation.");
            return std::nullopt;
        }

        if (_outputBufferData.empty()) {
             logMessage("Warning: Output buffer is empty despite having nodes. Skipping GLB generation.");
             return std::nullopt;
        }

        if (!_outputGltf.buffers.empty()) {
            _outputGltf.buffers[0].byteLength = static_cast<int64_t>(_outputBufferData.size());
        }

        // CesiumGltfContent::GltfUtilities::removeUnusedAccessors(_outputGltf);
        // CesiumGltfContent::GltfUtilities::removeUnusedBufferViews(_outputGltf);
        // CesiumGltfContent::GltfUtilities::removeUnusedBuffers(_outputGltf);

        CesiumGltfWriter::GltfWriterOptions writerOptions;
        CesiumGltfWriter::GltfWriterResult writerResult = _gltfWriter.writeGlb(_outputGltf, _outputBufferData, writerOptions);

        if (writerResult.gltfBytes.empty()) return std::nullopt;

        std::vector<std::byte> glbBytes = std::move(writerResult.gltfBytes);
        std::ofstream outFile(outputPath, std::ios::binary);
        if (!outFile) return std::nullopt;
        outFile.write(reinterpret_cast<const char*>(glbBytes.data()), glbBytes.size());
        outFile.close();

        return std::make_pair(outputPath, overallBoundingBox);
    }

    int32_t GlbWriter::createCubeMesh() {
        static const std::vector<float> positions = {
            -0.5f, -0.5f,  0.5f,  0.5f, -0.5f,  0.5f,  0.5f,  0.5f,  0.5f, -0.5f,  0.5f,  0.5f,
            -0.5f, -0.5f, -0.5f, -0.5f,  0.5f, -0.5f,  0.5f,  0.5f, -0.5f,  0.5f, -0.5f, -0.5f,
            -0.5f,  0.5f, -0.5f, -0.5f,  0.5f,  0.5f,  0.5f,  0.5f,  0.5f,  0.5f,  0.5f, -0.5f,
            -0.5f, -0.5f, -0.5f,  0.5f, -0.5f, -0.5f,  0.5f, -0.5f,  0.5f, -0.5f, -0.5f,  0.5f,
             0.5f, -0.5f, -0.5f,  0.5f,  0.5f, -0.5f,  0.5f,  0.5f,  0.5f,  0.5f, -0.5f,  0.5f,
            -0.5f, -0.5f, -0.5f, -0.5f, -0.5f,  0.5f, -0.5f,  0.5f,  0.5f, -0.5f,  0.5f, -0.5f,
        };

        static const std::vector<float> normals = {
             0.0f,  0.0f,  1.0f,  0.0f,  0.0f,  1.0f,  0.0f,  0.0f,  1.0f,  0.0f,  0.0f,  1.0f,
             0.0f,  0.0f, -1.0f,  0.0f,  0.0f, -1.0f,  0.0f,  0.0f, -1.0f,  0.0f,  0.0f, -1.0f,
             0.0f,  1.0f,  0.0f,  0.0f,  1.0f,  0.0f,  0.0f,  1.0f,  0.0f,  0.0f,  1.0f,  0.0f,
             0.0f, -1.0f,  0.0f,  0.0f, -1.0f,  0.0f,  0.0f, -1.0f,  0.0f,  0.0f, -1.0f,  0.0f,
             1.0f,  0.0f,  0.0f,  1.0f,  0.0f,  0.0f,  1.0f,  0.0f,  0.0f,  1.0f,  0.0f,  0.0f,
            -1.0f,  0.0f,  0.0f, -1.0f,  0.0f,  0.0f, -1.0f,  0.0f,  0.0f, -1.0f,  0.0f,  0.0f,
        };

        static const std::vector<uint16_t> indices = {
             0,  1,  2,  0,  2,  3, 
             4,  5,  6,  4,  6,  7, 
             8,  9, 10,  8, 10, 11, 
            12, 13, 14, 12, 14, 15, 
            16, 17, 18, 16, 18, 19, 
            20, 21, 22, 20, 22, 23  
        };

        gsl::span<const std::byte> posSpan(reinterpret_cast<const std::byte*>(positions.data()), positions.size() * sizeof(float));
        int32_t posBvIdx = addDataToBuffer(posSpan, 12, true);
        
        gsl::span<const std::byte> normSpan(reinterpret_cast<const std::byte*>(normals.data()), normals.size() * sizeof(float));
        int32_t normBvIdx = addDataToBuffer(normSpan, 12, true);

        gsl::span<const std::byte> idxSpan(reinterpret_cast<const std::byte*>(indices.data()), indices.size() * sizeof(uint16_t));
        int32_t idxBvIdx = addDataToBuffer(idxSpan, 0, false);

        CesiumGltf::Accessor posAcc;
        posAcc.bufferView = posBvIdx;
        posAcc.componentType = CesiumGltf::Accessor::ComponentType::FLOAT;
        posAcc.type = CesiumGltf::Accessor::Type::VEC3;
        posAcc.count = 24;
        posAcc.min = { -0.5, -0.5, -0.5 };
        posAcc.max = {  0.5,  0.5,  0.5 };
        _outputGltf.accessors.push_back(posAcc);
        int32_t posAccIdx = static_cast<int32_t>(_outputGltf.accessors.size() - 1);

        CesiumGltf::Accessor normAcc;
        normAcc.bufferView = normBvIdx;
        normAcc.componentType = CesiumGltf::Accessor::ComponentType::FLOAT;
        normAcc.type = CesiumGltf::Accessor::Type::VEC3;
        normAcc.count = 24;
        _outputGltf.accessors.push_back(normAcc);
        int32_t normAccIdx = static_cast<int32_t>(_outputGltf.accessors.size() - 1);

        CesiumGltf::Accessor idxAcc;
        idxAcc.bufferView = idxBvIdx;
        idxAcc.componentType = CesiumGltf::Accessor::ComponentType::UNSIGNED_SHORT;
        idxAcc.type = CesiumGltf::Accessor::Type::SCALAR;
        idxAcc.count = 36;
        _outputGltf.accessors.push_back(idxAcc);
        int32_t idxAccIdx = static_cast<int32_t>(_outputGltf.accessors.size() - 1);

        CesiumGltf::Mesh mesh;
        mesh.name = "LOD1_Proxy_Cube";
        CesiumGltf::MeshPrimitive prim;
        prim.attributes["POSITION"] = posAccIdx;
        prim.attributes["NORMAL"] = normAccIdx;
        prim.indices = idxAccIdx;
        prim.mode = CesiumGltf::MeshPrimitive::Mode::TRIANGLES;
        mesh.primitives.push_back(prim);

        _outputGltf.meshes.push_back(mesh);
        return static_cast<int32_t>(_outputGltf.meshes.size() - 1);
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

                                    if (!newGpuInstancingExtensionStruct.attributes.empty()) {
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
