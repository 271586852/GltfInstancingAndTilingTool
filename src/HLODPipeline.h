#ifndef HLOD_PIPELINE_H
#define HLOD_PIPELINE_H

#include <string>
#include <vector>
#include <memory>
#include <filesystem>
#include <map>
#include <glm/glm.hpp>
#include <CesiumGltf/Model.h>
#include "instancing_detector.h" // Needed for GltfInstancing::InstancingDetectionResult

struct ToolConfiguration; // Forward declaration

namespace HLOD {

    // 策略角色定义 (Refactored logic doesn't strictly use these linearly anymore, but good for reference)
    enum class TileRole {
        Proxy,      // 远距离：包围盒/替代体
        Instancing, // 中距离：GPU 实例化 (Standard approach)
        Detail      // 近距离：高精度模型
    };

    // 单层策略配置
    struct LodLevelConfig {
        int level;                      // 0 = Root (Farthest)
        TileRole role;
        double geometricErrorFactor;    // 相对 Tile 对角线的系数
    };

    // 场景对象元数据
    struct SceneObject {
        int id;
        std::filesystem::path originalFilePath;
        glm::vec3 minBound; // AABB Min
        glm::vec3 maxBound; // AABB Max
        glm::vec3 center;
        
        // 用于实例化聚类的特征
        size_t vertexCount = 0;
        glm::vec3 dimensions; // AABB Size
    };

    // 统计数据结构
    struct TileStats {
        std::string tileName;
        int level;
        double fileSizeKB;
        size_t triangleCount; // Unique/Stored Triangles (File size proxy)
        size_t renderedTriangleCount; // New: Total Visualized Triangles (GPU Load proxy)
        size_t instanceCount; // Number of instances (if instanced)
        size_t uniqueMeshCount; // Number of unique meshes stored
    };

    // HLOD 树节点
    struct HLODNode {
        int level;
        int x, y; // Grid coordinates at this level
        glm::vec3 minBound; // Node spatial bounds (Grid)
        glm::vec3 maxBound;
        
        // New: Tight fitting bounds (Actual content)
        glm::vec3 tightMinBound;
        glm::vec3 tightMaxBound;

        std::vector<SceneObject> objects; 
        std::vector<std::unique_ptr<HLODNode>> children;
        
        std::string tileFilename; // Generated GLB filename
        double geometricError;
        
        bool isLeaf() const { return children.empty(); }
    };

    class Pipeline {
    public:
        Pipeline(const ToolConfiguration& config);

        // 执行整个构建流程 (Bottom-Up)
        void run();

    private:
        const ToolConfiguration& _config;
        std::vector<LodLevelConfig> _strategies;
        std::vector<SceneObject> _sceneObjects;
        std::unique_ptr<HLODNode> _root;
        std::vector<TileStats> _stats; // Collection of stats
        
        // 1. 初始化 & 扫描
        void initStrategies();
        void scanInputDirectory();
        
        // 2. 构建树结构 (Determine structure)
        void buildHlodTree();
        void recursiveSplit(HLODNode* node);
        void calculateTightBounds(HLODNode* node); // New: Post-process to shrink bounds

        // 3. 生成内容 (Bottom-Up)
        void generateContentBottomUp();
        
        // Phase A: Generate Leaf Tiles (Max Depth)
        // Returns true if content was generated
        bool generateLeafTile(HLODNode* node);
        
        // Phase B: Generate Parent Tiles (Iterative Upward)
        // Returns true if content was generated
        bool processParentTile(HLODNode* node);

        // Helper: Collect all GLB paths from children
        std::vector<std::filesystem::path> getChildrenGlbPaths(const HLODNode* node);

        // 4. 生成 Tileset.json
        void generateTilesetJson();
        void writeTilesetJsonRecursive(std::ofstream& json, const HLODNode* node, int indentLevel, bool writeBraces);

        // 5. 生成分析报告
        void writeAnalysisReport();

        // 辅助函数
        TileRole getRoleForLevel(int level) const;
        double getGeometricErrorFactor(int level) const;
        
        // Core Logic for Parent Tile Processing
        // 1. Unpack children GLBs -> 2. Simplify -> 3. Re-detect Instancing -> 4. Write
        void createParentTileContent(
            const std::vector<std::filesystem::path>& childGlbPaths,
            const std::filesystem::path& outputGlbPath,
            int level
        );
        
        // Helper to simplify a merged model using NonInstancingLOD logic
        // ratio: 0.0-1.0 (target triangle count ratio)
        CesiumGltf::Model simplifyMergedModel(const CesiumGltf::Model& inputModel, float ratio);
        
        // Helper to detect instancing on a Model in memory and return structured data for writing
        GltfInstancing::InstancingDetectionResult detectInstancingInMemory(
            const CesiumGltf::Model& model, 
            const std::string& sourceName
        );
        
        // Helper to count triangles in a model
        size_t countTriangles(const CesiumGltf::Model& model);
    };

}

#endif // HLOD_PIPELINE_H
