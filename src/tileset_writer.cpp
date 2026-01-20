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
#include <fstream> // For writing the file

namespace GltfInstancing {

    TilesetWriter::TilesetWriter() {
        // Constructor, if any specific setup for _writer is needed.
    }

    bool ReadGltfFromGlbFile(
        const std::filesystem::path& uri,
        Model& gltf) {
        std::ifstream fs(
            uri.c_str(),
            std::ios::binary | std::ios::ate); // 获取文件流
        if (!fs.is_open() || !fs.good())
            return false;

        std::streamsize fsSize = fs.tellg(); // 获取文件流的大小
        fs.seekg(0, std::ios::beg);          // 指针归位到起点
        std::vector<std::byte> bufferGLB(
            static_cast<size_t>(fsSize)); // 创建一个空间存放数据
        fs.read(
            reinterpret_cast<char*>(bufferGLB.data()),
            fsSize); // 开始读取，并存放到buffer中

        GltfReader reader;
        GltfReaderOptions options;

        //将部分option变成false，从而尽可能减少读取时间，加快速度
        /*options.applyTextureTransform = false;
        options.clearDecodedDataUrls = false;
        options.decodeDataUrls = false;
        options.decodeDraco = false;
        options.decodeEmbeddedImages = false;
        options.decodeMeshOptData = false;
        options.dequantizeMeshData = false;
        options.resolveExternalImages = false;*/

        reader.getOptions().setCaptureUnknownProperties(true);
        GltfReaderResult readGLBResult = reader.readGltf(bufferGLB, options);

        if (readGLBResult.errors.empty() && readGLBResult.model.has_value()) {
            gltf = readGLBResult.model.value();
            return true;
        }
    }

    unsigned short byteSize_fromComponentType(unsigned short componentType)
    {
        switch (componentType)
        {
        case 5120:return 1;//GL_BYTE
        case 5121:return 1;//GL_UNSIGNED_BYTE
        case 5122:return 2;//GL_SHORT
        case 5123:return 2;//GL_UNSIGNED_SHORT
        case 5125:return 4;//GL_UNSIGNED_INT
        case 5126:return 4;//GL_FLOAT
        default:break;
        }
        return 0;
    }

    bool exportTilesetToJson(
        const Cesium3DTiles::Tileset& tileset,
        const std::filesystem::path& outputPath)
    {
        // 1. 创建 TilesetWriter 并序列化 Tileset
        Cesium3DTilesWriter::TilesetWriter writer;
        Cesium3DTilesWriter::TilesetWriterResult writeResult = writer.writeTileset(tileset);

        // 2. 检查错误和警告
        if (!writeResult.errors.empty()) {
            throw std::runtime_error("Failed to write tileset: " + writeResult.errors[0]);
            return false;
        }
        if (!writeResult.warnings.empty()) {
            // 可以选择记录警告或忽略
            std::cerr << "Warning when writing tileset: " << writeResult.warnings[0] << std::endl;
            return false;
        }

        // 3. 将二进制数据转换为字符串
        const std::vector<std::byte>& tilesetBytes = writeResult.tilesetBytes;
        std::string jsonString(
            reinterpret_cast<const char*>(tilesetBytes.data()),
            tilesetBytes.size());

        // 4. 写入文件
        std::ofstream outFile(outputPath);
        if (!outFile.is_open()) {
            throw std::runtime_error("Failed to open output file: " + outputPath.string());
            return false;
        }
        outFile << jsonString;
        outFile.close();
    }

    void changeGLBToCesiumAxis(std::vector<double>& boundingBox) {
        std::vector<double> tempBoundingBox = {
            boundingBox[0],
            -boundingBox[2],
            boundingBox[1],
            boundingBox[3],
            boundingBox[4],
            boundingBox[5],
            boundingBox[6],
            boundingBox[11],
            boundingBox[8],
            boundingBox[9],
            boundingBox[10],
            boundingBox[7]
        };
        boundingBox = tempBoundingBox;
    }

    bool GetInstanceTransform(const Model& gltf, int nodeIndex, std::vector<glm::mat4>& transforms) {
        auto it = gltf.nodes[nodeIndex].extensions.find("EXT_mesh_gpu_instancing");
        if (it == gltf.nodes[nodeIndex].extensions.end()) {
            return false;
        }
        if (it->second.type() != typeid(ExtensionExtMeshGpuInstancing)) {
            return false;
        }
        //将extension中的instance部分转换成其特有的结构
        ExtensionExtMeshGpuInstancing extInfo = std::any_cast<ExtensionExtMeshGpuInstancing>(it->second);
        //获取每个矩阵对应的accessor索引
        extInfo.attributes["TRANSLATION"];
        extInfo.attributes["ROTATION"];
        extInfo.attributes["SCALE"];
        const Accessor& tran_acc = gltf.accessors[extInfo.attributes["TRANSLATION"]];
        const BufferView& tran_bfv = gltf.bufferViews[tran_acc.bufferView];
        const Buffer& tran_bff = gltf.buffers[tran_bfv.buffer];
        const Accessor& rot_acc = gltf.accessors[extInfo.attributes["ROTATION"]];
        const BufferView& rot_bfv = gltf.bufferViews[rot_acc.bufferView];
        const Buffer& rot_bff = gltf.buffers[rot_bfv.buffer];
        const Accessor& sca_acc = gltf.accessors[extInfo.attributes["SCALE"]];
        const BufferView& sca_bfv = gltf.bufferViews[sca_acc.bufferView];
        const Buffer& sca_bff = gltf.buffers[sca_bfv.buffer];
        //遍历该node的所有转移矩阵（循环数量代表实例的数量）
        for (int i = 0; i < tran_acc.count; i++) {
            glm::vec3 tran_Value, sca_Value;
            glm::vec4 rot_Value;
            int tran_distance = tran_acc.byteOffset + tran_bfv.byteOffset;
            int rot_distance = rot_acc.byteOffset + rot_bfv.byteOffset;
            int sca_distance = sca_acc.byteOffset + sca_bfv.byteOffset;
            int tranByteSize = byteSize_fromComponentType(tran_acc.componentType);
            int rotByteSize = byteSize_fromComponentType(rot_acc.componentType);
            int scaByteSize = byteSize_fromComponentType(sca_acc.componentType);

            //把byte数据转换成对应的类型
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

            tran_distance += byteSize_fromComponentType(tran_acc.componentType) * 3;
            rot_distance += byteSize_fromComponentType(rot_acc.componentType) * 4;
            sca_distance += byteSize_fromComponentType(sca_acc.componentType) * 3;

            glm::mat4 T = glm::translate(glm::mat4(1.0), tran_Value);
            glm::mat4 R = glm::mat4_cast(glm::normalize(glm::make_quat(&rot_Value[0]))); // 四元数转矩阵
            glm::mat4 S = glm::scale(glm::mat4(1.0), sca_Value);
            glm::mat4 transform = T * R * S;
            transforms.push_back(transform);
        }
        return true;
    }

    bool TilesetWriter::writeTileset(
        const std::vector<std::filesystem::path>& uris,
        const std::filesystem::path& tilesetOutputPath,
        double geometricError) {
        Tileset tileset;
        tileset.asset.version = "1.1";
        tileset.geometricError = 10000;
        tileset.root.geometricError = 10000;
        tileset.root.transform =
        { -0.9023136427, 0.4310860309, 0.0, 0.0, -0.2117562093, -0.4431713488,
          0.8716388481, 0.0, 0.3731804153, 0.7899661139, 0.4899996041, 0.0,
          -2418525.0442296155, 5400267.3619212005, 2429440.0912170662, 1.0 };
        //最外层包围盒
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
            //设置uri
            Content content;
            content.uri = uri.filename().string();
            tile.content = content;//存储uri
            //计算包围盒
            //遍历glb所有顶点
            double minX, minY, minZ;
            minX = minY = minZ = std::numeric_limits<double>::max();
            double maxX, maxY, maxZ;
            maxX = maxY = maxZ = std::numeric_limits<double>::min();

            for (int i = 0; i < gltf.nodes.size(); i++) {
                const auto& node = gltf.nodes[i];
                glm::dmat4 transform;
                std::vector<glm::mat4> transforms;
                bool isInstance = GetInstanceTransform(gltf, i, transforms);
                //计算转移矩阵
                //判断是实例还是非实例
                if (!isInstance) {
                    if (!node.matrix.empty() && !glm::isIdentity(glm::make_mat4(node.matrix.data()), glm::epsilon<double>())) {//如果matrix不为空且单位矩阵的话
                        transform = glm::make_mat4(node.matrix.data());
                    }
                    //if (!node.matrix.empty()) {//如果存在matrix的话
                    //    transform = glm::make_mat4(node.matrix.data());
                    //}
                    else {//如果不存在matrix的话，使用TRS
                        glm::dvec3 b = glm::make_vec3(node.translation.data());
                        glm::dmat4 T = glm::translate(glm::dmat4(1.0), b);
                        glm::dmat4 R = glm::mat4_cast(glm::normalize((glm::make_quat(node.rotation.data())))); // 四元数转矩阵
                        glm::dmat4 S = glm::scale(glm::dmat4(1.0), glm::make_vec3(node.scale.data()));
                        transform = T * R * S;
                    }
                }


                if (node.mesh < 0) continue;
                //遍历mesh
                for (auto& meshPrimitive : gltf.meshes[node.mesh].primitives) {
                    //2.存储每个顶点或三角面的信息（与对应的tile关联）//不进行draco解压缩的话不要执行下面的代码
                    //要存储的数据类型
                    typeInformation verticeInfo;
                    //获取类型名称和accessor索引
                    //顶点
                    verticeInfo = { "POSITION", meshPrimitive.attributes.find("POSITION")->second };
                    //继续完善各个信息
                    verticeInfo.bufferViewIndice = gltf.accessors[verticeInfo.accessorIndice].bufferView;
                    verticeInfo.acc_byteOffset = gltf.accessors[verticeInfo.accessorIndice].byteOffset;
                    verticeInfo.componentType = gltf.accessors[verticeInfo.accessorIndice].componentType;
                    verticeInfo.count = gltf.accessors[verticeInfo.accessorIndice].count;
                    verticeInfo.type = gltf.accessors[verticeInfo.accessorIndice].type;
                    //在bufferview中获取
                    verticeInfo.bufferIndice = gltf.bufferViews[verticeInfo.bufferViewIndice].buffer;
                    verticeInfo.buf_byteOffset = gltf.bufferViews[verticeInfo.bufferViewIndice].byteOffset;
                    verticeInfo.byteLength = gltf.bufferViews[verticeInfo.bufferViewIndice].byteLength;
                    //获取对应的buffer数据
                    const std::vector<std::byte>& positionbufferdata = gltf.buffers[verticeInfo.bufferIndice].cesium.data;

                    //xyz中每个值占用的字节大小
                    unsigned short byteSize = byteSize_fromComponentType(verticeInfo.componentType);
                    for (unsigned int num = 0; num < verticeInfo.count; num++) {
                        //位置
                        unsigned int positionlocation = verticeInfo.acc_byteOffset + verticeInfo.buf_byteOffset +
                            num * 3 * byteSize;
                        glm::vec4 positionvalue;
                        positionvalue.w = 1.0;
                        //把byte数据转换成对应的类型
                        std::memcpy(&(positionvalue.x), positionbufferdata.data() + positionlocation, byteSize);
                        std::memcpy(&(positionvalue.y), positionbufferdata.data() + positionlocation + byteSize, byteSize);
                        std::memcpy(&(positionvalue.z), positionbufferdata.data() + positionlocation + byteSize * 2, byteSize);
                        //计算应用转移矩阵之后的坐标
                        glm::dvec4 dpositionvalue(positionvalue.x, positionvalue.y, positionvalue.z, positionvalue.w);
                        if (isInstance) {//如果是实例，则遍历实例转移矩阵，计算每个实例的坐标，并比较最值
                            for (const auto& InsTransform : transforms) {
                                glm::dvec4 temppositionvalue = InsTransform * dpositionvalue;
                                //比较大小
                                if (temppositionvalue.x < minX) minX = temppositionvalue.x;
                                if (temppositionvalue.y < minY) minY = temppositionvalue.y;
                                if (temppositionvalue.z < minZ) minZ = temppositionvalue.z;
                                if (temppositionvalue.x > maxX) maxX = temppositionvalue.x;
                                if (temppositionvalue.y > maxY) maxY = temppositionvalue.y;
                                if (temppositionvalue.z > maxZ) maxZ = temppositionvalue.z;
                            }
                        }
                        else {//如果是非实例，则直接用计算的单个转移矩阵乘顶点坐标
                            dpositionvalue = transform * dpositionvalue;
                            //比较大小
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
            //对每个glb比较其最大值最小值，为最大包围盒做准备
            if (minX < bminX) bminX = minX;
            if (minY < bminY) bminY = minY;
            if (minZ < bminZ) bminZ = minZ;
            if (maxX > bmaxX) bmaxX = maxX;
            if (maxY > bmaxY) bmaxY = maxY;
            if (maxZ > bmaxZ) bmaxZ = maxZ;

            double centerX = (maxX + minX) / 2, centerY = (maxY + minY) / 2, centerZ = (maxZ + minZ) / 2;
            double distanceX = maxX - centerX, distanceY = maxY - centerY, distanceZ = maxZ - centerZ;
            std::vector<double> boundingBox = { centerX,centerY,centerZ,distanceX,0,0,0,distanceY,0,0,0,distanceZ };
            //将包围盒从gltf的y向上坐标系转为cesium的z向上坐标系
            changeGLBToCesiumAxis(boundingBox);
            //将包围盒输入到tile中
            tile.boundingVolume.box = boundingBox;
            //设置refine
            tile.refine = "REPLACE";
            //设置geometricerror
            tile.geometricError = geometricError;
            //将tile加入到tileset中
            tileset.root.children.emplace_back(tile);
        }
        //制作根节点最大包围盒
        double bcenterX = (bmaxX + bminX) / 2, bcenterY = (bmaxY + bminY) / 2, bcenterZ = (bmaxZ + bminZ) / 2;
        double bdistanceX = bmaxX - bcenterX, bdistanceY = bmaxY - bcenterY, bdistanceZ = bmaxZ - bcenterZ;
        std::vector<double> BboundingBox = { bcenterX,bcenterY,bcenterZ,bdistanceX,0,0,0,bdistanceY,0,0,0,bdistanceZ };
        //将包围盒从gltf的y向上坐标系转为cesium的z向上坐标系
        changeGLBToCesiumAxis(BboundingBox);
        tileset.root.boundingVolume.box = BboundingBox;
        //输出json
        if (!exportTilesetToJson(tileset, tilesetOutputPath)) {
            return false;
        }
        return true;
    }
        


    //bool TilesetWriter::writeTileset(
    //    const std::filesystem::path& glbRelativePath,
    //    const std::filesystem::path& tilesetOutputPath,
    //    const BoundingBox& rootBoundingBox,
    //    double geometricError) {

    //    logMessage("Generating tileset.json: " + tilesetOutputPath.string());

    //    Cesium3DTiles::Tileset tileset;

    //    // Asset (required)
    //    tileset.asset.version = "1.1"; // 3D Tiles version
    //    tileset.asset.tilesetVersion = "1.0.0"; // User-defined version for this specific tileset

    //    // Geometric Error for the root (required)
    //    tileset.geometricError = geometricError;

    //    // Root Tile (required)
    //    Cesium3DTiles::Tile rootTile;

    //    // Bounding Volume for the root tile (required)
    //    if (rootBoundingBox.isValid()) {
    //        rootTile.boundingVolume.box = rootBoundingBox.toTilesetBoundingVolumeBox();
    //    }
    //    else {
    //        logError("Root bounding box is invalid. Cannot generate tileset.");
    //        // As a fallback, one could use a placeholder box, but it's better to ensure valid input.
    //        // Example placeholder: rootTile.boundingVolume.box = {0,0,0, 1,0,0, 0,1,0, 0,0,1}; // Unit box at origin
    //        return false;
    //    }

    //    // Geometric error for the root tile (can be same as tileset's or more refined)
    //    rootTile.geometricError = geometricError; // Can be refined if there are children

    //    // Content for the root tile (pointing to the GLB)
    //    Cesium3DTiles::Content content;
    //    content.uri = glbRelativePath.generic_string(); // Use generic_string for cross-platform paths
    //    rootTile.content = content;

    //    // Add extensions to the root tile if needed (e.g., implicit tiling extension metadata)
    //    // For a single GLB, this is usually not complex.

    //    // Add the root tile to the tileset
    //    tileset.root = rootTile;


    //    // Optional: Add other tileset properties like properties, extensionsUsed, extensionsRequired, etc.
    //    // tileset.extensionsUsed.push_back("SOME_EXTENSION");

    //    // Serialize the tileset to JSON
    //    CesiumJsonWriter::JsonWriter jsonWriter;
    //    // jsonWriter.setIndent(2); // For pretty printing, if desired

    //    // Use TilesetWriter to serialize the tileset object
    //    // The TilesetWriter class in Cesium Native is more about packaging tilesets (e.g., into .3tz)
    //    // For writing a simple tileset.json, we serialize the Cesium3DTiles::Tileset object directly.
    //    // Let's check how Cesium3DTilesWriter::TilesetWriter is intended to be used.
    //    // Looking at Cesium Native examples, `TilesetWriter::write()` seems to be for .3tz or directory structures.
    //    // For a single tileset.json, serializing the `Tileset` object to a JSON string is the way.

    //    // Correct approach: Serialize the Tileset object to JSON.
    //    // Cesium Native provides serialization functions for its objects.
    //    // We need to find the correct function to call or use nlohmann::json if direct serialization isn't obvious.
    //    // After checking Cesium Native's structure, serialization is typically handled by functions like:
    //    // `Cesium3DTiles::TilesetJsonWriter::write(tileset, jsonWriter);` or similar pattern.
    //    // Let's assume such a utility exists or build it.

    //    // For 3D Tiles 1.1, Cesium Native typically uses nlohmann::json for internal representation
    //    // and has `toJsonValue` methods.
    //    // Example pattern:
    //    // nlohmann::json tilesetJsonValue = Cesium3DTiles::Tileset::toJson(tileset);
    //    // However, the direct `toJson` might not be public or straightforward.
    //    // A more robust way is to use the provided writer infrastructure if available.

    //    // Let's use the standard way Cesium Native suggests for writing:
    //    // This involves creating a `CesiumJsonWriter::JsonOutputStream` and then calling a write function.

    //    std::stringbuf stringBuffer;
    //    CesiumJsonWriter::JsonOutputStream outputStream(stringBuffer);
    //    outputStream.setPrettyPrint(true); // Enable pretty printing

    //    // Get the writer context from the TilesetWriter
    //    // No, the TilesetWriter itself is for more complex scenarios (like .3tz).
    //    // We need to use the serialization functions from Cesium3DTiles directly.
    //    // This usually involves including specific writer headers if they exist, e.g. "TilesetWriter.h"
    //    // from Cesium3DTiles, or using the nlohmann::json conversion.

    //    // Let's use the direct nlohmann::json approach as Cesium Native structures are often convertible.
    //    // (This might require including nlohmann/json directly if not already implicitly via other Cesium headers)
    //    // #include <nlohmann/json.hpp> -- should be available via Cesium Native

    //    // Check if Cesium3DTiles::Tileset has a direct way to serialize.
    //    // Yes, Cesium Native's `TilesetWriter` class (from `Cesium3DTilesWriter`)
    //    // is NOT for writing a single JSON. It's for packaging.
    //    // We need to use the JsonWriter with the model.

    //    // The correct way to serialize with Cesium Native's system (if not writing a .3tz):
    //    // You typically populate the Cesium3DTiles::Tileset object and then serialize it to a string/file.
    //    // The `TilesetWriter` in `Cesium3DTilesWriter` is for `*.3tz` or tile directories.
    //    // For a simple `tileset.json`, we need to convert the `Cesium3DTiles::Tileset` object to JSON.
    //    // This is usually done by calling `writeJson` static methods available for each type or using a general writer.

    //    // The structure is:
    //    // Cesium3DTiles::Tileset tileset;
    //    // ... populate tileset ...
    //    // CesiumJsonWriter::JsonWriter writer;
    //    // TilesetJsonWriter::write(tileset, writer); // This is a hypothetical writer class/namespace
    //    // std::string jsonString = writer.toString();

    //    // After reviewing Cesium Native's capabilities, `Cesium3DTilesWriter::TilesetWriter`
    //    // indeed has a method to write just the `tileset.json` to a directory.
    //    // However, it expects to manage assets. For a single, simple tileset.json,
    //    // it might be overkill or not the most direct route.

    //    // A more direct route if you have the Tileset object:
    //    // nlohmann::json jsonOutput;
    //    // Cesium3DTiles::Tileset::writeJson(tileset, jsonOutput); // Hypothetical
    //    // std::string tilesetString = jsonOutput.dump(2);

    //    // Let's use the intended Cesium Native way if possible for a standalone tileset.json.
    //    // Cesium Native's test cases often show direct construction of JSON via nlohmann::json
    //    // or using its internal writers. `Cesium3DTiles::TilesetWriter` doesn't seem to have a
    //    // simple "write this Tileset object as JSON to this path" function.

    //    // Simplest is often to construct the nlohmann::json manually or via helper.
    //    // However, since we have the `Cesium3DTiles::Tileset` object, we want to serialize IT.
    //    // Cesium Native does have `Cesium3DTiles::TilesetJsonHandler` for reading,
    //    // implies there should be a symmetric way of writing.

    //    // Let's try with CesiumJsonWriter directly:
    //    // `Cesium3DTiles::writeTileset(tileset, outputStream);` assuming a `writeTileset` function exists in a proper namespace.

    //    // Final attempt using the more common pattern seen in Cesium Native for writing JSON:
    //    // Need to find the specific "write" function for `Cesium3DTiles::Tileset`.
    //    // It's often `Namespace::TypeWriter::write(&typeInstance, jsonOutputStream)`.
    //    // For Tileset, this would likely be in `Cesium3DTiles::` namespace.
    //    // The header `Cesium3DTiles/TilesetWriter.h` (if it exists for JSON, not package) or a similar one.

    //    // It appears the most straightforward way to serialize a `Cesium3DTiles::Tileset` object
    //    // into a JSON string within Cesium Native, without involving the full `Cesium3DTilesWriter::TilesetWriter`
    //    // packaging logic, is to use the internal serialization mechanism that populates a `CesiumJsonWriter::JsonValue`.
    //    // Then, this `JsonValue` can be written to a stream.
    //    // Example:
    //    // CesiumJsonWriter::JsonValue tilesetJsonValue = Cesium3DTiles::TilesetWriter::toJson(tileset); // This is often an internal detail.

    //    // Given the libraries:
    //    // `Cesium3DTilesWriter::TilesetWriter _writer;` (from .h) this class is for packaging.
    //    // For simply generating the JSON text for a `Cesium3DTiles::Tileset` object:
    //    // The `Cesium3DTiles` library itself should contain the serialization logic.

    //    // Re-evaluating `Cesium3DTilesWriter::TilesetWriter`:
    //    // It has methods like `writeTilesetJson`, but these are often private or protected,
    //    // used internally when writing a full package.
    //    // The most reliable method is to construct the JSON manually or find the specific export function.

    //    // Let's use `nlohmann::json` as an intermediary, which Cesium Native objects often convert to/from.
    //    // The `CesiumGltf::` types have ` §á§â§à§Ò§Ý§Ö§Þ§à§ÛWriter::write` functions. `Cesium3DTiles::` should too.

    //    // Looking at `Cesium3DTiles/src/TilesetJsonWriter.cpp` (if source is browsed), one would find how it's done.
    //    // Typically, it's `Cesium3DTiles::TilesetJsonWriter().write(tileset, outputStream);`
    //    // We need to include the correct writer header. Let's assume `TilesetJsonWriter.h` exists and is the way.
    //    // #include <Cesium3DTiles/TilesetJsonWriter.h> // This header might not be public or named this way.

    //    // The `Cesium3DTilesWriter` library is for packaging into .3tz or full directory structures.
    //    // The `Cesium3DTiles` library contains the data models and their basic JSON serialization.
    //    // The `CesiumJsonWriter` library provides the low-level JSON writing tools.

    //    // The pattern is usually:
    //    // SomeType someObject;
    //    // CesiumJsonWriter::JsonWriter writer; // Or JsonOutputStream
    //    // NamespaceForSomeType::SomeTypeWriter::write(someObject, writer); // This is what we need for Tileset.
    //    // This seems to be in `Cesium3DTilesContent/src/TilesetWriter.cpp` (for context of use)

    //    // Let's try with nlohmann::json directly, assuming Cesium Native types have `toJsonValue()` or similar.
    //    // This is the most common pattern in Cesium Native for converting its C++ model objects to JSON values.
    //    // (This requires `nlohmann/json.hpp` to be available)
    //    try {
    //        // Cesium Native objects often have a way to convert to nlohmann::json
    //        // For Tileset, this would be a method or a free function.
    //        // If not directly available, manual construction is an option but error-prone.
    //        // Let's construct the JSON using nlohmann::json for clarity and control here,
    //        // mirroring the structure of Cesium3DTiles::Tileset.

    //        nlohmann::json j;
    //        j["asset"]["version"] = tileset.asset.version;
    //        if (!tileset.asset.tilesetVersion.empty()) {
    //            j["asset"]["tilesetVersion"] = tileset.asset.tilesetVersion;
    //        }
    //        // Add other asset properties if set (e.g., gltfUpAxis)

    //        j["geometricError"] = tileset.geometricError;

    //        nlohmann::json rootJson;
    //        if (tileset.root) {
    //            const auto& t = *tileset.root; // Dereference optional
    //            if (!t.boundingVolume.box.empty()) {
    //                rootJson["boundingVolume"]["box"] = t.boundingVolume.box;
    //            }
    //            else if (!t.boundingVolume.region.empty()) {
    //                rootJson["boundingVolume"]["region"] = t.boundingVolume.region;
    //            }
    //            else if (!t.boundingVolume.sphere.empty()) {
    //                rootJson["boundingVolume"]["sphere"] = t.boundingVolume.sphere;
    //            }
    //            // else: boundingVolume is required. Our previous check ensures box is valid.

    //            rootJson["geometricError"] = t.geometricError.value_or(tileset.geometricError); // Use tile's geometricError or inherit

    //            if (t.content) {
    //                rootJson["content"]["uri"] = t.content->uri;
    //                if (t.content->boundingVolume) { // Optional content bounding volume
    //                    if (!t.content->boundingVolume->box.empty()) {
    //                        rootJson["content"]["boundingVolume"]["box"] = t.content->boundingVolume->box;
    //                    } // etc. for region/sphere
    //                }
    //            }
    //            if (t.refine) { // Optional
    //                if (*t.refine == Cesium3DTiles::Tile::Refine::ADD) rootJson["refine"] = "ADD";
    //                if (*t.refine == Cesium3DTiles::Tile::Refine::REPLACE) rootJson["refine"] = "REPLACE";
    //            }
    //            // Children, transform, etc. would go here if we had a more complex hierarchy
    //        }
    //        j["root"] = rootJson;

    //        // Write to file
    //        std::ofstream outFile(tilesetOutputPath);
    //        if (!outFile.is_open()) {
    //            logError("Failed to open tileset output file: " + tilesetOutputPath.string());
    //            return false;
    //        }
    //        outFile << j.dump(2); // .dump(2) for pretty print with 2 spaces indent
    //        outFile.close();

    //        logMessage("Successfully wrote tileset.json to: " + tilesetOutputPath.string());
    //        return true;

    //    }
    //    catch (const std::exception& e) {
    //        logError("Exception while generating or writing tileset.json: " + std::string(e.what()));
    //        return false;
    //    }
    //}

} // namespace GltfInstancing