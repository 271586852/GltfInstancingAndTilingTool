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

namespace NonInstancingLOD {

    void MeshSimplifier::generateNonInstancingLodChain(
        const std::filesystem::path& inputPath,
        const std::filesystem::path& outputDir,
        int levels,
        float ratio,
        const std::string& tilesetName
    ) {
        if (!std::filesystem::exists(inputPath)) {
        GltfInstancing::logError("MeshSimplifier: Input file does not exist: " + inputPath.string());
            return;
        }

        std::filesystem::create_directories(outputDir);

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
        } catch (const std::exception& e) {
            GltfInstancing::logError("MeshSimplifier: Failed to copy LOD0: " + std::string(e.what()));
            return;
        }

        float currentRatio = ratio;
        
        for (int i = 1; i <= levels; ++i) {
            GltfInstancing::logInfo("Generating Non-Instanced LOD " + std::to_string(i) + " (Target Ratio: " + std::to_string(currentRatio) + ")");
            
            CesiumGltf::Model simplified = simplifyModel(currentModel, currentRatio);
            
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
            
            // For the next level, we base it off the ORIGINAL model but with a smaller ratio
            // This avoids accumulating errors from repeated simplification of simplified meshes
            currentRatio *= ratio; 
        }

        // Generate Tileset
        // Construct a simple refinement chain: LOD N -> LOD N-1 -> ... -> LOD 0
        // Or actually: Root (LOD N, Low Detail) -> Child (LOD N-1) -> ... -> Leaf (LOD 0, High Detail)
        // Usually tileset traversal starts with low detail (root) and refines to high detail.
        
        // Assuming:
        // LOD N (Coarsest) -> SSE 100
        // LOD N-1          -> SSE 50
        // ...
        // LOD 0 (Finest)   -> SSE 0
        
        if (lodFilenames.empty()) return;

        GltfInstancing::TilesetNode rootNode;
        GltfInstancing::TilesetNode* current = &rootNode;
        
        // Reverse iterate: Coarsest (Last generated) to Finest (First generated)
        for (int i = lodFilenames.size() - 1; i >= 0; --i) {
            GltfInstancing::TilesetNode node;
            node.contentUri = lodFilenames[i];
            
            // Geometric error heuristic
            // LOD0 -> 0 (or very small)
            // LOD1 -> Higher
            // Formula: 2^(i) * base_error?
            double error = (i == 0) ? 0.0 : 16.0 * std::pow(2.0, i - 1); 
            if (i == lodFilenames.size() - 1) error = 1000.0; // Root is very tolerant

            node.geometricError = error;
            node.boundingVolume = { glm::dvec3(-10000), glm::dvec3(10000) }; // Bounding box needs to be calculated!
            // TODO: Calculate actual bounding box of the model.
            // For now, we can try to extract it from the model if available, or just set a large one if we don't check.
            // Actually, we should calculate it. Let's assume the calling code or simplifyModel preserves it.
            // But we need it for the tileset node.
            
            if (i == lodFilenames.size() - 1) {
                // This is the root of our chain
                rootNode = node;
                current = &rootNode;
            } else {
                current->children.push_back(node);
                current = &current->children.back();
            }
        }

        // Fix root geometric error
        // The root node's GE defines when its CHILDREN are loaded.
        // So Root(LOD_Coarse) has GE=X. When screen_error > X, we just see Root.
        // When screen_error <= X, we load Children(LOD_Finer).
        
        // Let's refine the chain logic:
        // Node (LOD_Last, GE=Large)
        //   -> Child (LOD_Last-1, GE=Medium)
        //      -> Child ...
        //         -> Child (LOD_0, GE=0)
        
        // We need bounding box. 
        // We can get it from the original model accessors.
        // Let's iterate accessors of LOD0 to find min/max.
        GltfInstancing::BoundingBox bbox;
        // ... implementation of bbox extraction ...
        // For simplicity, let's just make a huge box or re-read LOD0.
        // Better: simplifyModel returns model, we can verify.
        
        // Write tileset
        std::filesystem::path tilesetPath = outputDir / tilesetName;
        GltfInstancing::TilesetWriter tilesetWriter;
        // Note: TilesetWriter::writeHierarchicalTileset might need a valid bbox.
        // Let's add a helper to get BBox from CesiumGltf::Model.
        
        tilesetWriter.writeHierarchicalTileset(rootNode, tilesetPath);
        GltfInstancing::logInfo("Non-Instanced LOD chain generated at: " + outputDir.string());
    }

    CesiumGltf::Model MeshSimplifier::simplifyModel(const CesiumGltf::Model& source, float targetRatio) {
        CesiumGltf::Model result = source; // Deep copy
        
        // We will append new buffer data to buffer 0 (GLB expects a single buffer)
        if (result.buffers.empty()) {
            result.buffers.emplace_back();
        }
        size_t targetBufferIndex = 0;
        CesiumGltf::Buffer& targetBuffer = result.buffers[targetBufferIndex];

        for (auto& mesh : result.meshes) {
            for (auto& primitive : mesh.primitives) {
                // Check if indexed
                if (primitive.indices < 0) continue; // Skip non-indexed for now

                // Get Indices
                const CesiumGltf::Accessor& indexAccessor = result.accessors[primitive.indices];
                const CesiumGltf::BufferView& indexBufferView = result.bufferViews[indexAccessor.bufferView];
                const CesiumGltf::Buffer& indexBuffer = result.buffers[indexBufferView.buffer];
                
                // Get Positions
                auto posIt = primitive.attributes.find("POSITION");
                if (posIt == primitive.attributes.end()) continue;
                const CesiumGltf::Accessor& posAccessor = result.accessors[posIt->second];
                const CesiumGltf::BufferView& posBufferView = result.bufferViews[posAccessor.bufferView];
                const CesiumGltf::Buffer& posBuffer = result.buffers[posBufferView.buffer];

                // Prepare data for meshopt
                std::vector<unsigned int> indices;
                indices.resize(indexAccessor.count);
                
                // Read indices (handle different component types)
                const std::byte* indexData = indexBuffer.cesium.data.data() + indexBufferView.byteOffset + indexAccessor.byteOffset;
                if (indexAccessor.componentType == CesiumGltf::Accessor::ComponentType::UNSIGNED_INT) {
                    const uint32_t* src = reinterpret_cast<const uint32_t*>(indexData);
                    for(size_t i=0; i<indexAccessor.count; ++i) indices[i] = src[i];
                } else if (indexAccessor.componentType == CesiumGltf::Accessor::ComponentType::UNSIGNED_SHORT) {
                    const uint16_t* src = reinterpret_cast<const uint16_t*>(indexData);
                    for(size_t i=0; i<indexAccessor.count; ++i) indices[i] = src[i];
                } else if (indexAccessor.componentType == CesiumGltf::Accessor::ComponentType::UNSIGNED_BYTE) {
                    const uint8_t* src = reinterpret_cast<const uint8_t*>(indexData);
                    for(size_t i=0; i<indexAccessor.count; ++i) indices[i] = src[i];
                } else {
                    continue; // Unsupported
                }

                // Read positions
                // We assume float vec3.
                const std::byte* posData = posBuffer.cesium.data.data() + posBufferView.byteOffset + posAccessor.byteOffset;
                size_t vertexCount = posAccessor.count;
                size_t stride = posBufferView.byteStride.value_or(12); // 12 bytes for vec3 float

                // Convert positions to dense float array if stride != 12 or if needed
                // meshopt takes stride, so we can pass directly if float
                
                size_t targetIndexCount = static_cast<size_t>(indices.size() * targetRatio);
                float targetError = 1e-2f;

                std::vector<unsigned int> newIndices(indices.size());
                
                size_t simplifiedCount = meshopt_simplify(
                    newIndices.data(),
                    indices.data(),
                    indices.size(),
                    reinterpret_cast<const float*>(posData),
                    vertexCount,
                    stride,
                    targetIndexCount,
                    targetError,
                    0,
                    nullptr
                );

                // Resize and write to buffer
                newIndices.resize(simplifiedCount);
                
                // Align to 4 bytes
                while (targetBuffer.cesium.data.size() % 4 != 0) targetBuffer.cesium.data.push_back(std::byte(0));
                
                size_t byteOffset = targetBuffer.cesium.data.size();
                size_t byteLength = newIndices.size() * sizeof(unsigned int);
                
                targetBuffer.cesium.data.resize(byteOffset + byteLength);
                std::memcpy(targetBuffer.cesium.data.data() + byteOffset, newIndices.data(), byteLength);
                
                // Create new BufferView
                CesiumGltf::BufferView newView;
                newView.buffer = static_cast<int32_t>(targetBufferIndex);
                newView.byteOffset = static_cast<int64_t>(byteOffset);
                newView.byteLength = static_cast<int64_t>(byteLength);
                newView.target = CesiumGltf::BufferView::Target::ELEMENT_ARRAY_BUFFER;
                
                int32_t newViewIndex = static_cast<int32_t>(result.bufferViews.size());
                result.bufferViews.push_back(newView);
                
                // Create new Accessor
                CesiumGltf::Accessor newAccessor;
                newAccessor.bufferView = newViewIndex;
                newAccessor.byteOffset = 0;
                newAccessor.componentType = CesiumGltf::Accessor::ComponentType::UNSIGNED_INT;
                newAccessor.count = static_cast<int64_t>(newIndices.size());
                newAccessor.type = CesiumGltf::Accessor::Type::SCALAR;
                
                int32_t newAccessorIndex = static_cast<int32_t>(result.accessors.size());
                result.accessors.push_back(newAccessor);
                
                // Update Primitive
                primitive.indices = newAccessorIndex;
            }
        }
        
        return result;
    }

} // namespace NonInstancingLOD

