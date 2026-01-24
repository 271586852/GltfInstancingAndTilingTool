#include "mesh_simplifier.h"
#include "meshoptimizer.h"
#include "utilities.h"
#include "tileset_writer.h"

#include <CesiumGltfReader/GltfReader.h>
#include <CesiumGltfWriter/GltfWriter.h>
#include <CesiumGltf/AccessorView.h>
#include <iostream>
#include <fstream>
#include <glm/vec3.hpp>
#include <cfloat>

namespace NonInstancingLOD {

    struct LodStats {
        int level;
        std::string filename;
        size_t triangleCount;
        size_t originalTriangleCount;
        double fileSizeMB;
    };

    void MeshSimplifier::generateNonInstancingLodChain(
        const std::filesystem::path& inputPath,
        const std::filesystem::path& outputDir,
        int levels,
        float ratio,
        size_t minSimplifyIndexCount,
        const std::string& tilesetName
    ) {
        if (!std::filesystem::exists(inputPath)) {
        GltfInstancing::logError("MeshSimplifier: Input file does not exist: " + inputPath.string());
            return;
        }

        std::filesystem::create_directories(outputDir);

        std::vector<LodStats> allStats;

        // Load original model
        std::vector<std::byte> data;
        std::ifstream file(inputPath, std::ios::binary | std::ios::ate);
        if (!file) {
            GltfInstancing::logError("MeshSimplifier: Failed to open file: " + inputPath.string());
            return;
        }
        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);
        data.resize(size);
        if (!file.read(reinterpret_cast<char*>(data.data()), size)) {
             GltfInstancing::logError("MeshSimplifier: Failed to read file: " + inputPath.string());
             return;
        }
        
        CesiumGltfReader::GltfReader reader;
        auto modelResult = reader.readGltf(gsl::span<const std::byte>(data));
        if (!modelResult.model) {
            GltfInstancing::logError("MeshSimplifier: Failed to parse GLB: " + inputPath.string());
            return;
        }

        CesiumGltf::Model currentModel = std::move(*modelResult.model);
        
        // Prepare list of LOD filenames and errors for tileset
        std::vector<std::string> lodFilenames;
        // LOD0 is the original
        std::string lod0Name = "non_instanced_LOD0.glb";
        std::filesystem::path lod0Path = outputDir / lod0Name;
        
        // Copy original to LOD0
        try {
            std::filesystem::copy_file(inputPath, lod0Path, std::filesystem::copy_options::overwrite_existing);
            lodFilenames.push_back(lod0Name);

            // Calculate LOD0 stats
            LodStats lod0Stats;
            lod0Stats.level = 0;
            lod0Stats.filename = lod0Name;
            try {
                lod0Stats.fileSizeMB = (double)std::filesystem::file_size(lod0Path) / (1024.0 * 1024.0);
            } catch (...) { lod0Stats.fileSizeMB = 0.0; }
            
            lod0Stats.triangleCount = 0;
            for (const auto& mesh : currentModel.meshes) {
                for (const auto& prim : mesh.primitives) {
                    if (prim.indices >= 0) {
                        const auto& acc = currentModel.accessors[prim.indices];
                        lod0Stats.triangleCount += acc.count / 3;
                    }
                }
            }
            lod0Stats.originalTriangleCount = lod0Stats.triangleCount;
            allStats.push_back(lod0Stats);

        } catch (const std::exception& e) {
            GltfInstancing::logError("MeshSimplifier: Failed to copy LOD0: " + std::string(e.what()));
            return;
        }

        float currentRatio = ratio;
        
        for (int i = 1; i <= levels; ++i) {
            GltfInstancing::logInfo("Generating Non-Instanced LOD " + std::to_string(i) + " (Target Ratio: " + std::to_string(currentRatio) + ")");
            
            CesiumGltf::Model simplified = simplifyModel(currentModel, currentRatio, minSimplifyIndexCount);
            
            std::string lodName = "non_instanced_LOD" + std::to_string(i) + ".glb";
            std::filesystem::path lodPath = outputDir / lodName;

            CesiumGltfWriter::GltfWriter writer;
            if (!simplified.buffers.empty()) {
                simplified.buffers[0].byteLength = static_cast<int64_t>(simplified.buffers[0].cesium.data.size());
            }

            CesiumGltfWriter::GltfWriterOptions writerOptions;
            gsl::span<const std::byte> bufferSpan;
            if (!simplified.buffers.empty()) {
                bufferSpan = gsl::span<const std::byte>(simplified.buffers[0].cesium.data);
            }

            CesiumGltfWriter::GltfWriterResult writerResult = writer.writeGlb(simplified, bufferSpan, writerOptions);
            if (writerResult.gltfBytes.empty()) {
                GltfInstancing::logError("MeshSimplifier: Failed to write GLB: " + lodPath.string());
                return;
            }

            std::ofstream outFile(lodPath, std::ios::binary);
            if (!outFile) {
                GltfInstancing::logError("MeshSimplifier: Failed to open output file: " + lodPath.string());
                return;
            }
            outFile.write(reinterpret_cast<const char*>(writerResult.gltfBytes.data()), writerResult.gltfBytes.size());
            outFile.close();
            
            lodFilenames.push_back(lodName);

            // Calculate LOD stats
            LodStats stats;
            stats.level = i;
            stats.filename = lodName;
            try {
                stats.fileSizeMB = (double)std::filesystem::file_size(lodPath) / (1024.0 * 1024.0);
            } catch (...) { stats.fileSizeMB = 0.0; }
            
            stats.triangleCount = 0;
            for (const auto& mesh : simplified.meshes) {
                for (const auto& prim : mesh.primitives) {
                    if (prim.indices >= 0) {
                        const auto& acc = simplified.accessors[prim.indices];
                        stats.triangleCount += acc.count / 3;
                    }
                }
            }
            stats.originalTriangleCount = allStats[0].triangleCount;
            allStats.push_back(stats);
            
            currentRatio *= ratio; 
        }

        // Write CSV Report
        std::filesystem::path reportPath = outputDir / "non_instanced_lod_report.csv";
        std::ofstream reportFile(reportPath);
        if (reportFile.is_open()) {
            reportFile << "Level,Filename,File Size (MB),Triangle Count,Original Triangles,Reduction Ratio (Triangles),Reduction Ratio (File Size)\n";
            for (const auto& s : allStats) {
                double triRatio = (s.originalTriangleCount > 0) ? (1.0 - (double)s.triangleCount / s.originalTriangleCount) * 100.0 : 0.0;
                double sizeRatio = (allStats[0].fileSizeMB > 0) ? (1.0 - s.fileSizeMB / allStats[0].fileSizeMB) * 100.0 : 0.0;
                
                reportFile << "LOD" << s.level << ","
                           << s.filename << ","
                           << std::fixed << std::setprecision(2) << s.fileSizeMB << ","
                           << s.triangleCount << ","
                           << s.originalTriangleCount << ","
                           << triRatio << "%,"
                           << sizeRatio << "%\n";
            }
            reportFile.close();
            GltfInstancing::logInfo("Non-Instanced LOD report written to: " + reportPath.string());
        } else {
             GltfInstancing::logError("Failed to write non-instanced LOD report to: " + reportPath.string());
        }

        // Generate Tileset
        if (lodFilenames.empty()) return;

        GltfInstancing::TilesetNode rootNode;
        GltfInstancing::TilesetNode* current = &rootNode;
        
        for (int i = lodFilenames.size() - 1; i >= 0; --i) {
            GltfInstancing::TilesetNode node;
            node.contentUri = lodFilenames[i];
            
            double error = (i == 0) ? 0.0 : 16.0 * std::pow(2.0, i - 1); 
            if (i == lodFilenames.size() - 1) error = 1000.0;

            node.geometricError = error;
            node.boundingVolume = { glm::dvec3(-10000), glm::dvec3(10000) }; 
            
            if (i == lodFilenames.size() - 1) {
                rootNode = node;
                current = &rootNode;
            } else {
                current->children.push_back(node);
                current = &current->children.back();
            }
        }

        std::filesystem::path tilesetPath = outputDir / tilesetName;
        GltfInstancing::TilesetWriter tilesetWriter;
        
        tilesetWriter.writeHierarchicalTileset(rootNode, tilesetPath);
        GltfInstancing::logInfo("Non-Instanced LOD chain generated at: " + outputDir.string());
    }

    CesiumGltf::Model MeshSimplifier::simplifyModel(
        const CesiumGltf::Model& source,
        float targetRatio,
        size_t minSimplifyIndexCount
    ) {
        CesiumGltf::Model result = source;
        
        // Output buffers: single buffer for GLB
        result.buffers.clear();
        result.buffers.resize(1); // Single buffer
        result.bufferViews.clear();
        result.accessors.clear();

        std::vector<CesiumGltf::Mesh> newMeshes;
        newMeshes.reserve(result.meshes.size());
        std::vector<int32_t> meshRemap(source.meshes.size(), -1);

        for (size_t meshIndex = 0; meshIndex < source.meshes.size(); ++meshIndex) {
            const auto& mesh = source.meshes[meshIndex];
            CesiumGltf::Mesh newMesh = mesh;
            newMesh.primitives.clear();

            for (const auto& primitive : mesh.primitives) {
                // Skip if missing position
                auto posIt = primitive.attributes.find("POSITION");
                if (posIt == primitive.attributes.end()) continue;
                const bool hasNormal = primitive.attributes.count("NORMAL") > 0;
                const int32_t oldNormalAccessor = hasNormal ? primitive.attributes.at("NORMAL") : -1;

                // --- 1. Gather Data ---
                const CesiumGltf::Accessor& posAcc = source.accessors[posIt->second];
                const CesiumGltf::BufferView& posView = source.bufferViews[posAcc.bufferView];
                const CesiumGltf::Buffer& posBuf = source.buffers[posView.buffer];
                const std::byte* posDataPtr = posBuf.cesium.data.data() + posView.byteOffset + posAcc.byteOffset;
                size_t vertexCount = posAcc.count;
                // Assume vec3 float

                std::vector<uint32_t> indices;
                if (primitive.indices >= 0) {
                    const CesiumGltf::Accessor& idxAcc = source.accessors[primitive.indices];
                    const CesiumGltf::BufferView& idxView = source.bufferViews[idxAcc.bufferView];
                    const CesiumGltf::Buffer& idxBuf = source.buffers[idxView.buffer];
                    const std::byte* idxData = idxBuf.cesium.data.data() + idxView.byteOffset + idxAcc.byteOffset;

                    indices.resize(idxAcc.count);
                    if (idxAcc.componentType == CesiumGltf::Accessor::ComponentType::UNSIGNED_INT) {
                        const uint32_t* p = reinterpret_cast<const uint32_t*>(idxData);
                        std::copy(p, p + idxAcc.count, indices.begin());
                    } else if (idxAcc.componentType == CesiumGltf::Accessor::ComponentType::UNSIGNED_SHORT) {
                        const uint16_t* p = reinterpret_cast<const uint16_t*>(idxData);
                        std::copy(p, p + idxAcc.count, indices.begin());
                    } else if (idxAcc.componentType == CesiumGltf::Accessor::ComponentType::UNSIGNED_BYTE) {
                        const uint8_t* p = reinterpret_cast<const uint8_t*>(idxData);
                        std::copy(p, p + idxAcc.count, indices.begin());
                    }
                } else {
                    // Generate sequential indices if the primitive is unindexed
                    indices.resize(vertexCount);
                    for (size_t k = 0; k < vertexCount; ++k) {
                        indices[k] = static_cast<uint32_t>(k);
                    }
                }
                
                // --- 2. Welding (Key Step) ---
                // meshopt_generateVertexRemap requires raw vertex stream.
                // We'll use only Position for welding (simplest for LOD).
                std::vector<uint32_t> remap(indices.size()); // remap table: index -> unique_vertex_index
                
                // Note: meshopt expects a contiguous vertex buffer. glTF might have strides.
                std::vector<glm::vec3> positions(vertexCount);
                size_t posStride = static_cast<size_t>(posView.byteStride.value_or(12));
                if (posStride != 12) {
                    for(size_t k=0; k<vertexCount; ++k) {
                        const float* p = reinterpret_cast<const float*>(posDataPtr + k * posStride);
                        positions[k] = glm::vec3(p[0], p[1], p[2]);
                    }
                } else {
                    std::memcpy(positions.data(), posDataPtr, vertexCount * 12);
                }

                size_t uniqueVertexCount = meshopt_generateVertexRemap(
                    remap.data(),
                    indices.data(),
                    indices.size(),
                    positions.data(),
                    vertexCount,
                    sizeof(glm::vec3)
                );

                // --- 3. Re-index and Simplify ---
                std::vector<uint32_t> weldedIndices(indices.size());
                meshopt_remapIndexBuffer(weldedIndices.data(), indices.data(), indices.size(), remap.data());

                std::vector<glm::vec3> weldedPositions(uniqueVertexCount);
                meshopt_remapVertexBuffer(weldedPositions.data(), positions.data(), vertexCount, sizeof(glm::vec3), remap.data());

                // Now simplify the WELDED mesh
                const size_t originalIndexCount = weldedIndices.size();
                const size_t minIndexCount = 3;
                size_t targetIndexCount = static_cast<size_t>(originalIndexCount * targetRatio);
                float targetError = 1.0f; // 1 meter error allowed

                std::vector<uint32_t> simplifiedIndices;
                size_t simplifiedIndexCount = 0;

                if (originalIndexCount < minSimplifyIndexCount) {
                    simplifiedIndices = weldedIndices;
                    simplifiedIndexCount = originalIndexCount;
                } else {
                    if (targetIndexCount > originalIndexCount) targetIndexCount = originalIndexCount;
                    if (targetIndexCount < minIndexCount) targetIndexCount = minIndexCount;
                    targetIndexCount -= (targetIndexCount % 3);
                    if (targetIndexCount < minIndexCount) targetIndexCount = minIndexCount;

                    simplifiedIndices.resize(originalIndexCount);
                    simplifiedIndexCount = meshopt_simplify(
                        simplifiedIndices.data(),
                        weldedIndices.data(),
                        originalIndexCount,
                        reinterpret_cast<const float*>(weldedPositions.data()),
                        uniqueVertexCount,
                        sizeof(glm::vec3),
                        targetIndexCount,
                        targetError,
                        0,
                        nullptr
                    );

                    if (simplifiedIndexCount < minIndexCount) {
                        simplifiedIndices = weldedIndices;
                        simplifiedIndexCount = originalIndexCount;
                    } else {
                        simplifiedIndices.resize(simplifiedIndexCount);
                    }
                }

                GltfInstancing::logInfo("Simplified primitive: " + std::to_string(indices.size()/3) + " -> " + std::to_string(simplifiedIndexCount/3) + " tris");

                // Skip invalid/empty primitives
                if (simplifiedIndexCount < 3 || uniqueVertexCount == 0) {
                    continue;
                }

                // --- 4. Write New Buffers ---
                // We need to support ALL attributes, not just Position.
                // For simplicity in this LOD tool, we will only preserve POSITION, NORMAL, TEXCOORD_0 if they exist.
                // And we have to remap them using the same 'remap' table, but we effectively have to use the welded versions.
                // However, simplification produces indices into the WELDED vertex buffer.
                // So we must output the WELDED vertex buffer (subset of it? No, simplify reuses vertices).
                
                // Build new primitive
                CesiumGltf::MeshPrimitive newPrim = primitive;
                newPrim.attributes.clear();

                // Write Indices
                int32_t newIdxViewIdx = static_cast<int32_t>(result.bufferViews.size());
                size_t idxBytes = simplifiedIndices.size() * 4;
                // Align
                while(result.buffers[0].cesium.data.size() % 4 != 0) result.buffers[0].cesium.data.push_back(std::byte(0));
                size_t idxOffset = result.buffers[0].cesium.data.size();
                result.buffers[0].cesium.data.resize(idxOffset + idxBytes);
                std::memcpy(result.buffers[0].cesium.data.data() + idxOffset, simplifiedIndices.data(), idxBytes);
                
                CesiumGltf::BufferView bvIdx;
                bvIdx.buffer = 0;
                bvIdx.byteLength = idxBytes;
                bvIdx.byteOffset = idxOffset;
                bvIdx.target = CesiumGltf::BufferView::Target::ELEMENT_ARRAY_BUFFER;
                result.bufferViews.push_back(bvIdx);

                CesiumGltf::Accessor accIdx;
                accIdx.bufferView = newIdxViewIdx;
                accIdx.componentType = CesiumGltf::Accessor::ComponentType::UNSIGNED_INT;
                accIdx.count = simplifiedIndices.size();
                accIdx.type = CesiumGltf::Accessor::Type::SCALAR;
                int32_t newIdxAccIdx = static_cast<int32_t>(result.accessors.size());
                result.accessors.push_back(accIdx);
                newPrim.indices = newIdxAccIdx;

                // Write Attributes (Welded)
                // We reuse the same vertex buffer 'remap' for all attributes.
                // Warning: If we welded based on position only, normals might be averaged/mixed arbitrarily by remapVertexBuffer.
                // For LODs this is acceptable.
                
                // Position
                {
                    int32_t newPosViewIdx = static_cast<int32_t>(result.bufferViews.size());
                    size_t posBytes = uniqueVertexCount * 12; // vec3
                    while(result.buffers[0].cesium.data.size() % 4 != 0) result.buffers[0].cesium.data.push_back(std::byte(0));
                    size_t posOffset = result.buffers[0].cesium.data.size();
                    result.buffers[0].cesium.data.resize(posOffset + posBytes);
                    std::memcpy(result.buffers[0].cesium.data.data() + posOffset, weldedPositions.data(), posBytes);

                    CesiumGltf::BufferView bvPos;
                    bvPos.buffer = 0;
                    bvPos.byteLength = posBytes;
                    bvPos.byteOffset = posOffset;
                    bvPos.target = CesiumGltf::BufferView::Target::ARRAY_BUFFER;
                    result.bufferViews.push_back(bvPos);

                    CesiumGltf::Accessor accPos;
                    accPos.bufferView = newPosViewIdx;
                    accPos.componentType = CesiumGltf::Accessor::ComponentType::FLOAT;
                    accPos.count = uniqueVertexCount;
                    accPos.type = CesiumGltf::Accessor::Type::VEC3;
                    // Compute min/max for positions
                    glm::vec3 minV(FLT_MAX);
                    glm::vec3 maxV(-FLT_MAX);
                    for (const auto& v : weldedPositions) {
                        minV.x = std::min(minV.x, v.x);
                        minV.y = std::min(minV.y, v.y);
                        minV.z = std::min(minV.z, v.z);
                        maxV.x = std::max(maxV.x, v.x);
                        maxV.y = std::max(maxV.y, v.y);
                        maxV.z = std::max(maxV.z, v.z);
                    }
                    accPos.min = { minV.x, minV.y, minV.z };
                    accPos.max = { maxV.x, maxV.y, maxV.z };
                    int32_t newPosAccIdx = static_cast<int32_t>(result.accessors.size());
                    result.accessors.push_back(accPos);
                    newPrim.attributes["POSITION"] = newPosAccIdx;
                }

                // Normal (if exists)
                if (hasNormal && oldNormalAccessor >= 0 && oldNormalAccessor < static_cast<int32_t>(source.accessors.size())) {
                    const CesiumGltf::Accessor& oldAcc = source.accessors[oldNormalAccessor];
                    const CesiumGltf::BufferView& oldView = source.bufferViews[oldAcc.bufferView];
                    const std::byte* oldData = source.buffers[oldView.buffer].cesium.data.data() + oldView.byteOffset + oldAcc.byteOffset;
                    
                    std::vector<glm::vec3> normals(vertexCount); // Assuming vec3 float
                    size_t normalStride = static_cast<size_t>(oldView.byteStride.value_or(12));
                    if (normalStride != 12) {
                         for(size_t k=0; k<vertexCount; ++k) {
                            const float* p = reinterpret_cast<const float*>(oldData + k * normalStride);
                            normals[k] = glm::vec3(p[0], p[1], p[2]);
                        }
                    } else {
                        std::memcpy(normals.data(), oldData, vertexCount * 12);
                    }

                    std::vector<glm::vec3> weldedNormals(uniqueVertexCount);
                    meshopt_remapVertexBuffer(weldedNormals.data(), normals.data(), vertexCount, sizeof(glm::vec3), remap.data());

                    int32_t newViewIdx = static_cast<int32_t>(result.bufferViews.size());
                    size_t bytes = uniqueVertexCount * 12;
                    while(result.buffers[0].cesium.data.size() % 4 != 0) result.buffers[0].cesium.data.push_back(std::byte(0));
                    size_t offset = result.buffers[0].cesium.data.size();
                    result.buffers[0].cesium.data.resize(offset + bytes);
                    std::memcpy(result.buffers[0].cesium.data.data() + offset, weldedNormals.data(), bytes);

                    CesiumGltf::BufferView bv;
                    bv.buffer = 0;
                    bv.byteLength = bytes;
                    bv.byteOffset = offset;
                    bv.target = CesiumGltf::BufferView::Target::ARRAY_BUFFER;
                    result.bufferViews.push_back(bv);

                    CesiumGltf::Accessor acc;
                    acc.bufferView = newViewIdx;
                    acc.componentType = CesiumGltf::Accessor::ComponentType::FLOAT;
                    acc.count = uniqueVertexCount;
                    acc.type = CesiumGltf::Accessor::Type::VEC3;
                    result.accessors.push_back(acc);
                    newPrim.attributes["NORMAL"] = static_cast<int32_t>(result.accessors.size() - 1);
                }
                
                // Only handling POS and NORMAL for now to save space/time
                // Can add UV similarly if needed
                newMesh.primitives.push_back(std::move(newPrim));
            }

            if (!newMesh.primitives.empty()) {
                meshRemap[meshIndex] = static_cast<int32_t>(newMeshes.size());
                newMeshes.push_back(std::move(newMesh));
            }
        }

        result.meshes = std::move(newMeshes);

        // Remap node mesh indices to new mesh list, drop nodes with removed meshes
        for (auto& node : result.nodes) {
            if (node.mesh >= 0) {
                if (node.mesh < static_cast<int32_t>(meshRemap.size()) && meshRemap[node.mesh] >= 0) {
                    node.mesh = meshRemap[node.mesh];
                } else {
                    node.mesh = -1;
                }
            }
        }

        // Remove empty nodes (no mesh and no children) and remap indices
        std::vector<int32_t> nodeRemap(result.nodes.size(), -1);
        std::vector<CesiumGltf::Node> newNodes;
        newNodes.reserve(result.nodes.size());

        // Depth-first prune: returns new node index or -1 if pruned
        std::function<int32_t(int32_t)> pruneNode = [&](int32_t nodeIndex) -> int32_t {
            if (nodeIndex < 0 || nodeIndex >= static_cast<int32_t>(result.nodes.size())) return -1;
            if (nodeRemap[nodeIndex] >= 0) return nodeRemap[nodeIndex];

            const auto& oldNode = result.nodes[nodeIndex];
            CesiumGltf::Node newNode = oldNode;
            newNode.children.clear();

            for (int32_t child : oldNode.children) {
                int32_t newChild = pruneNode(child);
                if (newChild >= 0) newNode.children.push_back(newChild);
            }

            bool hasMesh = newNode.mesh >= 0;
            bool hasChildren = !newNode.children.empty();
            if (!hasMesh && !hasChildren) {
                nodeRemap[nodeIndex] = -1;
                return -1;
            }

            int32_t newIndex = static_cast<int32_t>(newNodes.size());
            nodeRemap[nodeIndex] = newIndex;
            newNodes.push_back(std::move(newNode));
            return newIndex;
        };

        // Rebuild scene roots
        for (auto& scene : result.scenes) {
            std::vector<int32_t> newRoots;
            for (int32_t root : scene.nodes) {
                int32_t newRoot = pruneNode(root);
                if (newRoot >= 0) newRoots.push_back(newRoot);
            }
            scene.nodes = std::move(newRoots);
        }
        result.nodes = std::move(newNodes);

        // Remove unused materials and remap indices
        std::vector<int32_t> materialRemap(result.materials.size(), -1);
        std::vector<CesiumGltf::Material> newMaterials;
        newMaterials.reserve(result.materials.size());

        for (auto& mesh : result.meshes) {
            for (auto& prim : mesh.primitives) {
                if (prim.material >= 0 && prim.material < static_cast<int32_t>(materialRemap.size())) {
                    if (materialRemap[prim.material] < 0) {
                        materialRemap[prim.material] = static_cast<int32_t>(newMaterials.size());
                        newMaterials.push_back(result.materials[prim.material]);
                    }
                    prim.material = materialRemap[prim.material];
                }
            }
        }
        result.materials = std::move(newMaterials);
        
        // Update buffer byte lengths
        if (!result.buffers.empty()) {
            result.buffers[0].byteLength = static_cast<int64_t>(result.buffers[0].cesium.data.size());
        }

        return result;
    }

} // namespace NonInstancingLOD
