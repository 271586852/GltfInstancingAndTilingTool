#ifndef TILESET_WRITER_H
#define TILESET_WRITER_H

#include "utilities.h" // For BoundingBox and logging
#include <filesystem>
#include <string>
#include <optional>
#include <iostream>
#include <vector>

// Cesium Native 3D Tiles classes
#include <Cesium3DTiles/Tileset.h>
#include <Cesium3DTiles/Tile.h>
#include <Cesium3DTiles/BoundingVolume.h>
#include <Cesium3DTiles/Content.h>
#include <Cesium3DTilesWriter/TilesetWriter.h> // For writing the tileset
#include <CesiumGltfReader/GltfReader.h>

#include <glm/glm.hpp>
#include <glm/gtc/epsilon.hpp>//equal
#include <glm/gtc/matrix_transform.hpp>//makemat4
#include <glm/gtc/type_ptr.hpp> 
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_query.hpp>

using namespace Cesium3DTiles;
using namespace CesiumGltf;
using namespace CesiumGltfReader;

namespace GltfInstancing {

    struct typeInformation {
        std::string typeName;
        int accessorIndice;
        int bufferViewIndice;
        int acc_byteOffset;
        int componentType;
        int count;
        std::string type;
        int bufferIndice;
        int buf_byteOffset;
        int byteLength;
    };

    // 树状结构的 Tileset 节点定义
    struct TilesetNode {
        BoundingBox boundingVolume;
        double geometricError;
        std::string contentUri; // 如果为空，表示中间节点
        std::vector<TilesetNode> children;
        
        // 构造函数方便使用
        TilesetNode() : geometricError(0.0) {}
    };

    class TilesetWriter {
    public:
        TilesetWriter();

        // Generates a tileset.json file.
        // glbRelativePath: The path to the GLB file, relative to the output tileset.json.
        // tilesetOutputPath: The full path where the tileset.json should be saved.
        // rootBoundingVolume: The bounding volume for the root tile.
        // geometricError: The geometric error for the root tile.
        bool writeTileset(
            const std::vector<std::filesystem::path>& uris,
            const std::filesystem::path& tilesetOutputPath,
            double geometricError = 500.0 // Default geometric error
        );

        // 新增：支持层级结构的 Tileset 写入
        bool writeHierarchicalTileset(
            const TilesetNode& rootNode,
            const std::filesystem::path& tilesetOutputPath
        );

    private:
        //Cesium3DTilesWriter::TilesetWriter _writer;
    };

} // namespace GltfInstancing

#endif // TILESET_WRITER_H
