#include "semantic_hausdorff_detector.h"
#include "utilities.h"
#include <CesiumGltf/ExtensionExtMeshGpuInstancing.h>
#include <CesiumGltf/AccessorView.h>
#include <glm/gtc/matrix_transform.hpp>
#include <sstream>
#include <algorithm>

namespace GltfInstancing {

    static std::vector<std::string> splitAndTrim(const std::string& s, char delim) {
        std::vector<std::string> out;
        std::istringstream ss(s);
        std::string part;
        while (std::getline(ss, part, delim)) {
            part.erase(0, part.find_first_not_of(" \t\r\n"));
            part.erase(part.find_last_not_of(" \t\r\n") + 1);
            if (!part.empty()) out.push_back(part);
        }
        return out;
    }

    SemanticHausdorffInstancingDetector::SemanticHausdorffInstancingDetector(
        const SemanticParser* semanticParser,
        const std::string& semanticHashFields,
        double similarityThreshold,
        int instanceLimit)
        : _semanticParser(semanticParser)
        , _similarityThreshold(std::max(0.0, std::min(1.0, similarityThreshold)))
        , _instanceLimit(instanceLimit > 0 ? instanceLimit : 2)
    {
        _semanticHashFieldNames = splitAndTrim(semanticHashFields, ',');
        if (_semanticHashFieldNames.empty()) {
            _semanticHashFieldNames = { "category", "family", "type" };
        }
    }

    std::string SemanticHausdorffInstancingDetector::buildSemanticHashKey(const std::optional<SemanticInfo>& info) const {
        if (!info.has_value()) return "unknown";
        std::string key;
        for (const auto& field : _semanticHashFieldNames) {
            if (field == "category") key += info->category + "|";
            else if (field == "family") key += info->family + "|";
            else if (field == "type") key += info->type + "|";
        }
        return key.empty() ? "unknown" : key;
    }

    void SemanticHausdorffInstancingDetector::traverseNode(
        const LoadedGltfModel& loadedGltf,
        int32_t modelIndexInLoadedModels,
        int32_t nodeIndex,
        const glm::dmat4& currentWorldTransform,
        std::map<std::string, std::vector<std::pair<std::pair<int32_t, int32_t>, MeshInstanceInfo>>>& semanticGroups,
        std::vector<int32_t>& parentNodeIndicesChainForChildren)
    {
        if (nodeIndex < 0 || static_cast<size_t>(nodeIndex) >= loadedGltf.model.nodes.size()) return;
        const CesiumGltf::Node& node = loadedGltf.model.nodes[nodeIndex];
        glm::dmat4 localTransform = getLocalTransformMatrix(node);
        glm::dmat4 worldTransform = currentWorldTransform * localTransform;

        if (node.mesh >= 0 && static_cast<size_t>(node.mesh) < loadedGltf.model.meshes.size()) {
            const CesiumGltf::Mesh& mesh = loadedGltf.model.meshes[node.mesh];
            std::string meshHashId = mesh.name.empty() ? "" : mesh.name;
            std::string glbStem = loadedGltf.originalPath.empty() ? "" : loadedGltf.originalPath.stem().string();
            auto semanticInfo = _semanticParser ? _semanticParser->getSemanticInfo(glbStem, meshHashId) : std::nullopt;
            std::string semanticKey = buildSemanticHashKey(semanticInfo);

            auto instancingExtIt = node.extensions.find("EXT_mesh_gpu_instancing");
            if (instancingExtIt != node.extensions.end()) {
                try {
                    const auto* extData = std::any_cast<CesiumGltf::ExtensionExtMeshGpuInstancing>(&instancingExtIt->second);
                    if (extData) {
                        int32_t transAccIdx = extData->attributes.count("TRANSLATION") ? extData->attributes.at("TRANSLATION") : -1;
                        int32_t rotAccIdx = extData->attributes.count("ROTATION") ? extData->attributes.at("ROTATION") : -1;
                        int32_t scaleAccIdx = extData->attributes.count("SCALE") ? extData->attributes.at("SCALE") : -1;
                        int64_t instanceCount = 0;
                        if (transAccIdx >= 0 && static_cast<size_t>(transAccIdx) < loadedGltf.model.accessors.size())
                            instanceCount = loadedGltf.model.accessors[transAccIdx].count;
                        else if (rotAccIdx >= 0 && static_cast<size_t>(rotAccIdx) < loadedGltf.model.accessors.size())
                            instanceCount = loadedGltf.model.accessors[rotAccIdx].count;
                        else if (scaleAccIdx >= 0 && static_cast<size_t>(scaleAccIdx) < loadedGltf.model.accessors.size())
                            instanceCount = loadedGltf.model.accessors[scaleAccIdx].count;

                        for (int64_t i = 0; i < instanceCount; ++i) {
                            MeshInstanceInfo inst;
                            inst.originalGltfIndex = loadedGltf.uniqueId;
                            inst.originalNodeIndex = nodeIndex;
                            inst.originalMeshIndex = node.mesh;
                            inst.sourceModelIndexInLoadedModels = modelIndexInLoadedModels;
                            glm::dvec3 t(0), s(1, 1, 1);
                            glm::dquat r(1, 0, 0, 0);
                            if (transAccIdx >= 0) {
                                CesiumGltf::AccessorView<glm::vec3> tv(loadedGltf.model, transAccIdx);
                                if (tv.status() == CesiumGltf::AccessorViewStatus::Valid && i < tv.size()) t = glm::dvec3(tv[i]);
                            }
                            if (rotAccIdx >= 0) {
                                CesiumGltf::AccessorView<glm::vec4> rv(loadedGltf.model, rotAccIdx);
                                if (rv.status() == CesiumGltf::AccessorViewStatus::Valid && i < rv.size()) {
                                    auto q = rv[i];
                                    r = glm::normalize(glm::dquat(q.w, q.x, q.y, q.z));
                                }
                            }
                            if (scaleAccIdx >= 0) {
                                CesiumGltf::AccessorView<glm::vec3> sv(loadedGltf.model, scaleAccIdx);
                                if (sv.status() == CesiumGltf::AccessorViewStatus::Valid && i < sv.size()) s = glm::dvec3(sv[i]);
                            }
                            glm::dmat4 instLocal = glm::translate(glm::dmat4(1), t) * glm::mat4_cast(r) * glm::scale(glm::dmat4(1), s);
                            inst.transform = TransformComponents::fromMat4(worldTransform * instLocal);
                            semanticGroups[semanticKey].push_back({ { loadedGltf.uniqueId, node.mesh }, inst });
                        }
                        parentNodeIndicesChainForChildren.push_back(nodeIndex);
                        for (int32_t c : node.children)
                            traverseNode(loadedGltf, modelIndexInLoadedModels, c, worldTransform, semanticGroups, parentNodeIndicesChainForChildren);
                        parentNodeIndicesChainForChildren.pop_back();
                        return;
                    }
                } catch (...) {}
            }

            MeshInstanceInfo inst;
            inst.originalGltfIndex = loadedGltf.uniqueId;
            inst.originalNodeIndex = nodeIndex;
            inst.originalMeshIndex = node.mesh;
            inst.sourceModelIndexInLoadedModels = modelIndexInLoadedModels;
            inst.transform = TransformComponents::fromMat4(worldTransform);
            semanticGroups[semanticKey].push_back({ { loadedGltf.uniqueId, node.mesh }, inst });
        }

        parentNodeIndicesChainForChildren.push_back(nodeIndex);
        for (int32_t c : node.children)
            traverseNode(loadedGltf, modelIndexInLoadedModels, c, worldTransform, semanticGroups, parentNodeIndicesChainForChildren);
        parentNodeIndicesChainForChildren.pop_back();
    }

    InstancingDetectionResult SemanticHausdorffInstancingDetector::detect(const std::vector<LoadedGltfModel>& loadedModels) {
        InstancingDetectionResult result;
        GltfInstancing::logInfo("[Hausdorff] 开始检测: 遍历 " + std::to_string(loadedModels.size()) + " 个模型收集语义组...");
        std::map<std::string, std::vector<std::pair<std::pair<int32_t, int32_t>, MeshInstanceInfo>>> semanticGroups;

        std::map<std::string, int> fileHashToRepId;
        std::map<int, int> modelIdToRepId;
        for (const auto& m : loadedModels) {
            if (m.fileHash.empty()) continue;
            auto it = fileHashToRepId.find(m.fileHash);
            if (it == fileHashToRepId.end()) {
                fileHashToRepId[m.fileHash] = m.uniqueId;
                modelIdToRepId[m.uniqueId] = m.uniqueId;
            } else {
                modelIdToRepId[m.uniqueId] = it->second;
            }
        }

        for (size_t mi = 0; mi < loadedModels.size(); ++mi) {
            const auto& lm = loadedModels[mi];
            if (lm.model.scenes.empty()) continue;
            int32_t sceneIdx = lm.model.scene >= 0 ? lm.model.scene : 0;
            if (static_cast<size_t>(sceneIdx) >= lm.model.scenes.size()) continue;
            std::vector<int32_t> parentChain;
            for (int32_t root : lm.model.scenes[sceneIdx].nodes) {
                traverseNode(lm, static_cast<int32_t>(mi), root, glm::dmat4(1), semanticGroups, parentChain);
            }
        }

        size_t totalInstances = 0;
        for (const auto& [k, v] : semanticGroups) totalInstances += v.size();
        const size_t totalGroups = semanticGroups.size();
        GltfInstancing::logInfo("[Hausdorff] 开始聚类: " + std::to_string(totalGroups) + " 个语义组, "
            + std::to_string(totalInstances) + " 个实例, 正在进行相似度比较");

        size_t groupIdx = 0;
        const size_t logInterval = 100;
        for (auto& [semanticKey, instances] : semanticGroups) {
            ++groupIdx;
            if (instances.empty()) {
                GltfInstancing::logInfo("[Hausdorff] 语义组 " + std::to_string(groupIdx) + "/" + std::to_string(totalGroups) + " (无实例, 跳过).");
                continue;
            }

            GltfInstancing::logInfo("[Hausdorff] 语义组 " + std::to_string(groupIdx) + "/" + std::to_string(totalGroups)
                + " (本组 " + std::to_string(instances.size()) + " 个实例)...");

            std::map<std::pair<int32_t, int32_t>, size_t> meshToRepModelMesh;
            std::vector<std::pair<int32_t, int32_t>> representatives;
            std::vector<std::vector<MeshInstanceInfo>> clusterInstances;
            size_t similarityCount = 0;

            for (const auto& [modelMesh, inst] : instances) {
                int32_t modelId = modelIdToRepId.count(inst.originalGltfIndex) ? modelIdToRepId[inst.originalGltfIndex] : inst.originalGltfIndex;
                auto key = std::make_pair(modelId, inst.originalMeshIndex);
                auto it = meshToRepModelMesh.find(key);
                if (it != meshToRepModelMesh.end()) {
                    clusterInstances[it->second].push_back(inst);
                    continue;
                }

                const LoadedGltfModel* repModel = nullptr;
                int repMeshIdx = -1;
                size_t clusterIdx = clusterInstances.size();
                bool found = false;

                for (size_t r = 0; r < representatives.size(); ++r) {
                    auto [repModelId, repMesh] = representatives[r];
                    const LoadedGltfModel* rm = nullptr;
                    for (const auto& m : loadedModels)
                        if (m.uniqueId == repModelId) { rm = &m; break; }
                    if (!rm) continue;
                    const LoadedGltfModel* cm = nullptr;
                    for (const auto& m : loadedModels)
                        if (m.uniqueId == modelId) { cm = &m; break; }
                    if (!cm) continue;
                    const CesiumGltf::Mesh& meshCur = cm->model.meshes[inst.originalMeshIndex];
                    const CesiumGltf::Mesh& meshRep = rm->model.meshes[repMesh];
                    double sim = computeMeshSimilarity(cm->model, meshCur, rm->model, meshRep);
                    if (++similarityCount % logInterval == 0)
                        GltfInstancing::logInfo("[Hausdorff]  已比较 " + std::to_string(similarityCount) + " 次 (组 " + std::to_string(groupIdx) + "/" + std::to_string(totalGroups) + ")");
                    if (sim >= 0 && sim >= _similarityThreshold) {
                        meshToRepModelMesh[key] = r;
                        clusterInstances[r].push_back(inst);
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    representatives.push_back(key);
                    meshToRepModelMesh[key] = clusterIdx;
                    clusterInstances.push_back({ inst });
                }
            }

            GltfInstancing::logInfo("[Hausdorff] 语义组 " + std::to_string(groupIdx) + "/" + std::to_string(totalGroups) + " 完成 (本组 " + std::to_string(representatives.size()) + " 个代表).");

            for (size_t c = 0; c < clusterInstances.size(); ++c) {
                if (clusterInstances[c].size() < static_cast<size_t>(_instanceLimit)) {
                    for (const auto& inst : clusterInstances[c]) {
                        NonInstancedMeshInfo ni;
                        ni.originalGltfModelIndex = inst.originalGltfIndex;
                        ni.originalMeshIndexInModel = inst.originalMeshIndex;
                        ni.originalNodeIndexInModel = inst.originalNodeIndex;
                        ni.transform = inst.transform;
                        ni.sourceModelIndexInLoadedModels = inst.sourceModelIndexInLoadedModels;
                        result.nonInstancedMeshes.push_back(ni);
                    }
                    continue;
                }
                auto [repModelId, repMeshIdx] = representatives[c];
                const LoadedGltfModel* repModel = nullptr;
                for (const auto& m : loadedModels)
                    if (m.uniqueId == repModelId) { repModel = &m; break; }
                if (!repModel) continue;

                InstancedMeshGroup grp;
                grp.representativeGltfModelIndex = repModelId;
                grp.representativeMeshIndexInModel = repMeshIdx;
                grp.representativeMeshName = repModel->model.meshes[repMeshIdx].name;
                grp.meshSignature = static_cast<size_t>(std::hash<std::string>{}(semanticKey + std::to_string(c)));
                grp.instances = clusterInstances[c];
                grp.representativeMeshBoundingBox = getMeshBoundingBox(repModel->model, repModel->model.meshes[repMeshIdx]);
                for (const auto& p : repModel->model.meshes[repMeshIdx].primitives)
                    grp.representativePrimitiveBoundingBoxes.push_back(getPrimitiveBoundingBox(repModel->model, p));
                result.instancedGroups.push_back(grp);
            }
        }

        GltfInstancing::logInfo("[Hausdorff] 聚类完成: " + std::to_string(result.instancedGroups.size()) + " 个实例组, "
            + std::to_string(result.nonInstancedMeshes.size()) + " 个非实例 mesh.");
        return result;
    }

} // namespace GltfInstancing
