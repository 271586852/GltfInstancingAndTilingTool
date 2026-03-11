#include "QuadtreePipeline.h"
#include "glb_reader.h"
#include "glb_writer.h"
#include "utilities.h"
#include "tileset_writer.h"
#include "ToolConfiguration.h" 
#include "NonInstancingLOD_manager.h" 
#include "instancing_result.h"
#include "semantic_hausdorff_detector.h"
#include "semantic_parser.h"      

#include <iostream>
#include <fstream>
#include <algorithm>
#include <cstdint> 
#include <cstring> 
#include <iomanip> // For std::setprecision
#include <functional>
#include <glm/gtc/matrix_transform.hpp>
#include <CesiumGltf/ExtensionExtMeshGpuInstancing.h>
#include <CesiumGltfReader/GltfReader.h>
#include <CesiumGltfWriter/GltfWriter.h>

namespace QuadtreePipeline {

    // Helper: Unpack EXT_mesh_gpu_instancing into real nodes
    // This allows subsequent simplification to work on geometry without losing instance transforms
    void unpackInstancing(CesiumGltf::Model& model) {
        std::vector<CesiumGltf::Node> newNodes;
        bool hasChanges = false;

        for (const auto& node : model.nodes) {
            auto it = node.extensions.find("EXT_mesh_gpu_instancing");
            if (it == node.extensions.end()) {
                newNodes.push_back(node);
                continue;
            }

            hasChanges = true;
            const auto& ext = std::any_cast<const CesiumGltf::ExtensionExtMeshGpuInstancing&>(it->second);
            
            int32_t tAccIdx = -1;
            int32_t rAccIdx = -1;
            int32_t sAccIdx = -1;

            if (auto attr = ext.attributes.find("TRANSLATION"); attr != ext.attributes.end()) tAccIdx = attr->second;
            if (auto attr = ext.attributes.find("ROTATION"); attr != ext.attributes.end()) rAccIdx = attr->second;
            if (auto attr = ext.attributes.find("SCALE"); attr != ext.attributes.end()) sAccIdx = attr->second;

            size_t count = 0;
            if (tAccIdx >= 0) count = model.accessors[tAccIdx].count;
            else if (rAccIdx >= 0) count = model.accessors[rAccIdx].count;
            else if (sAccIdx >= 0) count = model.accessors[sAccIdx].count;

            if (count == 0) {
                // Empty instancing, just drop or keep node without extension?
                // Keep node as is but remove extension to avoid issues? 
                // For now, if count is 0, just ignore.
                continue; 
            }

            for (size_t i = 0; i < count; ++i) {
                CesiumGltf::Node instanceNode;
                instanceNode.mesh = node.mesh; // Point to same prototype mesh
                
                // Copy base transform from original node (if any)
                // Note: Instancing spec says T/R/S in extension *replaces* node transform? 
                // Or is applied on top? Spec: "The instanced node's transform properties (matrix, translation, rotation, scale) are ignored."
                // So we don't copy node.matrix/translation/etc.
                
                // Read Translation
                if (tAccIdx >= 0) {
                    const auto& acc = model.accessors[tAccIdx];
                    const auto& bv = model.bufferViews[acc.bufferView];
                    const auto& buf = model.buffers[bv.buffer];
                    size_t stride = (bv.byteStride && *bv.byteStride > 0) ? static_cast<size_t>(*bv.byteStride) : 12; // 3 * float
                    size_t offset = bv.byteOffset + acc.byteOffset + i * stride;
                    const float* ptr = reinterpret_cast<const float*>(buf.cesium.data.data() + offset);
                    instanceNode.translation = { ptr[0], ptr[1], ptr[2] };
                }

                // Read Rotation
                if (rAccIdx >= 0) {
                    const auto& acc = model.accessors[rAccIdx];
                    const auto& bv = model.bufferViews[acc.bufferView];
                    const auto& buf = model.buffers[bv.buffer];
                    size_t stride = (bv.byteStride && *bv.byteStride > 0) ? static_cast<size_t>(*bv.byteStride) : 16; // 4 * float (xyzw)
                    size_t offset = bv.byteOffset + acc.byteOffset + i * stride;
                    // GLTF Rotation is Vec4 (x, y, z, w) - CesiumGltf::Node::rotation is vector<double>
                    const float* ptr = reinterpret_cast<const float*>(buf.cesium.data.data() + offset);
                    instanceNode.rotation = { ptr[0], ptr[1], ptr[2], ptr[3] };
                }

                // Read Scale
                if (sAccIdx >= 0) {
                    const auto& acc = model.accessors[sAccIdx];
                    const auto& bv = model.bufferViews[acc.bufferView];
                    const auto& buf = model.buffers[bv.buffer];
                    size_t stride = (bv.byteStride && *bv.byteStride > 0) ? static_cast<size_t>(*bv.byteStride) : 12; // 3 * float
                    size_t offset = bv.byteOffset + acc.byteOffset + i * stride;
                    const float* ptr = reinterpret_cast<const float*>(buf.cesium.data.data() + offset);
                    instanceNode.scale = { ptr[0], ptr[1], ptr[2] };
                }
                
                newNodes.push_back(std::move(instanceNode));
            }
        }

        if (hasChanges) {
            model.nodes = std::move(newNodes);
            // Since we flattened the nodes list, we need to ensure the scene references them.
            // Our pipeline mostly assumes flat scenes for tiles. 
            // Rebuild scene.nodes
            if (!model.scenes.empty()) {
                model.scenes[0].nodes.clear();
                for (size_t i = 0; i < model.nodes.size(); ++i) {
                    model.scenes[0].nodes.push_back(static_cast<int32_t>(i));
                }
            }
        }
    }

    // Helper: Merge a source Model into a destination Model, updating offsets
    // Returns index offset for Meshes in the destination model
    size_t mergeModel(CesiumGltf::Model& dest, const CesiumGltf::Model& src) {
        // Ensure dest has at least one buffer
        if (dest.buffers.empty()) {
            dest.buffers.resize(1);
        }
        
        CesiumGltf::Buffer& mainBuffer = dest.buffers[0];
        size_t currentBufferEnd = mainBuffer.cesium.data.size();
        
        // Pad buffer to 4-byte alignment
        size_t padding = (4 - (currentBufferEnd % 4)) % 4;
        if (padding > 0) {
            mainBuffer.cesium.data.insert(mainBuffer.cesium.data.end(), padding, std::byte(0));
            currentBufferEnd += padding;
        }

        // We only support merging from buffer[0] of src if available
        // Or we iterate all buffers in src and append them.
        // For GLB, usually there is only one buffer.
        
        // Map from src buffer index to byte offset in dest main buffer
        std::vector<size_t> srcBufferOffsets;
        
        for (const auto& srcBuf : src.buffers) {
            size_t offset = mainBuffer.cesium.data.size();
            // Pad to 4 bytes before appending
            size_t pad = (4 - (offset % 4)) % 4;
            if(pad > 0) {
                mainBuffer.cesium.data.insert(mainBuffer.cesium.data.end(), pad, std::byte(0));
                offset += pad;
            }
            
            srcBufferOffsets.push_back(offset);
            mainBuffer.cesium.data.insert(mainBuffer.cesium.data.end(), srcBuf.cesium.data.begin(), srcBuf.cesium.data.end());
        }
        
        // Update main buffer byteLength
        mainBuffer.byteLength = static_cast<int64_t>(mainBuffer.cesium.data.size());

        size_t bufferViewOffset = dest.bufferViews.size();
        size_t accessorOffset = dest.accessors.size();
        size_t meshOffset = dest.meshes.size();
        size_t materialOffset = dest.materials.size();
        size_t textureOffset = dest.textures.size();
        size_t imageOffset = dest.images.size();
        size_t samplerOffset = dest.samplers.size();

        // 2. BufferViews
        for (auto bv : src.bufferViews) {
            int32_t oldBufIdx = bv.buffer;
            bv.buffer = 0; // Point to main buffer
            if (oldBufIdx >= 0 && oldBufIdx < static_cast<int32_t>(srcBufferOffsets.size())) {
                bv.byteOffset += static_cast<int64_t>(srcBufferOffsets[oldBufIdx]); // Add offset
            }
            dest.bufferViews.push_back(bv);
        }

        // 3. Accessors
        for (auto acc : src.accessors) {
            if (acc.bufferView >= 0) acc.bufferView += static_cast<int32_t>(bufferViewOffset);
            dest.accessors.push_back(acc);
        }

        // 4. Images
        for (auto img : src.images) {
            if (img.bufferView >= 0) img.bufferView += static_cast<int32_t>(bufferViewOffset);
            dest.images.push_back(img);
        }

        // 5. Samplers
        for (const auto& smp : src.samplers) dest.samplers.push_back(smp);

        // 6. Textures
        for (auto tex : src.textures) {
            if (tex.source >= 0) tex.source += static_cast<int32_t>(imageOffset);
            if (tex.sampler >= 0) tex.sampler += static_cast<int32_t>(samplerOffset);
            dest.textures.push_back(tex);
        }

        // 7. Materials
        for (auto mat : src.materials) {
            if (mat.pbrMetallicRoughness) {
                if (mat.pbrMetallicRoughness->baseColorTexture) mat.pbrMetallicRoughness->baseColorTexture->index += static_cast<int32_t>(textureOffset);
                if (mat.pbrMetallicRoughness->metallicRoughnessTexture) mat.pbrMetallicRoughness->metallicRoughnessTexture->index += static_cast<int32_t>(textureOffset);
            }
            if (mat.normalTexture) mat.normalTexture->index += static_cast<int32_t>(textureOffset);
            if (mat.occlusionTexture) mat.occlusionTexture->index += static_cast<int32_t>(textureOffset);
            if (mat.emissiveTexture) mat.emissiveTexture->index += static_cast<int32_t>(textureOffset);
            dest.materials.push_back(mat);
        }

        // 8. Meshes
        for (auto m : src.meshes) {
            for (auto& prim : m.primitives) {
                for (auto& attr : prim.attributes) {
                    attr.second += static_cast<int32_t>(accessorOffset);
                }
                if (prim.indices >= 0) prim.indices += static_cast<int32_t>(accessorOffset);
                if (prim.material >= 0) prim.material += static_cast<int32_t>(materialOffset);
            }
            dest.meshes.push_back(m);
        }

        // 9. Extensions Declarations
        for (const auto& ext : src.extensionsUsed) {
            if (std::find(dest.extensionsUsed.begin(), dest.extensionsUsed.end(), ext) == dest.extensionsUsed.end()) {
                dest.extensionsUsed.push_back(ext);
            }
        }
        for (const auto& ext : src.extensionsRequired) {
            if (std::find(dest.extensionsRequired.begin(), dest.extensionsRequired.end(), ext) == dest.extensionsRequired.end()) {
                dest.extensionsRequired.push_back(ext);
            }
        }

        return meshOffset;
    }

    void computeAABB(const CesiumGltf::Model& model, glm::vec3& minPt, glm::vec3& maxPt) {
        GltfInstancing::BoundingBox overallBox;
        glm::dmat4 identity(1.0);

        auto mergeMeshWithTransform = [&](int32_t meshIndex, const glm::dmat4& worldTransform) {
            if (meshIndex < 0 || static_cast<size_t>(meshIndex) >= model.meshes.size()) return;
            GltfInstancing::BoundingBox meshBox = GltfInstancing::getMeshBoundingBox(model, model.meshes[meshIndex]);
            if (!meshBox.isValid()) return;
            meshBox.transform(worldTransform);
            overallBox.merge(meshBox);
        };

        std::function<void(int32_t, const glm::dmat4&)> visitNode;
        visitNode = [&](int32_t nodeIndex, const glm::dmat4& parentTransform) {
            if (nodeIndex < 0 || static_cast<size_t>(nodeIndex) >= model.nodes.size()) return;
            const CesiumGltf::Node& node = model.nodes[nodeIndex];
            glm::dmat4 localTransform = GltfInstancing::getLocalTransformMatrix(node);
            glm::dmat4 worldTransform = parentTransform * localTransform;

            if (node.mesh >= 0) {
                mergeMeshWithTransform(node.mesh, worldTransform);
            }

            for (int32_t childIndex : node.children) {
                visitNode(childIndex, worldTransform);
            }
        };

        bool traversed = false;
        if (model.scene >= 0 && static_cast<size_t>(model.scene) < model.scenes.size()) {
            const auto& scene = model.scenes[model.scene];
            if (!scene.nodes.empty()) {
                for (int32_t rootIndex : scene.nodes) {
                    visitNode(rootIndex, identity);
                }
                traversed = true;
            }
        }

        if (!traversed && !model.scenes.empty()) {
            for (const auto& scene : model.scenes) {
                for (int32_t rootIndex : scene.nodes) {
                    visitNode(rootIndex, identity);
                    traversed = true;
                }
            }
        }

        if (!traversed && !model.nodes.empty()) {
            std::vector<bool> isChild(model.nodes.size(), false);
            for (const auto& node : model.nodes) {
                for (int32_t childIndex : node.children) {
                    if (childIndex >= 0 && static_cast<size_t>(childIndex) < model.nodes.size()) {
                        isChild[childIndex] = true;
                    }
                }
            }

            bool hasRoot = false;
            for (size_t i = 0; i < model.nodes.size(); ++i) {
                if (!isChild[i]) {
                    visitNode(static_cast<int32_t>(i), identity);
                    hasRoot = true;
                }
            }

            if (!hasRoot) {
                for (size_t i = 0; i < model.nodes.size(); ++i) {
                    visitNode(static_cast<int32_t>(i), identity);
                }
            }
        }

        if (overallBox.isValid()) {
            minPt = glm::vec3(
                static_cast<float>(overallBox.min.x),
                static_cast<float>(overallBox.min.y),
                static_cast<float>(overallBox.min.z));
            maxPt = glm::vec3(
                static_cast<float>(overallBox.max.x),
                static_cast<float>(overallBox.max.y),
                static_cast<float>(overallBox.max.z));
        } else {
            minPt = glm::vec3(0.0f);
            maxPt = glm::vec3(0.0f);
        }
    }

    // --- Pipeline Implementation ---

    Pipeline::Pipeline(const ToolConfiguration& config) : _config(config) {
        initStrategies();
    }

    void Pipeline::initStrategies() {
        // Adjusted factors to trigger refinement earlier (higher factors = larger error = refine sooner)
        _strategies = {
            {0, TileRole::Proxy, 2.0},      // L0: Was 1.0 -> 2.0 (更容易细分到 L1)
            {1, TileRole::Instancing, 0.8}, // L1: Was 0.4 -> 0.8 (更容易细分到 L2)
            {2, TileRole::Instancing, 0.4}, // L2: Was 0.2 -> 0.4
            {3, TileRole::Detail, 0.1},     // L3: Was 0.05 -> 0.1
            {4, TileRole::Detail, 0.0}      // L4: 0.0 (Max detail)
        };
    }

    void Pipeline::run() {
        std::cout << "[QuadtreePipeline] Starting Bottom-Up HLOD Pipeline..." << std::endl;
        _stats.clear(); // Reset stats
        
        scanInputDirectory();
        if (_sceneObjects.empty()) {
            std::cerr << "[QuadtreePipeline] No objects found in input directory." << std::endl;
            return;
        }

        buildQuadtree();
        
        std::filesystem::create_directories(_config.outputDirectory + "/tiles");
        
        generateContentBottomUp();
        generateTilesetJson();
        writeAnalysisReport(); // Generate report
        
        std::cout << "[QuadtreePipeline] Pipeline completed." << std::endl;
    }

    void Pipeline::scanInputDirectory() {
        std::cout << "[QuadtreePipeline] Scanning input: " << _config.inputDirectory << std::endl;
        int idCounter = 0;
        
        CesiumGltfReader::GltfReader reader;
        
        for (const auto& entry : std::filesystem::recursive_directory_iterator(_config.inputDirectory)) {
            if (entry.is_regular_file()) {
                std::string ext = entry.path().extension().string();
                if (ext == ".glb" || ext == ".gltf") {
                    SceneObject obj;
                    obj.id = idCounter++;
                    obj.originalFilePath = entry.path();
                    
                    std::vector<std::byte> data;
                    if (auto bytes = GltfInstancing::readFileBytes(entry.path())) {
                        data.resize(bytes->size());
                        std::memcpy(data.data(), bytes->data(), bytes->size());
                    } else {
                        continue;
                    }
                    
                    CesiumGltfReader::GltfReaderOptions options;
                    auto result = reader.readGltf(gsl::span<const std::byte>(data), options);
                    
                    if (result.model) {
                        CesiumGltf::Model& model = *result.model;
                        computeAABB(model, obj.minBound, obj.maxBound);
                        obj.center = (obj.minBound + obj.maxBound) * 0.5f;
                        obj.dimensions = obj.maxBound - obj.minBound;
                        
                        size_t vCount = 0;
                        for (const auto& mesh : model.meshes) {
                            for (const auto& prim : mesh.primitives) {
                                auto it = prim.attributes.find("POSITION");
                                if (it != prim.attributes.end()) {
                                    int32_t accIdx = it->second;
                                    if (accIdx >= 0 && accIdx < static_cast<int32_t>(model.accessors.size())) {
                                        vCount += model.accessors[accIdx].count;
                                    }
                                }
                            }
                        }
                        obj.vertexCount = vCount;
                        
                        _sceneObjects.push_back(obj);
                    } else {
                        std::cerr << "Failed to read " << entry.path() << std::endl;
                    }
                }
            }
        }
        std::cout << "[QuadtreePipeline] Found " << _sceneObjects.size() << " objects." << std::endl;
    }

    void Pipeline::buildQuadtree() {
        std::cout << "[QuadtreePipeline] Building Quadtree..." << std::endl;
        
        glm::vec3 globalMin(std::numeric_limits<float>::max());
        glm::vec3 globalMax(std::numeric_limits<float>::lowest());
        
        for (const auto& obj : _sceneObjects) {
            globalMin = glm::min(globalMin, obj.minBound);
            globalMax = glm::max(globalMax, obj.maxBound);
        }
        
        std::cout << "[Debug] Global Min: " << globalMin.x << ", " << globalMin.y << ", " << globalMin.z << std::endl;
        std::cout << "[Debug] Global Max: " << globalMax.x << ", " << globalMax.y << ", " << globalMax.z << std::endl;

        // Switch to XZ Plane Quadtree (Assuming Y is Up/Height and is the smallest dimension)
        // Or adaptively choose? For Architecture (Y-Up), XZ is usually the floor plan.
        float width = globalMax.x - globalMin.x;
        float depth = globalMax.z - globalMin.z; // Use Z for depth
        float maxSize = std::max(width, depth);
        
        std::cout << "[Debug] Quadtree Plane: XZ. Size: " << width << " x " << depth << " (Max: " << maxSize << ")" << std::endl;

        // Add a small epsilon to width/height to ensure objects exactly on the max boundary are included
        // because the split logic uses [min, max) range.
        maxSize += 0.01f; 

        _root = std::make_unique<QuadtreeNode>();
        _root->level = 0;
        _root->x = 0;
        _root->y = 0;
        // Quadtree covers X and Z. Y is fully included.
        _root->minBound = globalMin;
        _root->maxBound = glm::vec3(globalMin.x + maxSize, globalMax.y, globalMin.z + maxSize); 
        _root->objects = _sceneObjects; 
        
        recursiveSplit(_root.get());
        
        std::cout << "[QuadtreePipeline] Calculating tight bounds..." << std::endl;
        calculateTightBounds(_root.get());
    }

    void Pipeline::calculateTightBounds(QuadtreeNode* node) {
        // Initialize with inverted infinity
        glm::vec3 minB(std::numeric_limits<float>::max());
        glm::vec3 maxB(std::numeric_limits<float>::lowest());
        bool hasContent = false;

        // 1. Include own objects
        for (const auto& obj : node->objects) {
            minB = glm::min(minB, obj.minBound);
            maxB = glm::max(maxB, obj.maxBound);
            hasContent = true;
        }

        // 2. Include children bounds (Post-order)
        for (const auto& child : node->children) {
            calculateTightBounds(child.get());
            
            // If child has valid tight bounds, merge them
            // Check if child actually has content (min < max)
            if (child->tightMinBound.x <= child->tightMaxBound.x) {
                minB = glm::min(minB, child->tightMinBound);
                maxB = glm::max(maxB, child->tightMaxBound);
                hasContent = true;
            }
        }

        if (hasContent) {
            node->tightMinBound = minB;
            node->tightMaxBound = maxB;
        } else {
            // If empty, just use grid bounds or keep inverted (to indicate empty)
            // For safety in JSON, let's fall back to grid bounds but maybe logged?
            // Actually, empty nodes shouldn't be generated in JSON usually.
            // But let's set to grid bounds to be safe.
            node->tightMinBound = node->minBound;
            node->tightMaxBound = node->maxBound;
        }
    }

    void Pipeline::recursiveSplit(QuadtreeNode* node) {
        const int MAX_OBJECTS = _config.quadtreeMaxObjectsPerTile; 
        const int MAX_DEPTH = _config.quadtreeMaxDepth;    
        
        if (node->objects.size() <= MAX_OBJECTS || node->level >= MAX_DEPTH) {
            return;
        }
        
        // Split on XZ plane
        float midX = (node->minBound.x + node->maxBound.x) * 0.5f;
        float midZ = (node->minBound.z + node->maxBound.z) * 0.5f; // Split Z
        
        for (int i = 0; i < 4; i++) {
            auto child = std::make_unique<QuadtreeNode>();
            child->level = node->level + 1;
            child->x = node->x * 2 + (i % 2); 
            child->y = node->y * 2 + (i / 2); 
            child->minBound.y = node->minBound.y; // Keep Y (Height) full
            child->maxBound.y = node->maxBound.y;
            
            // i=0: BL (minX, minZ)
            // i=1: BR (midX, minZ)
            // i=2: TL (minX, midZ)
            // i=3: TR (midX, midZ)
            
            if (i == 0) { // Bottom-Left
                child->minBound.x = node->minBound.x; child->maxBound.x = midX;
                child->minBound.z = node->minBound.z; child->maxBound.z = midZ;
            } else if (i == 1) { // Bottom-Right
                child->minBound.x = midX;             child->maxBound.x = node->maxBound.x;
                child->minBound.z = node->minBound.z; child->maxBound.z = midZ;
            } else if (i == 2) { // Top-Left
                child->minBound.x = node->minBound.x; child->maxBound.x = midX;
                child->minBound.z = midZ;             child->maxBound.z = node->maxBound.z;
            } else if (i == 3) { // Top-Right
                child->minBound.x = midX;             child->maxBound.x = node->maxBound.x;
                child->minBound.z = midZ;             child->maxBound.z = node->maxBound.z;
            }
            
            for (const auto& obj : node->objects) {
                // Check X and Z center
                if (obj.center.x >= child->minBound.x && obj.center.x < child->maxBound.x &&
                    obj.center.z >= child->minBound.z && obj.center.z < child->maxBound.z) {
                    child->objects.push_back(obj);
                }
            }
            
            if (!child->objects.empty()) {
                recursiveSplit(child.get());
                node->children.push_back(std::move(child));
            }
        }
    }

    size_t Pipeline::countTriangles(const CesiumGltf::Model& model) {
        size_t count = 0;
        for (const auto& mesh : model.meshes) {
            for (const auto& prim : mesh.primitives) {
                if (prim.indices >= 0) {
                    const auto& acc = model.accessors[prim.indices];
                    count += acc.count / 3;
                } else {
                    auto it = prim.attributes.find("POSITION");
                    if (it != prim.attributes.end()) {
                        const auto& acc = model.accessors[it->second];
                        count += acc.count / 3;
                    }
                }
            }
        }
        // If instancing, multiply?
        // Current logic stores instanced nodes with extension.
        // The mesh count above is for prototypes.
        // Total visualized triangles = Sum(PrototypeTriangles * InstanceCount)
        // Let's count stored triangles (file size proxy) vs visual triangles.
        // Let's just count unique mesh triangles for now.
        return count;
    }

    // --- Bottom-Up Generation ---

    void Pipeline::generateContentBottomUp() {
        std::cout << "[QuadtreePipeline] Generating content Bottom-Up..." << std::endl;
        
        std::vector<QuadtreeNode*> leaves;
        std::vector<QuadtreeNode*> allNodes;
        
        std::vector<QuadtreeNode*> stack = {_root.get()};
        while(!stack.empty()) {
            QuadtreeNode* n = stack.back();
            stack.pop_back();
            allNodes.push_back(n);
            
            if (n->isLeaf()) {
                leaves.push_back(n);
            } else {
                for(auto& child : n->children) stack.push_back(child.get());
            }
        }
        
        std::sort(allNodes.begin(), allNodes.end(), [](const QuadtreeNode* a, const QuadtreeNode* b){
            return a->level > b->level;
        });
        
        int currentLevel = -1;
        for (auto* node : allNodes) {
            if (node->level != currentLevel) {
                currentLevel = node->level;
                std::cout << "Processing Level " << currentLevel << "..." << std::endl;
            }
            
            bool generated = false;
            if (node->isLeaf()) {
                generated = generateLeafTile(node);
            } else {
                generated = processParentTile(node);
            }
        }
    }

    bool Pipeline::generateLeafTile(QuadtreeNode* node) {
        if (node->objects.empty()) return false;
        
        std::string filename = "T" + std::to_string(node->level) + "_" + 
                               std::to_string(node->x) + "_" + 
                               std::to_string(node->y) + ".glb";
        
        std::cout << "[Debug] Generating Leaf " << filename << " with " << node->objects.size() << " objects." << std::endl;

        std::filesystem::path outputPath = std::filesystem::path(_config.outputDirectory) / "tiles" / filename;
        node->tileFilename = "tiles/" + filename;
        
        CesiumGltf::Model outModel;
        outModel.asset.version = "2.0";
        CesiumGltf::Scene& scene = outModel.scenes.emplace_back();
        outModel.scene = 0;

        CesiumGltfReader::GltfReader reader;
        
        for (const auto& obj : node->objects) {
            std::vector<std::byte> data;
            if (auto bytes = GltfInstancing::readFileBytes(obj.originalFilePath)) {
                 data.resize(bytes->size());
                 std::memcpy(data.data(), bytes->data(), bytes->size());
            } else continue;
            
            CesiumGltfReader::GltfReaderOptions options;
            auto result = reader.readGltf(gsl::span<const std::byte>(data), options);
            if (result.model) {
                 size_t meshOffset = mergeModel(outModel, *result.model);
                 
                 for (auto n : result.model->nodes) {
                     if (n.mesh >= 0) n.mesh += static_cast<int32_t>(meshOffset);
                     size_t newNodeIdx = outModel.nodes.size();
                     outModel.nodes.push_back(n);
                     scene.nodes.push_back(static_cast<int32_t>(newNodeIdx));
                 }
            }
        }
        
        if (outModel.nodes.empty()) return false;

        // Extract the merged buffer data to pass explicitly to writer
        std::vector<std::byte> binData = std::move(outModel.buffers[0].cesium.data);
        
        // We must ensure the buffer definition in the model matches what the writer expects.
        // The writer expects buffers[0] to correspond to the BIN chunk.
        // However, if we pass buffer data externally, the writer might reconstruct the buffer definition.
        // Let's pass it as the content for buffer 0.
        
        gsl::span<const std::byte> binSpan(binData);
        
        CesiumGltfWriter::GltfWriter writer;
        CesiumGltfWriter::GltfWriterOptions options;
        auto result = writer.writeGlb(outModel, { binSpan }, options);
        
        if (!result.gltfBytes.empty()) {
            std::ofstream f(outputPath, std::ios::binary);
            f.write(reinterpret_cast<const char*>(result.gltfBytes.data()), result.gltfBytes.size());
            
            // Record Stats
            TileStats stats;
            stats.level = node->level;
            stats.tileName = filename;
            stats.fileSizeKB = result.gltfBytes.size() / 1024.0;
            stats.triangleCount = countTriangles(outModel);
            stats.renderedTriangleCount = stats.triangleCount; // Leaf tiles have no instancing, so 1:1
            stats.instanceCount = 0; 
            stats.uniqueMeshCount = outModel.meshes.size();
            _stats.push_back(stats);
            
            return true;
        }
        return false;
    }

    bool Pipeline::processParentTile(QuadtreeNode* node) {
        std::vector<std::filesystem::path> childPaths;
        for (const auto& child : node->children) {
            if (!child->tileFilename.empty()) {
                childPaths.push_back(std::filesystem::path(_config.outputDirectory) / child->tileFilename);
            }
        }
        
        if (childPaths.empty()) return false;
        
        std::string filename = "T" + std::to_string(node->level) + "_" + 
                               std::to_string(node->x) + "_" + 
                               std::to_string(node->y) + ".glb";
        std::filesystem::path outputPath = std::filesystem::path(_config.outputDirectory) / "tiles" / filename;
        node->tileFilename = "tiles/" + filename;
        
        createParentTileContent(childPaths, outputPath, node->level);
        return true;
    }

    size_t getMeshTriangleCount(const CesiumGltf::Model& model, int32_t meshIndex) {
        if (meshIndex < 0 || meshIndex >= static_cast<int32_t>(model.meshes.size())) return 0;
        const auto& mesh = model.meshes[meshIndex];
        size_t count = 0;
        for (const auto& prim : mesh.primitives) {
            if (prim.indices >= 0) {
                if (prim.indices < static_cast<int32_t>(model.accessors.size())) {
                    count += model.accessors[prim.indices].count / 3;
                }
            } else {
                auto it = prim.attributes.find("POSITION");
                if (it != prim.attributes.end() && it->second < static_cast<int32_t>(model.accessors.size())) {
                    count += model.accessors[it->second].count / 3;
                }
            }
        }
        return count;
    }

    void Pipeline::createParentTileContent(
        const std::vector<std::filesystem::path>& childGlbPaths,
        const std::filesystem::path& outputGlbPath,
        int level
    ) {
        CesiumGltf::Model mergedModel;
        mergedModel.asset.version = "2.0";
        CesiumGltf::Scene& scene = mergedModel.scenes.emplace_back();
        mergedModel.scene = 0;
        
        CesiumGltfReader::GltfReader reader;
        
        for (const auto& path : childGlbPaths) {
            std::vector<std::byte> data;
            if (auto bytes = GltfInstancing::readFileBytes(path)) {
                 data.resize(bytes->size());
                 std::memcpy(data.data(), bytes->data(), bytes->size());
            } else continue;
            
            auto res = reader.readGltf(gsl::span<const std::byte>(data), {});
            if (res.model) {
                // Unpack instancing BEFORE merging
                // This converts EXT_mesh_gpu_instancing into explicit Nodes
                // ensuring simplifyModel can handle them correctly later.
                unpackInstancing(*res.model);

                size_t meshOffset = mergeModel(mergedModel, *res.model);
                
                for(auto n : res.model->nodes) {
                    if(n.mesh >= 0) n.mesh += static_cast<int32_t>(meshOffset);
                    size_t newIdx = mergedModel.nodes.size();
                    mergedModel.nodes.push_back(n);
                    scene.nodes.push_back(static_cast<int32_t>(newIdx));
                }
            }
        }
        
        float simplifyRatio = 0.5f; 
        
        CesiumGltf::Model simplifiedModel;
        
        if (mergedModel.meshes.empty()) {
            simplifiedModel = std::move(mergedModel);
        } else {
            try {
                simplifiedModel = NonInstancingLOD::NonInstancingLODManager::simplifyModel(
                    mergedModel, 
                    simplifyRatio, 
                    300 
                );
            } catch (...) {
                std::cerr << "[Error] Simplification failed for parent tile " << outputGlbPath << ". Using unsimplified model." << std::endl;
                simplifiedModel = std::move(mergedModel);
            }
        }
        
        GltfInstancing::LoadedGltfModel loadedForDet;
        loadedForDet.model = std::move(simplifiedModel);
        loadedForDet.uniqueId = 0;
        loadedForDet.originalPath = outputGlbPath; 
        
        std::vector<GltfInstancing::LoadedGltfModel> models = { std::move(loadedForDet) };
        
        int hlodInstanceLimit = _config.hlodInstanceLimitSet ?
            _config.hlodInstanceLimit : _config.instanceLimit;

        // Detect instancing (legacy bbox logic removed; semantic_hausdorff only)
        GltfInstancing::InstancingDetectionResult result;
        {
            GltfInstancing::SemanticParser semanticParser;
            if (!_config.semanticDataPath.empty() && std::filesystem::exists(_config.semanticDataPath)) {
                if (std::filesystem::is_directory(_config.semanticDataPath)) {
                    std::string discoverDir = _config.semanticInputDirectory.empty() ? _config.inputDirectory : _config.semanticInputDirectory;
                    auto discovered = GltfInstancing::GlbReader().discoverGlbFiles(discoverDir, true);
                    semanticParser.parseFromFolder(_config.semanticDataPath, discovered);
                } else {
                    semanticParser.parse(_config.semanticDataPath);
                }
            }
            GltfInstancing::SemanticHausdorffInstancingDetector detector(
                &semanticParser,
                _config.semanticHashFields,
                _config.hlodSimilarityThreshold,
                hlodInstanceLimit,
                _config.hausdorffMaxSamplePoints);
            result = detector.detect(models);
        }
        
        GltfInstancing::GlbWriter writer;
        auto writeRes = writer.writeInstancedGlb(models, result, outputGlbPath);
        
        // Record Stats
        TileStats stats;
        stats.level = level;
        stats.tileName = outputGlbPath.filename().string();
        if (std::filesystem::exists(outputGlbPath))
            stats.fileSizeKB = std::filesystem::file_size(outputGlbPath) / 1024.0;
        else stats.fileSizeKB = 0;
        
        // Count from detection result
        stats.uniqueMeshCount = result.instancedGroups.size() + result.nonInstancedMeshes.size();
        
        size_t totalInst = 0;
        size_t storedTris = 0;
        size_t renderedTris = 0;

        // 1. Instanced Groups
        for(const auto& g : result.instancedGroups) {
            totalInst += g.instances.size();
            // Use local helper to count triangles from the model used for detection
            size_t tris = getMeshTriangleCount(simplifiedModel, g.representativeMeshIndexInModel);
            storedTris += tris;
            renderedTris += tris * g.instances.size();
        }

        // 2. Non-Instanced Meshes
        for(const auto& m : result.nonInstancedMeshes) {
            size_t tris = getMeshTriangleCount(simplifiedModel, m.originalMeshIndexInModel);
            storedTris += tris;
            renderedTris += tris;
        }

        stats.instanceCount = totalInst;
        stats.triangleCount = storedTris; 
        stats.renderedTriangleCount = renderedTris;
        
        _stats.push_back(stats);
    }

    void Pipeline::writeAnalysisReport() {
        std::filesystem::path reportPath = std::filesystem::path(_config.outputDirectory) / "hlod_analysis.csv";
        std::ofstream csv(reportPath);
        if (!csv.is_open()) return;
        
        csv << "Level,Tile,FileSize(KB),UniqueMeshes,Instances,StoredTriangles,RenderedTriangles,ReductionRatio\n";
        
        // Sort by level then name
        std::sort(_stats.begin(), _stats.end(), [](const TileStats& a, const TileStats& b){
            if (a.level != b.level) return a.level > b.level;
            return a.tileName < b.tileName;
        });
        
        for (const auto& s : _stats) {
            double ratio = (s.renderedTriangleCount > 0) ? (double)s.triangleCount / s.renderedTriangleCount : 1.0;
            
            csv << s.level << ","
                << s.tileName << ","
                << std::fixed << std::setprecision(2) << s.fileSizeKB << ","
                << s.uniqueMeshCount << ","
                << s.instanceCount << ","
                << s.triangleCount << ","
                << s.renderedTriangleCount << ","
                << std::setprecision(4) << ratio << "\n";
        }
        
        csv.close();
        std::cout << "[QuadtreePipeline] Analysis report written to: " << reportPath << std::endl;
    }

    TileRole Pipeline::getRoleForLevel(int level) const {
        if (level >= _strategies.size()) return _strategies.back().role;
        return _strategies[level].role;
    }

    double Pipeline::getGeometricErrorFactor(int level) const {
        if (level >= _strategies.size()) return _strategies.back().geometricErrorFactor;
        return _strategies[level].geometricErrorFactor;
    }

    void Pipeline::generateTilesetJson() {
        std::ofstream jsonFile(_config.outputDirectory + "/tileset.json");
        jsonFile << "{" << std::endl;
        jsonFile << "  \"asset\": { \"version\": \"1.1\" }," << std::endl;
        jsonFile << "  \"geometricError\": 10000.0," << std::endl;
        
        jsonFile << "  \"root\": {" << std::endl; // Start root object

        // Add User Provided Transform
        if (_config.rootTransform.size() == 16) {
            jsonFile << "    \"transform\": [" << std::endl;
            for(int i=0; i<16; i+=4) {
                jsonFile << "      " << _config.rootTransform[i] << ", " << _config.rootTransform[i+1] << ", " 
                         << _config.rootTransform[i+2] << ", " << _config.rootTransform[i+3];
                if (i < 12) jsonFile << "," << std::endl;
                else jsonFile << std::endl;
            }
            jsonFile << "    ]," << std::endl;
        }

        // Use indent=4 because we are already inside "root": { ... }
        writeTilesetJsonRecursive(jsonFile, _root.get(), 4, false); // false = don't wrap in braces, write content of root
        
        jsonFile << "  }" << std::endl; // End root object
        jsonFile << "}" << std::endl;
        jsonFile.close();
    }

    // Adjusted to optionally write the surrounding braces
    void Pipeline::writeTilesetJsonRecursive(std::ofstream& json, const QuadtreeNode* node, int indent, bool writeBraces) {
        std::string sp(indent, ' ');
        if (writeBraces) json << sp << "{" << std::endl;
        
        std::string innerSp = writeBraces ? std::string(indent + 2, ' ') : sp;
        
        // Use Tight Bounds for display
        glm::vec3 min = node->tightMinBound;
        glm::vec3 max = node->tightMaxBound;
        glm::vec3 center = (min + max) * 0.5f;
        glm::vec3 scale = (max - min) * 0.5f;
        
        std::vector<double> boundingBox = {
            center.x, center.y, center.z,
            scale.x, 0.0, 0.0,
            0.0, scale.y, 0.0,
            0.0, 0.0, scale.z
        };
        // 将包围盒从 glTF 的 Y-up 转为 Cesium 的 Z-up
        GltfInstancing::changeGLBToCesiumAxis(boundingBox);

        json << innerSp << "\"boundingVolume\": {" << std::endl;
        json << innerSp << "  \"box\": [" 
             << boundingBox[0] << ", " << boundingBox[1] << ", " << boundingBox[2] << ", "
             << boundingBox[3] << ", " << boundingBox[4] << ", " << boundingBox[5] << ", "
             << boundingBox[6] << ", " << boundingBox[7] << ", " << boundingBox[8] << ", "
             << boundingBox[9] << ", " << boundingBox[10] << ", " << boundingBox[11] << "]" << std::endl;
        json << innerSp << "}," << std::endl;
        
        double diagonal = glm::length(max - min);
        double error = diagonal * getGeometricErrorFactor(node->level);
        
        // Fix: If a node has children (not a leaf), its Geometric Error MUST NOT be 0.
        // Otherwise, Cesium will think this tile is perfect and never refine to its children.
        // If strategy gave us 0.0 (e.g. because we exceeded strategy depth), enforce a minimum error.
        if (!node->children.empty() && error < 0.001) {
            error = std::max(diagonal * 0.05, 0.1); // Fallback: 5% of diagonal or at least 0.1
        }

        json << innerSp << "\"geometricError\": " << error << "," << std::endl;
        
        json << innerSp << "\"refine\": \"REPLACE\"," << std::endl;
        
        json << innerSp << "\"content\": { \"uri\": \"" << node->tileFilename << "\" }";
        
        if (!node->children.empty()) {
            json << "," << std::endl;
            json << innerSp << "\"children\": [" << std::endl;
            for (size_t i = 0; i < node->children.size(); ++i) {
                // Children always write braces
                writeTilesetJsonRecursive(json, node->children[i].get(), indent + (writeBraces ? 4 : 2), true);
                if (i < node->children.size() - 1) json << ",";
                json << std::endl;
            }
            json << innerSp << "]" << std::endl;
        } else {
            json << std::endl;
        }
        
        if (writeBraces) json << sp << "}";
    }

}
