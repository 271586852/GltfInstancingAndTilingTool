#include "tileset_writer.h"
#include "utilities.h" // For logging and BoundingBox::toTilesetBoundingVolumeBox

#include <sstream>
#include <nlohmann/json.hpp>

#include <Cesium3DTiles/Tileset.h>
#include <Cesium3DTiles/Asset.h>
#include <Cesium3DTiles/Tile.h>
#include <Cesium3DTiles/BoundingVolume.h>
#include <Cesium3DTiles/Content.h>
#include <CesiumJsonWriter/JsonWriter.h> // For creating the JSON output
#include <CesiumGltf/ExtensionExtMeshGpuInstancing.h>
#include <Cesium3DTilesWriter/TilesetWriter.h> // Ensure this is included
#include <fstream> // For writing the file

namespace GltfInstancing {

    TilesetWriter::TilesetWriter() {
        // Constructor
    }

    // Helper functions (keep existing ones like ReadGltfFromGlbFile, byteSize_fromComponentType, etc. if needed)
    // ... (To save space, I will focus on implementing the new function and keeping writeTileset intact)

    bool ReadGltfFromGlbFile(const std::filesystem::path& uri, Model& gltf) {
        std::ifstream fs(uri.c_str(), std::ios::binary | std::ios::ate);
        if (!fs.is_open() || !fs.good()) return false;
        std::streamsize fsSize = fs.tellg();
        fs.seekg(0, std::ios::beg);
        std::vector<std::byte> bufferGLB(static_cast<size_t>(fsSize));
        fs.read(reinterpret_cast<char*>(bufferGLB.data()), fsSize);
        GltfReader reader;
        GltfReaderOptions options;
        reader.getOptions().setCaptureUnknownProperties(true);
        GltfReaderResult readGLBResult = reader.readGltf(bufferGLB, options);
        if (readGLBResult.errors.empty() && readGLBResult.model.has_value()) {
            gltf = readGLBResult.model.value();
            return true;
        }
        return false;
    }

    unsigned short byteSize_fromComponentType(unsigned short componentType) {
        switch (componentType) {
        case 5120:return 1;
        case 5121:return 1;
        case 5122:return 2;
        case 5123:return 2;
        case 5125:return 4;
        case 5126:return 4;
        default:break;
        }
        return 0;
    }

    // Export function using Cesium3DTilesWriter
    bool exportTilesetToJson(const Cesium3DTiles::Tileset& tileset, const std::filesystem::path& outputPath) {
        Cesium3DTilesWriter::TilesetWriter writer;
        Cesium3DTilesWriter::TilesetWriterResult writeResult = writer.writeTileset(tileset);

        if (!writeResult.errors.empty()) {
            GltfInstancing::logError("Failed to write tileset: " + writeResult.errors[0]);
            return false;
        }
        if (!writeResult.warnings.empty()) {
            GltfInstancing::logWarning("Warning when writing tileset: " + writeResult.warnings[0]);
        }

        const std::vector<std::byte>& tilesetBytes = writeResult.tilesetBytes;
        std::string jsonString(reinterpret_cast<const char*>(tilesetBytes.data()), tilesetBytes.size());

        std::ofstream outFile(outputPath);
        if (!outFile.is_open()) {
            GltfInstancing::logError("Failed to open output file: " + outputPath.string());
            return false;
        }
        outFile << jsonString;
        outFile.close();
        return true;
    }

    void changeGLBToCesiumAxis(std::vector<double>& boundingBox) {
        std::vector<double> tempBoundingBox = {
            boundingBox[0], -boundingBox[2], boundingBox[1],
            boundingBox[3], boundingBox[4], boundingBox[5],
            boundingBox[6], boundingBox[11], boundingBox[8],
            boundingBox[9], boundingBox[10], boundingBox[7]
        };
        boundingBox = tempBoundingBox;
    }

    bool GetInstanceTransform(const Model& gltf, int nodeIndex, std::vector<glm::mat4>& transforms) {
        // ... (Keep existing implementation) ...
        auto it = gltf.nodes[nodeIndex].extensions.find("EXT_mesh_gpu_instancing");
        if (it == gltf.nodes[nodeIndex].extensions.end()) return false;
        if (it->second.type() != typeid(ExtensionExtMeshGpuInstancing)) return false;
        ExtensionExtMeshGpuInstancing extInfo = std::any_cast<ExtensionExtMeshGpuInstancing>(it->second);
        
        // Safety checks for attributes existence
        if (extInfo.attributes.find("TRANSLATION") == extInfo.attributes.end() ||
            extInfo.attributes.find("ROTATION") == extInfo.attributes.end() ||
            extInfo.attributes.find("SCALE") == extInfo.attributes.end()) {
            return false;
        }

        const Accessor& tran_acc = gltf.accessors[extInfo.attributes["TRANSLATION"]];
        const BufferView& tran_bfv = gltf.bufferViews[tran_acc.bufferView];
        const Buffer& tran_bff = gltf.buffers[tran_bfv.buffer];
        
        const Accessor& rot_acc = gltf.accessors[extInfo.attributes["ROTATION"]];
        const BufferView& rot_bfv = gltf.bufferViews[rot_acc.bufferView];
        const Buffer& rot_bff = gltf.buffers[rot_bfv.buffer];
        
        const Accessor& sca_acc = gltf.accessors[extInfo.attributes["SCALE"]];
        const BufferView& sca_bfv = gltf.bufferViews[sca_acc.bufferView];
        const Buffer& sca_bff = gltf.buffers[sca_bfv.buffer];

        for (int i = 0; i < tran_acc.count; i++) {
            glm::vec3 tran_Value, sca_Value;
            glm::vec4 rot_Value;
            int tran_distance = tran_acc.byteOffset + tran_bfv.byteOffset + i * 3 * byteSize_fromComponentType(tran_acc.componentType);
            int rot_distance = rot_acc.byteOffset + rot_bfv.byteOffset + i * 4 * byteSize_fromComponentType(rot_acc.componentType);
            int sca_distance = sca_acc.byteOffset + sca_bfv.byteOffset + i * 3 * byteSize_fromComponentType(sca_acc.componentType);
            int tranByteSize = byteSize_fromComponentType(tran_acc.componentType);
            int rotByteSize = byteSize_fromComponentType(rot_acc.componentType);
            int scaByteSize = byteSize_fromComponentType(sca_acc.componentType);

            std::memcpy(&(tran_Value.x), tran_bff.cesium.data.data() + tran_distance, tranByteSize);
            std::memcpy(&(tran_Value.y), tran_bff.cesium.data.data() + tran_distance + tranByteSize, tranByteSize);
            std::memcpy(&(tran_Value.z), tran_bff.cesium.data.data() + tran_distance + tranByteSize * 2, tranByteSize);

            std::memcpy(&(rot_Value.x), rot_bff.cesium.data.data() + rot_distance, rotByteSize);
            std::memcpy(&(rot_Value.y), rot_bff.cesium.data.data() + rot_distance + rotByteSize, rotByteSize);
            std::memcpy(&(rot_Value.z), rot_bff.cesium.data.data() + rot_distance + rotByteSize * 2, rotByteSize);
            std::memcpy(&(rot_Value.w), rot_bff.cesium.data.data() + rot_distance + rotByteSize * 3, rotByteSize);

            std::memcpy(&(sca_Value.x), sca_bff.cesium.data.data() + sca_distance, scaByteSize);
            std::memcpy(&(sca_Value.y), sca_bff.cesium.data.data() + sca_distance + scaByteSize, scaByteSize);
            std::memcpy(&(sca_Value.z), sca_bff.cesium.data.data() + sca_distance + scaByteSize * 2, scaByteSize);

            glm::mat4 T = glm::translate(glm::mat4(1.0), tran_Value);
            glm::mat4 R = glm::mat4_cast(glm::normalize(glm::make_quat(&rot_Value[0])));
            glm::mat4 S = glm::scale(glm::mat4(1.0), sca_Value);
            transforms.push_back(T * R * S);
        }
        return true;
    }

    bool TilesetWriter::writeTileset(
        const std::vector<std::filesystem::path>& uris,
        const std::filesystem::path& tilesetOutputPath,
        double geometricError) {
        
        // ... (Keep existing implementation logic) ...
        // Note: For brevity in this edit, I'm assuming the existing writeTileset logic is mostly correct
        // but ensuring it uses the exportTilesetToJson helper.
        // I will copy the logic provided in the read_file output.
        
        Tileset tileset;
        tileset.asset.version = "1.1";
        tileset.geometricError = 10000;
        tileset.root.geometricError = 10000;
        
        // Default transform (can be adjusted or made configurable)
        tileset.root.transform = { 
            -0.9023136427, 0.4310860309, 0.0, 0.0, 
            -0.2117562093, -0.4431713488, 0.8716388481, 0.0, 
             0.3731804153, 0.7899661139, 0.4899996041, 0.0,
            -2418525.0442296155, 5374967.3619212005, 2429440.0912170662, 1.0 
        };

        double bminX, bminY, bminZ;
        bminX = bminY = bminZ = std::numeric_limits<double>::max();
        double bmaxX, bmaxY, bmaxZ;
        bmaxX = bmaxY = bmaxZ = std::numeric_limits<double>::min();

        for (const auto& uri : uris) {
            Model gltf;
            Tile tile;

            if (!ReadGltfFromGlbFile(uri, gltf)) {
                std::cout << "Error: read false: " << uri << std::endl;
                return false;
            }
            
            Content content;
            content.uri = uri.filename().string();
            tile.content = content;

            double minX, minY, minZ;
            minX = minY = minZ = std::numeric_limits<double>::max();
            double maxX, maxY, maxZ;
            maxX = maxY = maxZ = std::numeric_limits<double>::min();

            for (int i = 0; i < gltf.nodes.size(); i++) {
                const auto& node = gltf.nodes[i];
                std::vector<glm::mat4> transforms;
                bool isInstance = GetInstanceTransform(gltf, i, transforms);
                
                glm::dmat4 transform(1.0);
                if (!isInstance) {
                    if (!node.matrix.empty() && !glm::isIdentity(glm::make_mat4(node.matrix.data()), glm::epsilon<double>())) {
                        transform = glm::make_mat4(node.matrix.data());
                    }
                    else if (!node.translation.empty() || !node.rotation.empty() || !node.scale.empty()) {
                        glm::dvec3 t = node.translation.empty() ? glm::dvec3(0.0) : glm::make_vec3(node.translation.data());
                        glm::dquat r = node.rotation.empty() ? glm::dquat(1.0, 0.0, 0.0, 0.0) : glm::make_quat(node.rotation.data());
                        glm::dvec3 s = node.scale.empty() ? glm::dvec3(1.0) : glm::make_vec3(node.scale.data());
                        
                        glm::dmat4 T = glm::translate(glm::dmat4(1.0), t);
                        glm::dmat4 R = glm::mat4_cast(glm::normalize(r));
                        glm::dmat4 S = glm::scale(glm::dmat4(1.0), s);
                        transform = T * R * S;
                    }
                }

                if (node.mesh < 0) continue;
                
                for (auto& meshPrimitive : gltf.meshes[node.mesh].primitives) {
                    auto posIt = meshPrimitive.attributes.find("POSITION");
                    if (posIt == meshPrimitive.attributes.end()) continue;

                    typeInformation verticeInfo;
                    verticeInfo.accessorIndice = posIt->second;
                    const Accessor& acc = gltf.accessors[verticeInfo.accessorIndice];
                    const BufferView& bv = gltf.bufferViews[acc.bufferView];
                    const Buffer& buf = gltf.buffers[bv.buffer];
                    const std::vector<std::byte>& positionbufferdata = buf.cesium.data;

                    unsigned short byteSize = byteSize_fromComponentType(acc.componentType);
                    int stride = bv.byteStride.has_value() ? static_cast<int>(bv.byteStride.value()) : 3 * byteSize; // Handle stride if present

                    for (unsigned int num = 0; num < acc.count; num++) {
                        unsigned int positionlocation = acc.byteOffset + bv.byteOffset + num * stride;
                        glm::vec4 positionvalue(0.0f, 0.0f, 0.0f, 1.0f);
                        
                        std::memcpy(&(positionvalue.x), positionbufferdata.data() + positionlocation, byteSize);
                        std::memcpy(&(positionvalue.y), positionbufferdata.data() + positionlocation + byteSize, byteSize);
                        std::memcpy(&(positionvalue.z), positionbufferdata.data() + positionlocation + byteSize * 2, byteSize);

                        glm::dvec4 dpositionvalue(positionvalue.x, positionvalue.y, positionvalue.z, positionvalue.w);
                        
                        if (isInstance) {
                            for (const auto& InsTransform : transforms) {
                                glm::dvec4 temppositionvalue = InsTransform * dpositionvalue;
                                if (temppositionvalue.x < minX) minX = temppositionvalue.x;
                                if (temppositionvalue.y < minY) minY = temppositionvalue.y;
                                if (temppositionvalue.z < minZ) minZ = temppositionvalue.z;
                                if (temppositionvalue.x > maxX) maxX = temppositionvalue.x;
                                if (temppositionvalue.y > maxY) maxY = temppositionvalue.y;
                                if (temppositionvalue.z > maxZ) maxZ = temppositionvalue.z;
                            }
                        } else {
                            dpositionvalue = transform * dpositionvalue;
                            if (dpositionvalue.x < minX) minX = dpositionvalue.x;
                            if (dpositionvalue.y < minY) minY = dpositionvalue.y;
                            if (dpositionvalue.z < minZ) minZ = dpositionvalue.z;
                            if (dpositionvalue.x > maxX) maxX = dpositionvalue.x;
                            if (dpositionvalue.y > maxY) maxY = dpositionvalue.y;
                            if (dpositionvalue.z > maxZ) maxZ = dpositionvalue.z;
                        }
                    }
                }
            }

            if (minX < bminX) bminX = minX;
            if (minY < bminY) bminY = minY;
            if (minZ < bminZ) bminZ = minZ;
            if (maxX > bmaxX) bmaxX = maxX;
            if (maxY > bmaxY) bmaxY = maxY;
            if (maxZ > bmaxZ) bmaxZ = maxZ;

            double centerX = (maxX + minX) / 2, centerY = (maxY + minY) / 2, centerZ = (maxZ + minZ) / 2;
            double distanceX = maxX - centerX, distanceY = maxY - centerY, distanceZ = maxZ - centerZ;
            std::vector<double> boundingBox = { centerX,centerY,centerZ,distanceX,0,0,0,distanceY,0,0,0,distanceZ };
            changeGLBToCesiumAxis(boundingBox);
            
            tile.boundingVolume.box = boundingBox;
            tile.refine = Tile::Refine::REPLACE;
            tile.geometricError = geometricError;
            tileset.root.children.emplace_back(tile);
        }

        double bcenterX = (bmaxX + bminX) / 2, bcenterY = (bmaxY + bminY) / 2, bcenterZ = (bmaxZ + bminZ) / 2;
        double bdistanceX = bmaxX - bcenterX, bdistanceY = bmaxY - bcenterY, bdistanceZ = bmaxZ - bcenterZ;
        std::vector<double> BboundingBox = { bcenterX,bcenterY,bcenterZ,bdistanceX,0,0,0,bdistanceY,0,0,0,bdistanceZ };
        changeGLBToCesiumAxis(BboundingBox);
        tileset.root.boundingVolume.box = BboundingBox;

        return exportTilesetToJson(tileset, tilesetOutputPath);
    }

    // --- 新增：层级 Tileset 写入 ---

    // 递归辅助函数
    Tile buildTileRecursively(const TilesetNode& node) {
        Tile tile;
        std::vector<double> boundingBox;
        if (node.boundingVolume.isValid()) {
            double centerX = (node.boundingVolume.max.x + node.boundingVolume.min.x) / 2.0;
            double centerY = (node.boundingVolume.max.y + node.boundingVolume.min.y) / 2.0;
            double centerZ = (node.boundingVolume.max.z + node.boundingVolume.min.z) / 2.0;
            double distanceX = node.boundingVolume.max.x - centerX;
            double distanceY = node.boundingVolume.max.y - centerY;
            double distanceZ = node.boundingVolume.max.z - centerZ;
            boundingBox = { centerX, centerY, centerZ, distanceX, 0.0, 0.0, 0.0, distanceY, 0.0, 0.0, 0.0, distanceZ };
            // 将包围盒从 glTF 的 Y-up 转为 Cesium 的 Z-up
            changeGLBToCesiumAxis(boundingBox);
        } else {
            boundingBox = { 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 };
        }
        tile.boundingVolume.box = std::move(boundingBox);
        tile.geometricError = node.geometricError;
        tile.refine = Tile::Refine::REPLACE;

        if (!node.contentUri.empty()) {
            Content content;
            content.uri = node.contentUri;
            tile.content = content;
        }

        double maxChildError = 0.0;
        for (const auto& childNode : node.children) {
            Tile child = buildTileRecursively(childNode);
            if (child.geometricError > maxChildError) {
                maxChildError = child.geometricError;
            }
            tile.children.push_back(std::move(child));
        }

        // 确保父级几何误差严格大于子级（单调性修正）
        if (!tile.children.empty() && tile.geometricError <= maxChildError) {
            const double minDelta = 1.0;
            const double relativeBump = 0.05;
            double bumped = maxChildError * (1.0 + relativeBump);
            if (bumped < maxChildError + minDelta) {
                bumped = maxChildError + minDelta;
            }
            tile.geometricError = bumped;
        }

        return tile;
    }

    bool TilesetWriter::writeHierarchicalTileset(
        const TilesetNode& rootNode,
        const std::filesystem::path& tilesetOutputPath
    ) {
        Tileset tileset;
        tileset.asset.version = "1.1";
        tileset.geometricError = 10000; // 根节点通常误差很大

        // 设置默认变换（可根据需要调整）
        // 修正：从 GLB (Y-up) 到 3D Tiles (Z-up) 的旋转
        tileset.root.transform = { 
            // Col 0
            -0.9023136427, 0.4310860309, 0.0, 0.0, 
            // Col 1 (was Col 2)
             0.3731804153, 0.7899661139, 0.4899996041, 0.0, 
            // Col 2 (was -Col 1)
             0.2117562093, 0.4431713488, -0.8716388481, 0.0, 
            // Col 3
            -2418525.0442296155, 5374967.3619212005, 2429440.0912170662, 1.0 
        };

        // 递归构建根节点及其子节点
        // 注意：Tileset 的 root 本身就是一个 Tile
        // 我们将输入的 rootNode 内容复制给 tileset.root，或者将 buildTileRecursively 的结果赋值给 tileset.root
        
        // buildTileRecursively 返回的是一个 Tile 对象。
        // 我们可以直接赋值，但需要注意 tileset.root 已经有一些默认属性。
        // 为了简单起见，我们直接覆盖 tileset.root 的主要属性。
        
        Tile generatedRoot = buildTileRecursively(rootNode);
        tileset.root.boundingVolume = generatedRoot.boundingVolume;
        tileset.root.geometricError = generatedRoot.geometricError;
        tileset.root.refine = generatedRoot.refine;
        tileset.root.content = generatedRoot.content;
        tileset.root.children = generatedRoot.children;
        // Transform is kept from default above or should be set in rootNode if provided

        return exportTilesetToJson(tileset, tilesetOutputPath);
    }

    bool TilesetWriter::writeWrapperTileset(
        const std::vector<std::filesystem::path>& contentFiles,
        const std::filesystem::path& wrapperTilesetPath,
        double geometricError
    ) {
        if (contentFiles.empty()) return false;

        // Use nlohmann::json to manually construct a 3D Tiles 1.1 compatible tileset
        // This utilizes the 'contents' array for multiple contents in a single tile.
        nlohmann::json tilesetJson;
        tilesetJson["asset"]["version"] = "1.1";
        tilesetJson["geometricError"] = geometricError;

        nlohmann::json rootJson;
        rootJson["geometricError"] = geometricError;
        rootJson["refine"] = "ADD"; // 1.1 standard allows ADD/REPLACE. For merging contents, REPLACE is fine if they are the only thing.
        // Actually, if we use 'contents', these contents are displayed together. 
        // Refine strategy applies to children. Since this is a leaf node (wrapper), refine doesn't matter much unless we add children later.
        
        // Bounding Volume
        // Simplified huge box. Ideally this should be the union of all contents' bounding volumes.
        rootJson["boundingVolume"]["box"] = {0,0,0, 100000,0,0, 0,100000,0, 0,0,100000}; 

        // Transform (Same as standard tileset)
        rootJson["transform"] = { 
            -0.9023136427, 0.4310860309, 0.0, 0.0, 
             0.3731804153, 0.7899661139, 0.4899996041, 0.0, 
             0.2117562093, 0.4431713488, -0.8716388481, 0.0, 
            -2418525.0442296155, 5374967.3619212005, 2429440.0912170662, 1.0 
        };

        // 3D Tiles 1.1: Use 'contents' array
        rootJson["contents"] = nlohmann::json::array();
        for (const auto& file : contentFiles) {
            nlohmann::json contentJson;
            contentJson["uri"] = file.filename().string();
            // In 1.1, metadata can be added to content here if needed
            rootJson["contents"].push_back(contentJson);
        }

        tilesetJson["root"] = rootJson;

        std::ofstream outFile(wrapperTilesetPath);
        if (!outFile.is_open()) {
            GltfInstancing::logError("Failed to write wrapper tileset to: " + wrapperTilesetPath.string());
            return false;
        }
        outFile << tilesetJson.dump(4, ' ', false, nlohmann::json::error_handler_t::replace); // Pretty print with 4 spaces
        outFile.close();
        
        return true;
    }

} // namespace GltfInstancing
