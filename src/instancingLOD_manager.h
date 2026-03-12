#ifndef INSTANCING_LOD_MANAGER_H
#define INSTANCING_LOD_MANAGER_H

#include "utilities.h" // For BoundingBox, MeshInstanceInfo, etc.
#include "semantic_parser.h"
#include "glb_reader.h" // For LoadedGltfModel
#include "instancing_result.h" // For InstancingDetectionResult

#include <vector>
#include <map>
#include <string>
#include <memory>

namespace GltfInstancing {

    // 扩展的 Mesh 信息，包含几何特征与语义数据（用于语义+几何联合驱动的 LOD 分层）
    struct ExtendedMeshInfo {
        int originalMeshId;       // 对应 LoadedGltfModel 中的 uniqueId 或全局索引
        std::string meshName;     // 用于关联语义 (Actor.Hash)
        
        // 几何特征 (Geometric Features)
        BoundingBox aabb;
        double volume;            // AABB 体积
        double diagonal;          // AABB 对角线长度
        int vertexCount;          // 顶点数量 (用于评估复杂度)
        
        // 语义特征 (Semantic Features)
        SemanticInfo semantic;

        // 实例数据
        // 使用 MeshInstanceInfo 以保留变换组件，方便后续修改 Scale/Translation
        std::vector<MeshInstanceInfo> instances;

        // 指向原始几何数据的指针 (用于后续写入 GLB)
        int sourceModelIndex; 
        int sourceMeshIndex;
    };

    // 单个 LOD 层级的结果
    struct LODLevelResult {
        int level; // 5, 4, 3, 2, 1
        std::vector<ExtendedMeshInfo> nodes; // 该层级的所有节点 (Representative Meshes)
        double geometricError; // 该层级对应的 Geometric Error (用于写入 tileset)
    };

    // LOD 配置参数
    struct LODConfig {
        bool enableLOD = true;
        int maxLODLevels = 5;
        double targetScreenSSE = 16.0; // 目标屏幕像素误差
        bool enableSemanticCheck = true;
        bool enableGeometricCheck = true;
        
        // 每级 LOD 的 Hausdorff 相似度阈值，LOD 越粗糙越宽松。index 0=LOD4, 1=LOD3, 2=LOD2, 3=LOD1
        std::vector<double> similarityThresholdsPerLevel = { 0.90, 0.85, 0.80, 0.75 };
        size_t hausdorffMaxSamplePoints = 2000;
        int instanceLimit = 2;
        std::string materialFilterMode = "none"; // "none", "hash", "index"
        double lod4_sizeTolerance = 0.05;  // 仅当 similarityThresholdsPerLevel 为空时用作体积聚类回退
        double lod3_aspectRatioTolerance = 0.20;
    };

    class InstancingLODManager {
    public:
        InstancingLODManager(const LODConfig& config);
        ~InstancingLODManager();

        // 核心函数：生成所有 LOD 层级（语义+几何联合驱动）
        // 输入：LOD5 的原始检测结果 (InstancingDetectionResult) + 语义数据 + 几何特征
        // 输出：按层级组织的 LOD 数据
        std::map<int, LODLevelResult> generateLODs(
            const InstancingDetectionResult& lod5Data,
            const std::vector<LoadedGltfModel>& loadedModels,
            const SemanticParser& semanticParser
        );

        /**
         * 对实例化检测结果做 Family/Category 聚类，减少 Unique Meshes。
         * 供 Non-instanced LOD 后处理、HLOD 父 tile 等场景复用。
         */
        static InstancingDetectionResult clusterInstancingResult(
            const InstancingDetectionResult& input,
            const std::vector<LoadedGltfModel>& loadedModels,
            const SemanticParser& semanticParser,
            const LODConfig& config
        );

    private:
        LODConfig _config;

        // --- 内部处理流程 ---

        // 1. 初始化：将 InstancingDetectionResult 转换为 ExtendedMeshInfo 列表 (LOD5)
        std::vector<ExtendedMeshInfo> initializeLOD5(
            const InstancingDetectionResult& lod5Data,
            const std::vector<LoadedGltfModel>& loadedModels,
            const SemanticParser& semanticParser
        );

        // 2. 构建 LOD4 (Variant Level): 基于 Family + Hausdorff 几何相似度聚类
        LODLevelResult buildLOD4(const std::vector<ExtendedMeshInfo>& lod5Meshes,
            const std::vector<LoadedGltfModel>& loadedModels);

        // 3. 构建 LOD3 (Class Level): 基于 Category + Hausdorff 几何相似度聚类
        LODLevelResult buildLOD3(const std::vector<ExtendedMeshInfo>& lod4Meshes,
            const std::vector<LoadedGltfModel>& loadedModels);

        // 4. 构建 LOD2 (Abstract Level): 基于抽象类别 (需映射) + 极简模型
        LODLevelResult buildLOD2(const std::vector<ExtendedMeshInfo>& lod3Meshes);

        // 5. 构建 LOD1 (Proxy Level): 全局 AABB 替换 (生成 Cube)
        LODLevelResult buildLOD1(const std::vector<ExtendedMeshInfo>& lod2Meshes);

        // 聚类内部流程：DetectionResult -> LOD5 -> LOD4 -> LOD3 -> DetectionResult
        InstancingDetectionResult clusterResultInternal(
            const InstancingDetectionResult& input,
            const std::vector<LoadedGltfModel>& loadedModels,
            const SemanticParser& semanticParser
        );

        // --- 辅助函数 ---

        // 计算几何误差 (AABB 差异)
        double calculateGeometricError(const ExtendedMeshInfo& original, const ExtendedMeshInfo& representative);

        // 检查 SSE 是否允许合并
        // error: 物理误差 (米)
        // distance: 预设观察距离 (米)
        bool checkSSE(double error, double distance);

        // 获取 AABB 的主要特征 (用于哈希/比较)
        // 返回: 0=X, 1=Y, 2=Z (最长轴)
        int getMainAxis(const BoundingBox& box);
        double getAspectRatio(const BoundingBox& box); // 最长边 / 次长边

        // 查找最佳代表 (Representative)
        // strategy: 0=VolumeMean, 1=MinVertexCount, 2=SimplestShape
        const ExtendedMeshInfo* findRepresentative(
            const std::vector<const ExtendedMeshInfo*>& group, 
            int strategy
        );
    };

} // namespace GltfInstancing

#endif // INSTANCING_LOD_MANAGER_H
