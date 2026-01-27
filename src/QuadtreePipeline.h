#ifndef QUADTREE_PIPELINE_H
#define QUADTREE_PIPELINE_H

#include <string>
#include <vector>
#include <memory>
#include <filesystem>
#include <map>
#include <glm/glm.hpp>
#include <CesiumGltf/Model.h>

struct ToolConfiguration; // Forward declaration

namespace QuadtreePipeline {

    // 策略角色定义
    enum class TileRole {
        Proxy,      // 远距离：包围盒/替代体
        Instancing, // 中距离：GPU 实例化
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

    // 四叉树节点
    struct QuadtreeNode {
        int level;
        int x, y; // Grid coordinates at this level
        glm::vec3 minBound; // Node spatial bounds
        glm::vec3 maxBound;
        
        std::vector<SceneObject> objects;
        std::vector<std::unique_ptr<QuadtreeNode>> children;
        
        std::string tileFilename; // Generated GLB filename
        double geometricError;
        
        bool isLeaf() const { return children.empty(); }
    };

    class Pipeline {
    public:
        Pipeline(const ToolConfiguration& config);

        // 执行整个构建流程
        void run();

    private:
        const ToolConfiguration& _config;
        std::vector<LodLevelConfig> _strategies;
        std::vector<SceneObject> _sceneObjects;
        std::unique_ptr<QuadtreeNode> _root;
        
        // 1. 初始化策略
        void initStrategies();
        
        // 2. 扫描输入目录
        void scanInputDirectory();
        
        // 3. 构建四叉树
        void buildQuadtree();
        void recursiveSplit(QuadtreeNode* node);
        
        // 4. 生成 GLB 内容
        void generateTileContent();
        void processNode(QuadtreeNode* node);
        
        // 生成不同角色的内容
        void generateProxyTile(QuadtreeNode* node, const std::filesystem::path& outputPath);
        void generateInstancingTile(QuadtreeNode* node, const std::filesystem::path& outputPath);
        void generateDetailTile(QuadtreeNode* node, const std::filesystem::path& outputPath);

        // 5. 生成 Tileset.json
        void generateTilesetJson();
        void writeTilesetJsonRecursive(std::ofstream& json, const QuadtreeNode* node, int indentLevel);

        // 辅助函数
        TileRole getRoleForLevel(int level) const;
        double getGeometricErrorFactor(int level) const;
    };

}

#endif // QUADTREE_PIPELINE_H

