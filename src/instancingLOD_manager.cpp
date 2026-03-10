#include "instancingLOD_manager.h"
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
            results[4] = buildLOD4(lod5Meshes);
            logMessage("LOD4 generated: " + std::to_string(results[4].nodes.size()) + " unique meshes.");
        }

        // 3. LOD3 (Class)
        if (_config.maxLODLevels >= 3) {
            // LOD3 基于 LOD4 的结果继续聚�?
            results[3] = buildLOD3(results[4].nodes);
            logMessage("LOD3 generated: " + std::to_string(results[3].nodes.size()) + " unique meshes.");
        }

        // 4. LOD2 (Abstract)
        if (_config.maxLODLevels >= 2) {
            results[2] = buildLOD2(results[3].nodes);
            logMessage("LOD2 generated: " + std::to_string(results[2].nodes.size()) + " unique meshes.");
        }

        // 5. LOD1 (Proxy)
        if (_config.maxLODLevels >= 1) {
            // LOD1 基于 LOD2 (或者任何一�? 生成，因为它完全替换了几何体
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

        // 处理 Instanced Groups
        for (const auto& group : lod5Data.instancedGroups) {
            ExtendedMeshInfo info;
            info.originalMeshId = group.representativeMeshIndexInModel; // 这里可能需要全局唯一ID，暂用局�?
            info.sourceModelIndex = group.representativeGltfModelIndex;
            info.sourceMeshIndex = group.representativeMeshIndexInModel;
            info.meshName = group.representativeMeshName;
            info.instances = group.instances;

            // 获取几何信息
            const auto& model = loadedModels[info.sourceModelIndex].model;
            if (info.sourceMeshIndex >= 0 && info.sourceMeshIndex < model.meshes.size()) {
                const auto& mesh = model.meshes[info.sourceMeshIndex];
                info.vertexCount = 0;
                // 计算 AABB �?顶点�?
                for (const auto& prim : mesh.primitives) {
                    BoundingBox primBox = getPrimitiveBoundingBox(model, prim);
                    info.aabb.merge(primBox);
                    
                    // 估算顶点�?(通过 POSITION accessor)
                     auto posIt = prim.attributes.find("POSITION");
                     if (posIt != prim.attributes.end()) {
                         const auto& accessor = model.accessors[posIt->second];
                         info.vertexCount += static_cast<int>(accessor.count);
                     }
                }
            }

            // 计算衍生几何特征
            glm::dvec3 size = info.aabb.max - info.aabb.min;
            info.volume = size.x * size.y * size.z;
            info.diagonal = glm::length(size);

            // 获取语义信息 (支持 glbStem + meshHash 匹配)
            std::string glbStem;
            if (info.sourceModelIndex >= 0 && info.sourceModelIndex < static_cast<int>(loadedModels.size()))
                glbStem = loadedModels[info.sourceModelIndex].originalPath.stem().string();
            auto semOpt = semanticParser.getSemanticInfo(glbStem, info.meshName);
            if (semOpt.has_value()) {
                info.semantic = semOpt.value();
            } else {
                // 如果没有语义，给默认值或保留�?
                info.semantic.category = "Unknown";
                info.semantic.family = "Unknown";
            }

            result.push_back(info);
        }

        // TODO: 处理 Non-Instanced Meshes (也可以视为只有一个实例的 Group)
        // 为了简化，这里暂时只处理检测到�?Instanced Groups
        // 实际项目中应该把所�?Mesh 都纳�?LOD 管理

        return result;
    }

    // --- LOD4: Family Clustering ---
    LODLevelResult InstancingLODManager::buildLOD4(const std::vector<ExtendedMeshInfo>& lod5Meshes) {
        LODLevelResult result;
        result.level = 4;
        result.geometricError = 0.0; // 将计算最大误�?

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

            // 如果只有一个，直接保留
            if (group.size() == 1) {
                result.nodes.push_back(*group[0]);
                continue;
            }

            // 几何子聚�?(Geometric Sub-clustering)
            // 即使 Family 相同，如果尺寸差异太大，也不能合�?
            std::vector<std::vector<const ExtendedMeshInfo*>> subClusters;
            
            // 简单的贪心聚类: 取第一个作为种子，找所有相似的；剩下的再取第一�?..
            std::vector<bool> processed(group.size(), false);
            for (size_t i = 0; i < group.size(); ++i) {
                if (processed[i]) continue;
                
                std::vector<const ExtendedMeshInfo*> currentCluster;
                currentCluster.push_back(group[i]);
                processed[i] = true;

                double baseVol = group[i]->volume;
                
                for (size_t j = i + 1; j < group.size(); ++j) {
                    if (processed[j]) continue;

                    // 检查体�?尺寸差异
                    double volDiff = std::abs(group[j]->volume - baseVol) / (baseVol + 1e-6);
                    if (volDiff < _config.lod4_sizeTolerance) { // e.g. 5%
                        currentCluster.push_back(group[j]);
                        processed[j] = true;
                    }
                }
                subClusters.push_back(currentCluster);
            }

            // 对每个子聚类生成代表
            for (const auto& cluster : subClusters) {
                // 策略 0: 找体积最接近平均值的 (Representative)
                const ExtendedMeshInfo* rep = findRepresentative(cluster, 0);
                
                ExtendedMeshInfo newNode = *rep; // 复制代表的信�?
                newNode.instances.clear(); // 清空实例，准备合�?

                // 合并所有成员的实例
                double maxErrorInCluster = 0.0;
                for (const auto* member : cluster) {
                    newNode.instances.insert(newNode.instances.end(), member->instances.begin(), member->instances.end());
                    
                    // 计算误差
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
    LODLevelResult InstancingLODManager::buildLOD3(const std::vector<ExtendedMeshInfo>& lod4Meshes) {
        LODLevelResult result;
        result.level = 3;
        result.geometricError = 0.0;

        // 1. Group by Category
        std::map<std::string, std::vector<const ExtendedMeshInfo*>> categoryGroups;
        for (const auto& mesh : lod4Meshes) {
            std::string key = mesh.semantic.category;
             if (key.empty() || key == "Unknown") {
                // 无法聚类的单独处�?
                categoryGroups["__UNIQUE__" + std::to_string(mesh.sourceModelIndex)].push_back(&mesh);
            } else {
                categoryGroups[key].push_back(&mesh);
            }
        }

        // 2. Process
        for (auto& [cat, group] : categoryGroups) {
            // 类似 LOD4，但使用 Aspect Ratio 判定
             // 简单的贪心聚类
            std::vector<bool> processed(group.size(), false);
            for (size_t i = 0; i < group.size(); ++i) {
                if (processed[i]) continue;
                
                std::vector<const ExtendedMeshInfo*> currentCluster;
                currentCluster.push_back(group[i]);
                processed[i] = true;

                double baseRatio = getAspectRatio(group[i]->aabb);
                
                for (size_t j = i + 1; j < group.size(); ++j) {
                    if (processed[j]) continue;

                    double ratio = getAspectRatio(group[j]->aabb);
                    if (std::abs(ratio - baseRatio) < _config.lod3_aspectRatioTolerance) {
                        currentCluster.push_back(group[j]);
                        processed[j] = true;
                    }
                }
                
                // 策略 1: 找顶点数最少的 (Simplest)
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
        
        // 确保 LOD3 误差不小�?LOD4 (单调�?
        // 实际应用中通常不需要强制，因为 Tileset 结构会处�?
        return result;
    }

    // --- LOD2: Abstract Level ---
    LODLevelResult InstancingLODManager::buildLOD2(const std::vector<ExtendedMeshInfo>& lod3Meshes) {
        // 暂时简化：直接复制 LOD3，或者在这里应用更激进的 Category 合并
        // 比如�?"Office Chair" �?"Dining Chair" 合并�?"Chair"
        // 目前没有映射表，暂时透传，但增加误差阈�?
        LODLevelResult result = buildLOD3(lod3Meshes); // 复用逻辑，但因为输入已经�?LOD3，通常不会有太多变化除非映射表存在
        result.level = 2;
        result.geometricError *= 2.0; // 简单放大误�?
        return result;
    }

    // --- LOD1: Proxy Level (Cube) ---
    LODLevelResult InstancingLODManager::buildLOD1(const std::vector<ExtendedMeshInfo>& lod2Meshes) {
        LODLevelResult result;
        result.level = 1;
        result.geometricError = 1000.0; // 很大

        // 创建一个特殊的 Proxy Mesh Info
        // 在实�?GLB 写入时，我们需要识别这个标志，写入一�?Unit Cube
        ExtendedMeshInfo proxyInfo;
        proxyInfo.meshName = "LOD1_Proxy_Cube";
        proxyInfo.sourceModelIndex = -1; // -1 表示生成�?Proxy
        proxyInfo.sourceMeshIndex = -1;
        
        // 收集所有实例，并修改它们的矩阵
        for (const auto& mesh : lod2Meshes) {
            // 计算�?Mesh �?AABB 尺寸和中心偏�?
            glm::dvec3 size = mesh.aabb.max - mesh.aabb.min;
            glm::dvec3 center = (mesh.aabb.max + mesh.aabb.min) * 0.5;

            for (const auto& inst : mesh.instances) {
                MeshInstanceInfo newInst = inst;
                
                // 变换逻辑�?
                // 原始矩阵 M �?(0,0,0) 变换到世界位�?P
                // 我们需要将 Unit Cube (假设中心�?0, 边长 1) 变换�?Mesh �?AABB
                
                // 1. 应用原始变换
                glm::dmat4 originalMat = inst.transform.toMat4();
                
                // 2. 在局部空间缩放和位移
                // Cube (1x1x1) -> Scale(size) -> Translate(center) -> OriginalTransform
                // 注意：这里的 center �?Mesh 在其自身模型空间中的 AABB 中心
                
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
        // 简化计算：对角线长度差 + AABB 中心距离
        // 更精确的应该�?Hausdorff，但这里�?AABB 估算
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
        // 排序 x, y, z
        std::vector<double> dims = { size.x, size.y, size.z };
        std::sort(dims.begin(), dims.end());
        // �?/ �?(忽略高度/厚度)
        if (dims[1] < 1e-6) return 1.0;
        return dims[2] / dims[1];
    }

} // namespace GltfInstancing

