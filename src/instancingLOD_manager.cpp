#include "instancingLOD_manager.h"
#include "hausdorff_similarity.h"
#include "material_matching.h"
#include "utilities.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <unordered_map>
#include <vector>

namespace GltfInstancing {

    InstancingLODManager::InstancingLODManager(const LODConfig& config) : _config(config) {}

    InstancingLODManager::~InstancingLODManager() {}

    std::map<int, LODLevelResult> InstancingLODManager::generateLODs(
        const InstancingDetectionResult& lod5Data,
        const std::vector<LoadedGltfModel>& loadedModels,
        const SemanticParser& semanticParser
    ) {
        std::map<int, LODLevelResult> results;
        
        if (!_config.enableLOD) {
            logMessage("LOD generation disabled.");
            return results;
        }

        logMessage("Starting LOD generation...");

        // 1. LOD5 (Base)
        auto lod5Meshes = initializeLOD5(lod5Data, loadedModels, semanticParser);
        results[5] = { 5, lod5Meshes, 0.0 };
        logMessage("LOD5 generated: " + std::to_string(lod5Meshes.size()) + " unique meshes.");

        // 2. LOD4 (Variant)
        if (_config.maxLODLevels >= 4) {
            results[4] = buildLOD4(lod5Meshes, loadedModels);
            logMessage("LOD4 generated: " + std::to_string(results[4].nodes.size()) + " unique meshes.");
        }

        // 3. LOD3 (Class)
        if (_config.maxLODLevels >= 3) {
            // LOD3 ?Ÿ?ä? LOD4 çš„ç?“?œç?§ç?­?š??
            results[3] = buildLOD3(results[4].nodes, loadedModels);
            logMessage("LOD3 generated: " + std::to_string(results[3].nodes.size()) + " unique meshes.");
        }

        // 4. LOD2 (Abstract)
        if (_config.maxLODLevels >= 2) {
            results[2] = buildLOD2(results[3].nodes);
            logMessage("LOD2 generated: " + std::to_string(results[2].nodes.size()) + " unique meshes.");
        }

        // 5. LOD1 (Proxy)
        if (_config.maxLODLevels >= 1) {
            // LOD1 ?Ÿ?ä? LOD2 (?ˆ–?€…ä??ä?•ä¸€ç?? ç”Ÿ?ˆ??Œ?› ä¸???ƒ??Œ?…¨?›???ä?†?‡ ä?•ä?“
            results[1] = buildLOD1(results[2].nodes);
            logMessage("LOD1 generated: " + std::to_string(results[1].nodes.size()) + " unique meshes (Proxies).");
        }

        return results;
    }

    std::vector<ExtendedMeshInfo> InstancingLODManager::initializeLOD5(
        const InstancingDetectionResult& lod5Data,
        const std::vector<LoadedGltfModel>& loadedModels,
        const SemanticParser& semanticParser
    ) {
        std::vector<ExtendedMeshInfo> result;

        // ?¤„ç† Instanced Groups
        for (const auto& group : lod5Data.instancedGroups) {
            ExtendedMeshInfo info;
            info.originalMeshId = group.representativeMeshIndexInModel; // ??™é‡Œ???ƒ?éœ€???…¨??€?”?ä¸€ID??Œ?š‚ç”¨??€éƒ?
            info.sourceModelIndex = group.representativeGltfModelIndex;
            info.sourceMeshIndex = group.representativeMeshIndexInModel;
            info.meshName = group.representativeMeshName;
            info.instances = group.instances;

            // ???–?‡ ä?•ä????
            const auto& model = loadedModels[info.sourceModelIndex].model;
            if (info.sourceMeshIndex >= 0 && info.sourceMeshIndex < model.meshes.size()) {
                const auto& mesh = model.meshes[info.sourceMeshIndex];
                info.vertexCount = 0;
                // ???ç?— AABB ?’?é??ç‚??•?
                for (const auto& prim : mesh.primitives) {
                    BoundingBox primBox = getPrimitiveBoundingBox(model, prim);
                    info.aabb.merge(primBox);
                    
                    // ä?°ç?—é??ç‚??•?(é€š??‡ POSITION accessor)
                     auto posIt = prim.attributes.find("POSITION");
                     if (posIt != prim.attributes.end()) {
                         const auto& accessor = model.accessors[posIt->second];
                         info.vertexCount += static_cast<int>(accessor.count);
                     }
                }
            }

            // ???ç?—??ç”Ÿ?‡ ä?•ç‰???
            glm::dvec3 size = info.aabb.max - info.aabb.min;
            info.volume = size.x * size.y * size.z;
            info.diagonal = glm::length(size);

            // ???–??­ä?‰ä???? (?”??Œ glbStem + meshHash ?Œ?é…)
            std::string glbStem;
            if (info.sourceModelIndex >= 0 && info.sourceModelIndex < static_cast<int>(loadedModels.size()))
                glbStem = loadedModels[info.sourceModelIndex].originalPath.stem().string();
            auto semOpt = semanticParser.getSemanticInfo(glbStem, info.meshName);
            if (semOpt.has_value()) {
                info.semantic = semOpt.value();
            } else {
                // ??‚?œ????œ‰??­ä?‰??Œç?™é?˜??¤?€??ˆ–ä?ç•™ç??
                info.semantic.category = "Unknown";
                info.semantic.family = "Unknown";
            }

            result.push_back(info);
        }

        // TODO: ?¤„ç† Non-Instanced Meshes (ä?Ÿ??ä???§†ä¸????œ‰ä¸€ä¸???ä?‹çš„ Group)
        // ä¸?ä?†ç?€?Œ–??Œ??™é‡Œ?š‚?—????¤„ç†??€??‹?ˆ°çš?Instanced Groups
        // ??é™…é??ç›?ä¸­??”????ŠŠ?‰€?œ?Mesh éƒ?ç???…?LOD ç??ç†

        return result;
    }

    // --- LOD4: Family Clustering ---
    LODLevelResult InstancingLODManager::buildLOD4(const std::vector<ExtendedMeshInfo>& lod5Meshes,
        const std::vector<LoadedGltfModel>& loadedModels) {
        LODLevelResult result;
        result.level = 4;
        result.geometricError = 0.0; // ?°†???ç?—?œ€?¤§??????

        // 1. Group by Family
        std::map<std::string, std::vector<const ExtendedMeshInfo*>> familyGroups;
        for (const auto& mesh : lod5Meshes) {
            std::string key = mesh.semantic.family;
            if (key.empty() || key == "Unknown") {
                key = "Unknown_" + std::to_string(mesh.sourceModelIndex) + "_" + std::to_string(mesh.sourceMeshIndex);
            }
            familyGroups[key].push_back(&mesh);
        }

        // 2. Process each family group
        for (auto& [family, group] : familyGroups) {
            if (group.empty()) continue;

            // ??‚?œ???œ‰ä¸€ä¸???Œç›´??ä?ç•™
            if (group.size() == 1) {
                result.nodes.push_back(*group[0]);
                continue;
            }

            // ?‡ ä?•?­?šç??(Geometric Sub-clustering)
            // ??ä?? Family ç›¸?Œ??Œ??‚?œ?°???¸?????‚?¤??¤§??Œä?Ÿä¸?ƒ??ˆ???
            std::vector<std::vector<const ExtendedMeshInfo*>> subClusters;
            
            // ç?€?•çš„?´???ƒ?šç??: ?–ç??ä¸€ä¸?ä?œä¸?ç§?­??Œ?‰??‰€?œ‰ç›¸ä??çš„??›?‰?ä¸‹çš„?†?–ç??ä¸€ä¸?..
            std::vector<bool> processed(group.size(), false);
            for (size_t i = 0; i < group.size(); ++i) {
                if (processed[i]) continue;
                
                std::vector<const ExtendedMeshInfo*> currentCluster;
                currentCluster.push_back(group[i]);
                processed[i] = true;

                double threshold = (_config.similarityThresholdsPerLevel.size() >= 1) ? _config.similarityThresholdsPerLevel[0] : 0.90;
                const auto* repExt = group[i];
                bool useHausdorff = (repExt->sourceModelIndex >= 0 && static_cast<size_t>(repExt->sourceModelIndex) < loadedModels.size());
                double baseVol = group[i]->volume;
                
                for (size_t j = i + 1; j < group.size(); ++j) {
                    if (processed[j]) continue;

                    // ??€?Ÿ?ä?“ç§??°???¸?????‚
                    bool merge = false;
                    if (useHausdorff) {
                        const auto* candExt = group[j];
                        if (candExt->sourceModelIndex >= 0 && static_cast<size_t>(candExt->sourceModelIndex) < loadedModels.size()) {
                            const auto& repModel = loadedModels[repExt->sourceModelIndex];
                            const auto& repMesh = repModel.model.meshes[repExt->sourceMeshIndex];
                            const auto& candModel = loadedModels[candExt->sourceModelIndex];
                            const auto& candMesh = candModel.model.meshes[candExt->sourceMeshIndex];
                            if (_config.materialFilterMode == "hash") {
                                if (getMeshMaterialHash(candModel.model, candMesh) == getMeshMaterialHash(repModel.model, repMesh)) {
                                    double sim = computeMeshSimilarity(candModel.model, candMesh, repModel.model, repMesh, _config.hausdorffMaxSamplePoints);
                                    merge = (sim >= 0 && sim >= threshold);
                                }
                            } else if (_config.materialFilterMode == "index") {
                                if (getMeshMaterialIndex(candModel.model, candMesh) == getMeshMaterialIndex(repModel.model, repMesh)) {
                                    double sim = computeMeshSimilarity(candModel.model, candMesh, repModel.model, repMesh, _config.hausdorffMaxSamplePoints);
                                    merge = (sim >= 0 && sim >= threshold);
                                }
                            } else {
                                double sim = computeMeshSimilarity(candModel.model, candMesh, repModel.model, repMesh, _config.hausdorffMaxSamplePoints);
                                merge = (sim >= 0 && sim >= threshold);
                            }
                        }
                    } else {
                        double volDiff = std::abs(group[j]->volume - baseVol) / (baseVol + 1e-6);
                        merge = (volDiff < _config.lod4_sizeTolerance);
                    }
                    if (merge) {
                        currentCluster.push_back(group[j]);
                        processed[j] = true;
                    }
                }
                subClusters.push_back(currentCluster);
            }

            // ?????ä¸??­?šç??ç”Ÿ?ˆä????¨
            for (const auto& cluster : subClusters) {
                // ç­–ç•? 0: ?‰?ä?“ç§??œ€????‘????‡?€?çš„ (Representative)
                const ExtendedMeshInfo* rep = findRepresentative(cluster, 0);
                
                ExtendedMeshInfo newNode = *rep; // ?¤?ˆ?ä????¨çš„ä????
                newNode.instances.clear(); // ?¸…ç????ä?‹??Œ?‡†?¤‡?ˆ???

                // ?ˆ????‰€?œ‰?ˆ?‘˜çš„??ä?‹
                double maxErrorInCluster = 0.0;
                for (const auto* member : cluster) {
                    newNode.instances.insert(newNode.instances.end(), member->instances.begin(), member->instances.end());
                    
                    // ???ç?—??????
                    double err = calculateGeometricError(*member, *rep);
                    if (err > maxErrorInCluster) maxErrorInCluster = err;
                }

                if (maxErrorInCluster > result.geometricError) {
                    result.geometricError = maxErrorInCluster;
                }

                result.nodes.push_back(newNode);
            }
        }

        return result;
    }

    // --- LOD3: Category Clustering ---
    LODLevelResult InstancingLODManager::buildLOD3(const std::vector<ExtendedMeshInfo>& lod4Meshes,
        const std::vector<LoadedGltfModel>& loadedModels) {
        LODLevelResult result;
        result.level = 3;
        result.geometricError = 0.0;

        // 1. Group by Category
        std::map<std::string, std::vector<const ExtendedMeshInfo*>> categoryGroups;
        for (const auto& mesh : lod4Meshes) {
            std::string key = mesh.semantic.category;
             if (key.empty() || key == "Unknown") {
                // ?— ??•?šç??çš„?•ç‹??¤„ç?
                categoryGroups["__UNIQUE__" + std::to_string(mesh.sourceModelIndex)].push_back(&mesh);
            } else {
                categoryGroups[key].push_back(&mesh);
            }
        }

        // 2. Process
        for (auto& [cat, group] : categoryGroups) {
            // ç??ä?? LOD4??Œä?†ä??ç”¨ Aspect Ratio ?ˆ¤??š
             // ç?€?•çš„?´???ƒ?šç??
            std::vector<bool> processed(group.size(), false);
            for (size_t i = 0; i < group.size(); ++i) {
                if (processed[i]) continue;
                
                std::vector<const ExtendedMeshInfo*> currentCluster;
                currentCluster.push_back(group[i]);
                processed[i] = true;

                double threshold = (_config.similarityThresholdsPerLevel.size() >= 2) ? _config.similarityThresholdsPerLevel[1] : 0.85;
                const auto* repExt = group[i];
                bool useHausdorff = (repExt->sourceModelIndex >= 0 && static_cast<size_t>(repExt->sourceModelIndex) < loadedModels.size());
                double baseRatio = getAspectRatio(group[i]->aabb);
                
                for (size_t j = i + 1; j < group.size(); ++j) {
                    if (processed[j]) continue;
                    bool merge = false;
                    if (useHausdorff) {
                        const auto* candExt = group[j];
                        if (candExt->sourceModelIndex >= 0 && static_cast<size_t>(candExt->sourceModelIndex) < loadedModels.size()) {
                            const auto& repModel = loadedModels[repExt->sourceModelIndex];
                            const auto& repMesh = repModel.model.meshes[repExt->sourceMeshIndex];
                            const auto& candModel = loadedModels[candExt->sourceModelIndex];
                            const auto& candMesh = candModel.model.meshes[candExt->sourceMeshIndex];
                            if (_config.materialFilterMode == "hash") {
                                if (getMeshMaterialHash(candModel.model, candMesh) == getMeshMaterialHash(repModel.model, repMesh)) {
                                    double sim = computeMeshSimilarity(candModel.model, candMesh, repModel.model, repMesh, _config.hausdorffMaxSamplePoints);
                                    merge = (sim >= 0 && sim >= threshold);
                                }
                            } else if (_config.materialFilterMode == "index") {
                                if (getMeshMaterialIndex(candModel.model, candMesh) == getMeshMaterialIndex(repModel.model, repMesh)) {
                                    double sim = computeMeshSimilarity(candModel.model, candMesh, repModel.model, repMesh, _config.hausdorffMaxSamplePoints);
                                    merge = (sim >= 0 && sim >= threshold);
                                }
                            } else {
                                double sim = computeMeshSimilarity(candModel.model, candMesh, repModel.model, repMesh, _config.hausdorffMaxSamplePoints);
                                merge = (sim >= 0 && sim >= threshold);
                            }
                        }
                    } else {
                        double ratio = getAspectRatio(group[j]->aabb);
                        merge = (std::abs(ratio - baseRatio) < _config.lod3_aspectRatioTolerance);
                    }
                    if (merge) {
                        currentCluster.push_back(group[j]);
                        processed[j] = true;
                    }
                }
                
                // ç­–ç•? 1: ?‰?é??ç‚??•°?œ€?°‘çš„ (Simplest)
                const ExtendedMeshInfo* rep = findRepresentative(currentCluster, 1);
                
                ExtendedMeshInfo newNode = *rep;
                newNode.instances.clear();
                
                double maxErrorInCluster = 0.0;
                for (const auto* member : currentCluster) {
                    newNode.instances.insert(newNode.instances.end(), member->instances.begin(), member->instances.end());
                    double err = calculateGeometricError(*member, *rep);
                    if (err > maxErrorInCluster) maxErrorInCluster = err;
                }
                
                if (maxErrorInCluster > result.geometricError) {
                    result.geometricError = maxErrorInCluster;
                }
                result.nodes.push_back(newNode);
            }
        }
        
        // ç??ä? LOD3 ??????ä¸?°ä??LOD4 (?•?°ƒ?€?
        // ??é™…??”ç”¨ä¸­é€š?¸¸ä¸éœ€??????ˆ???Œ?› ä¸? Tileset ç?“?„ä?š?¤„ç?
        return result;
    }

    // --- LOD2: Abstract Level ---
    LODLevelResult InstancingLODManager::buildLOD2(const std::vector<ExtendedMeshInfo>& lod3Meshes) {
        // ?š‚?—?ç?€?Œ–??šç›´???¤?ˆ? LOD3??Œ?ˆ–?€…?œ¨??™é‡Œ??”ç”¨?›´??€??›çš„ Category ?ˆ???
        // ??”??‚?°?"Office Chair" ?’?"Dining Chair" ?ˆ???ä¸?"Chair"
        // ç›??‰????œ‰?˜ ?°„??¨??Œ?š‚?—?é€ä? ??Œä?†???Š ??????é˜ˆ?€?
        LODLevelResult result;
        result.level = 2;
        result.nodes = lod3Meshes;
        result.geometricError = 200.0; // ?¤ç”¨é€???‘??Œä?†?› ä¸???“?…????ç??˜?LOD3??Œé€š?¸¸ä¸ä?š?œ‰?¤??¤š?˜?Œ–é™¤é?˜ ?°„??¨?­˜?œ¨
        result.geometricError *= 2.0; // ç?€?•?”??¤§??????
        return result;
    }

    // --- LOD1: Proxy Level (Cube) ---
    LODLevelResult InstancingLODManager::buildLOD1(const std::vector<ExtendedMeshInfo>& lod2Meshes) {
        LODLevelResult result;
        result.level = 1;
        result.geometricError = 1000.0; // ??ˆ?¤§

        // ?ˆ›???ä¸€ä¸?ç‰???Šçš„ Proxy Mesh Info
        // ?œ¨??é™?GLB ?†™?…??—???Œ?ˆ‘ä??éœ€????†?ˆ???™ä¸?? ‡??—??Œ?†™?…?ä¸€ä¸?Unit Cube
        ExtendedMeshInfo proxyInfo;
        proxyInfo.meshName = "LOD1_Proxy_Cube";
        proxyInfo.sourceModelIndex = -1; // -1 ??¨ç¤?ç”Ÿ?ˆçš?Proxy
        proxyInfo.sourceMeshIndex = -1;
        
        // ?”?é›†?‰€?œ‰??ä?‹??Œ???ä???”???ƒä??çš„çŸ?é˜?
        for (const auto& mesh : lod2Meshes) {
            // ???ç?—???Mesh çš?AABB ?°???¸?’Œä¸­??ƒ?ç§?
            glm::dvec3 size = mesh.aabb.max - mesh.aabb.min;
            glm::dvec3 center = (mesh.aabb.max + mesh.aabb.min) * 0.5;

            for (const auto& inst : mesh.instances) {
                MeshInstanceInfo newInst = inst;
                
                // ?˜??é€???‘???
                // ?Ÿ?§‹çŸ?é˜? M ?°?(0,0,0) ?˜???ˆ°ä¸–ç•Œä?ç??P
                // ?ˆ‘ä??éœ€???°† Unit Cube (?‡???ä¸­??ƒ?œ?0, ???é•? 1) ?˜???ˆ?Mesh çš?AABB
                
                // 1. ??”ç”¨?Ÿ?§‹?˜??
                glm::dmat4 originalMat = inst.transform.toMat4();
                
                // 2. ?œ¨??€éƒ¨ç??é—´ç???”??’Œä?ç§?
                // Cube (1x1x1) -> Scale(size) -> Translate(center) -> OriginalTransform
                // ??¨?„??š??™é‡Œçš„ center ?˜?Mesh ?œ¨?…??‡?????¨??‹ç??é—´ä¸­çš„ AABB ä¸­??ƒ
                
                glm::dmat4 localFix = glm::translate(glm::dmat4(1.0), center) * glm::scale(glm::dmat4(1.0), size);
                
                glm::dmat4 finalMat = originalMat * localFix;
                
                newInst.transform = TransformComponents::fromMat4(finalMat);
                proxyInfo.instances.push_back(newInst);
            }
        }
        
        result.nodes.push_back(proxyInfo);
        return result;
    }

    // --- Helpers ---

    double InstancingLODManager::calculateGeometricError(const ExtendedMeshInfo& original, const ExtendedMeshInfo& representative) {
        // ç?€?Œ–???ç?—??š????§’ç??é•??????? + AABB ä¸­??ƒ??ç??
        // ?›´ç??ç??çš„??”????˜?Hausdorff??Œä?†??™é‡Œç”?AABB ä?°ç?—
        double diagDiff = std::abs(original.diagonal - representative.diagonal);
        
        glm::dvec3 c1 = (original.aabb.max + original.aabb.min) * 0.5;
        glm::dvec3 c2 = (representative.aabb.max + representative.aabb.min) * 0.5;
        double centerDist = glm::length(c1 - c2);

        return diagDiff + centerDist;
    }

    const ExtendedMeshInfo* InstancingLODManager::findRepresentative(
        const std::vector<const ExtendedMeshInfo*>& group, 
        int strategy
    ) {
        if (group.empty()) return nullptr;
        
        if (strategy == 0) { // Mean Volume
            double totalVol = 0;
            for (auto* m : group) totalVol += m->volume;
            double avgVol = totalVol / group.size();
            
            const ExtendedMeshInfo* best = group[0];
            double minDiff = std::numeric_limits<double>::max();
            
            for (auto* m : group) {
                double diff = std::abs(m->volume - avgVol);
                if (diff < minDiff) {
                    minDiff = diff;
                    best = m;
                }
            }
            return best;
        }
        else if (strategy == 1) { // Min Vertex Count
            const ExtendedMeshInfo* best = group[0];
            for (auto* m : group) {
                if (m->vertexCount < best->vertexCount) {
                    best = m;
                }
            }
            return best;
        }
        
        return group[0];
    }

    double InstancingLODManager::getAspectRatio(const BoundingBox& box) {
        glm::dvec3 size = box.max - box.min;
        std::vector<double> dims = { size.x, size.y, size.z };
        std::sort(dims.begin(), dims.end());
        if (dims[1] < 1e-6) return 1.0;
        return dims[2] / dims[1];
    }

    // --- clusterInstancingResult (Family/Category clustering) ---

    InstancingDetectionResult InstancingLODManager::clusterInstancingResult(
        const InstancingDetectionResult& input,
        const std::vector<LoadedGltfModel>& loadedModels,
        const SemanticParser& semanticParser,
        const LODConfig& config
    ) {
        InstancingLODManager mgr(config);
        return mgr.clusterResultInternal(input, loadedModels, semanticParser);
    }

    InstancingDetectionResult InstancingLODManager::clusterResultInternal(
        const InstancingDetectionResult& input,
        const std::vector<LoadedGltfModel>& loadedModels,
        const SemanticParser& semanticParser
    ) {
        InstancingDetectionResult result;
        result.nonInstancedMeshes = input.nonInstancedMeshes;

        if (input.instancedGroups.empty()) return result;

        auto lod5Meshes = initializeLOD5(input, loadedModels, semanticParser);
        if (lod5Meshes.empty()) return input;

        LODLevelResult lod4Result = buildLOD4(lod5Meshes, loadedModels);
        LODLevelResult lod3Result = buildLOD3(lod4Result.nodes, loadedModels);

        for (const auto& node : lod3Result.nodes) {
            if (node.instances.empty()) continue;
            if (node.sourceModelIndex < 0 || node.sourceModelIndex >= static_cast<int>(loadedModels.size())) continue;
            const auto& model = loadedModels[node.sourceModelIndex].model;
            if (node.sourceMeshIndex < 0 || node.sourceMeshIndex >= static_cast<int>(model.meshes.size())) continue;

            InstancedMeshGroup grp;
            grp.representativeGltfModelIndex = node.sourceModelIndex;
            grp.representativeMeshIndexInModel = node.sourceMeshIndex;
            grp.representativeMeshName = node.meshName;
            grp.meshSignature = static_cast<size_t>(std::hash<std::string>{}(node.meshName + std::to_string(result.instancedGroups.size())));
            grp.instances = node.instances;

            const auto& mesh = model.meshes[node.sourceMeshIndex];
            grp.representativeMeshBoundingBox = getMeshBoundingBox(model, mesh);
            grp.representativePrimitiveBoundingBoxes.clear();
            for (const auto& p : mesh.primitives)
                grp.representativePrimitiveBoundingBoxes.push_back(getPrimitiveBoundingBox(model, p));

            result.instancedGroups.push_back(grp);
        }

        return result;
    }

} // namespace GltfInstancing

