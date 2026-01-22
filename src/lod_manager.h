#ifndef LOD_MANAGER_H
#define LOD_MANAGER_H

#include "utilities.h" // For BoundingBox, MeshInstanceInfo, etc.
#include "semantic_parser.h"
#include "glb_reader.h" // For LoadedGltfModel
#include "instancing_detector.h" // For InstancingDetectionResult

#include <vector>
#include <map>
#include <string>
#include <memory>

namespace GltfInstancing {

    // 扩展的 Mesh 信息，包含几何特征和语义数据
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
        
        // 几何相似度阈值
        double lod4_sizeTolerance = 0.05; // 5% 尺寸差异
        double lod3_aspectRatioTolerance = 0.20; // 20% 长宽比差异
    };

    class LODManager {
    public:
        LODManager(const LODConfig& config);
        ~LODManager();

        // 核心函数：生成所有 LOD 层级
        // 输入：LOD5 的原始检测结果 (InstancingDetectionResult) + 语义数据
        // 输出：按层级组织的 LOD 数据
        std::map<int, LODLevelResult> generateLODs(
            const InstancingDetectionResult& lod5Data,
            const std::vector<LoadedGltfModel>& loadedModels,
            const SemanticParser& semanticParser
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

        // 2. 构建 LOD4 (Variant Level): 基于 Family + 几何尺寸聚类
        LODLevelResult buildLOD4(const std::vector<ExtendedMeshInfo>& lod5Meshes);

        // 3. 构建 LOD3 (Class Level): 基于 Category + 几何形状(长宽比)聚类
        LODLevelResult buildLOD3(const std::vector<ExtendedMeshInfo>& lod4Meshes);

        // 4. 构建 LOD2 (Abstract Level): 基于抽象类别 (需映射) + 极简模型
        LODLevelResult buildLOD2(const std::vector<ExtendedMeshInfo>& lod3Meshes);

        // 5. 构建 LOD1 (Proxy Level): 全局 AABB 替换 (生成 Cube)
        LODLevelResult buildLOD1(const std::vector<ExtendedMeshInfo>& lod2Meshes);

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

#endif // LOD_MANAGER_H
