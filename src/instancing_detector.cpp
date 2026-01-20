#include "instancing_detector.h"
#include "utilities.h" // For logging, transform math, compare functions (though signature is preferred)

#include <CesiumGltf/Accessor.h> // Ensure Accessor.h is included for its static methods
#include <CesiumGltf/AccessorView.h>
#include <CesiumGltf/ExtensionExtMeshGpuInstancing.h>
#include <CesiumGltf/AccessorSpec.h> // Explicitly include AccessorSpec.h
// #include <CesiumGltf/AccessorTypes.h> // Removed as it seems problematic or redundant
// #include <CesiumGltf/AccessorUtilities.h> // Removed this incorrect include
#include <set> // For ordering primitive attributes consistently
#include <vector> // For std::vector
#include <string> // For std::string, explicitly included
#include <iomanip> // For std::hex, std::setw, std::setfill
#include <sstream> // For std::ostringstream, explicitly included
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <functional> // For std::hash
#include <spdlog/spdlog.h> // 确保 spdlog 已包含
#include <cctype> // For isprint

namespace GltfInstancing {

    // TARGET_MESH_NAMES and hash_combine, hashAccessorData remain here as they are specific to instancing_detector logic
    // or are helper functions closely tied to it and not general utilities.
    const std::set<std::string> TARGET_MESH_NAMES = {
        "16e8662fce06bb27a5f40e5b910eeaee", // mesh105
        "e07caf21ead09641c32f8692d73e913e", // mesh103
        "4c70cee7887fa7ac24f1a9bf0a3a272e", // mesh146
        // Removed other mesh names to reduce log verbosity for now
        // "b1fc7322806483e4b3c352e26e5cc623", // mesh110
        // "15d378bda51d9e0b24e2924a8f1c95db", // mesh111 
        // "0c72923173eb539adffbe05dbf5c9500", // mesh64 - was part of original debug set
        // "d5b342d0dde053f350500bd0acd93a58", // mesh36 - was part of original debug set
        // "13be3312e3f71a73611e544c6cfa60bf", // mesh41 - was part of original debug set
        // "989a1deb55f36defd9e9d4faaa8a9a36", // mesh32 - was part of original debug set
        // "54b9250c8a0322b4050558c4e6d5659c", // mesh34 - was part of original debug set
        // "a9b637f5b5b96691dd9385e77d018083"  // mesh20 - was part of original debug set      
    };
    
    // Combined hash function (from various sources, e.g., Boost)
    // Moved to GltfInstancing namespace
    template <class T>
    inline void hash_combine(std::size_t& seed, const T& v) {
        std::hash<T> hasher;
        seed ^= hasher(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    }

    // Hashes the data of an accessor.
    // Moved to GltfInstancing namespace
    // accessorIndex is the index in model.accessors
    // logDetailsForThisAccessor is a flag to enable detailed logging for specific accessors (e.g., POSITION of a target mesh)
    size_t hashAccessorData(
        const CesiumGltf::Model& model,
        int32_t accessorIndex,                      // Use index to construct AccessorView
        bool logDetailsForThisAccessor,
        const std::string& meshNameParam,               // Parameter for context if needed, e.g. for TARGET_MESH_NAMES checks
        const std::string& attributeName,                   // Name of the attribute being hashed (e.g., "NORMAL", "TEXCOORD_0")
        double attributeSpecificTolerance = 0.0) {          // Specific tolerance for this attribute (e.g., normalTolerance)

        if (accessorIndex < 0 || static_cast<size_t>(accessorIndex) >= model.accessors.size()) {
            if (logDetailsForThisAccessor) {
                logMessage("      DEBUG_SIGNATURE: Invalid accessorIndex " + std::to_string(accessorIndex) + " provided to hashAccessorData.");
            }
            size_t error_seed = 0;
            hash_combine(error_seed, static_cast<int32_t>(-1)); // Indicate error
            return error_seed;
        }

        const CesiumGltf::Accessor& accessor = model.accessors[static_cast<size_t>(accessorIndex)];

        // Always perform detailed type check if view.status() is invalid later, 
        // or if logDetailsForThisAccessor is true for targeted debugging.
        // We define typeString here to be available for both conditions.
        const std::string& typeString = accessor.type;

        // ---- TOLERANCE HASHING FOR NORMALS ----
        if (attributeName == "NORMAL" && 
            typeString == CesiumGltf::Accessor::Type::VEC3 && 
            accessor.componentType == CesiumGltf::Accessor::ComponentType::FLOAT &&
            attributeSpecificTolerance > 1e-9) { // Check if specific tolerance is active for normals

            CesiumGltf::AccessorView<glm::vec3> normalView(model, accessorIndex);
            if (normalView.status() == CesiumGltf::AccessorViewStatus::Valid) {
                size_t normal_seed = 0;
                for (int64_t i = 0; i < normalView.size(); ++i) {
                    glm::vec3 v = normalView[i];
                    // Quantize the normal vector based on the tolerance
                    glm::vec3 quantized_v = glm::round(v / static_cast<float>(attributeSpecificTolerance));
                    
                    // Hash the quantized components
                    hash_combine(normal_seed, quantized_v.x);
                    hash_combine(normal_seed, quantized_v.y);
                    hash_combine(normal_seed, quantized_v.z);
                }
                if (logDetailsForThisAccessor) {
                    logMessage("      DEBUG_SIGNATURE: Hashed NORMAL attribute data with tolerance " + 
                               std::to_string(attributeSpecificTolerance) + ". Accessor: " + 
                               std::to_string(accessorIndex) + ". Hash: " + std::to_string(normal_seed) + 
                               " (Mesh: " + meshNameParam + ")");
                     // Optionally log first few original and quantized normals if extreme debugging is needed
                    if (normalView.size() > 0) {
                        glm::vec3 v_orig = normalView[0];
                        glm::vec3 q_orig = glm::round(v_orig / static_cast<float>(attributeSpecificTolerance));
                        logMessage("        Example Original NORMAL[0]: (" + std::to_string(v_orig.x) + ", " + std::to_string(v_orig.y) + ", " + std::to_string(v_orig.z) + ")");
                        logMessage("        Example Quantized NORMAL[0]: (" + std::to_string(q_orig.x) + ", " + std::to_string(q_orig.y) + ", " + std::to_string(q_orig.z) + ")");
                    }
                }
                return normal_seed;
            } else if (logDetailsForThisAccessor) {
                spdlog::warn("      DEBUG_SIGNATURE: AccessorView<glm::vec3> for NORMAL attribute (Accessor {}) failed with status: {}. Cannot apply tolerance hashing.", accessorIndex, static_cast<int>(normalView.status()));
                // Fall through to default hashing if view is invalid
            }
        }
        // ---- END OF TOLERANCE HASHING FOR NORMALS ----

        CesiumGltf::AccessorView<std::byte> byteView(model, accessorIndex);

        // If AccessorView<std::byte> is valid (i.e. accessor data is single bytes), hash its content directly.
        // This case is unlikely for POSITION, NORMAL etc. but good to have.
        if (byteView.status() == CesiumGltf::AccessorViewStatus::Valid) {
            std::string_view data_view(
                reinterpret_cast<const char*>(byteView.data()),
                static_cast<size_t>(byteView.size()) // byteView.size() is number of bytes here
            );
            size_t currentHash = std::hash<std::string_view>{}(data_view);
            if (logDetailsForThisAccessor) {
                logMessage("      DEBUG_SIGNATURE: Hashed directly using AccessorView<std::byte> (valid). Hash: " + std::to_string(currentHash));
            }
            return currentHash;
        }

        // Log details if byteView failed (which is expected for multi-byte elements due to WrongSizeT)
        // This logging block is mostly kept for debugging consistency if we see unexpected byteView statuses.
        if (byteView.status() != CesiumGltf::AccessorViewStatus::Valid && logDetailsForThisAccessor) {
             spdlog::info("      DEBUG_SIGNATURE: AccessorView<std::byte> initial status: {} (Enum val {}) for Accessor Index: {}. This is often expected (e.g. WrongSizeT for multi-byte elements).",
                         static_cast<int>(byteView.status()), static_cast<int>(byteView.status()), accessorIndex);
            // The detailed logging about accessor.type, bufferView, buffer etc. that was here previously
            // can be re-enabled if further deep debugging of the byteView failure (beyond WrongSizeT) is needed.
            // For now, we assume WrongSizeT is the primary reason and proceed to typed views.
        }
        
        // Attempt to use a typed AccessorView to get raw data
        // Calculate total byte size for hashing later, assuming data is tightly packed.
        int64_t numComponents = CesiumGltf::Accessor::computeNumberOfComponents(accessor.type);
        int64_t componentSizeInBytes = CesiumGltf::Accessor::computeByteSizeOfComponent(accessor.componentType);
        
        if (numComponents == 0 || componentSizeInBytes == 0) {
             spdlog::warn("      DEBUG_SIGNATURE: Accessor {} has numComponents ({}) or componentSizeInBytes ({}) as 0. Cannot determine element size for typed view hash.", accessorIndex, numComponents, componentSizeInBytes);
             // Fallback to min/max hash if element size cannot be determined
        } else {
            size_t totalByteSizeForAccessor = static_cast<size_t>(accessor.count) * static_cast<size_t>(numComponents) * static_cast<size_t>(componentSizeInBytes);
            const std::byte* rawDataPtr = nullptr;
            bool typedViewValid = false;

            // VEC3 / FLOAT (e.g., POSITION, NORMAL)
            if (typeString == CesiumGltf::Accessor::Type::VEC3 && accessor.componentType == CesiumGltf::Accessor::ComponentType::FLOAT) {
                CesiumGltf::AccessorView<glm::vec3> typedView(model, accessorIndex);
                if (typedView.status() == CesiumGltf::AccessorViewStatus::Valid) {
                    rawDataPtr = reinterpret_cast<const std::byte*>(typedView.data());
                    typedViewValid = true;

                    if (logDetailsForThisAccessor) {
                        spdlog::info("      DEBUG_SIGNATURE: Accessor {} is VEC3 FLOAT. TypedView is valid.");
                        spdlog::info("      DEBUG_SIGNATURE: First up to 3 VEC3 float values:");
                        for (int64_t i = 0; i < std::min(3LL, typedView.size()); ++i) {
                            glm::vec3 val = typedView[i];
                            spdlog::info("        [{}]: ({:.8f}, {:.8f}, {:.8f})", i, val.x, val.y, val.z);
                        }
                    }
                } else if (logDetailsForThisAccessor) {
                    spdlog::warn("      DEBUG_SIGNATURE: AccessorView<glm::vec3> for Accessor {} failed with status: {}", accessorIndex, static_cast<int>(typedView.status()));
                }
            } else if (accessor.type == CesiumGltf::Accessor::Type::SCALAR && accessor.componentType == CesiumGltf::Accessor::ComponentType::UNSIGNED_INT) {
                CesiumGltf::AccessorView<uint32_t> typedView(model, accessorIndex);
                if (typedView.status() == CesiumGltf::AccessorViewStatus::Valid) {
                    rawDataPtr = reinterpret_cast<const std::byte*>(typedView.data());
                    typedViewValid = true;
                } else if (logDetailsForThisAccessor) {
                    spdlog::warn("      DEBUG_SIGNATURE: AccessorView<uint32_t> for Accessor {} failed with status: {}", accessorIndex, static_cast<int>(typedView.status()));
                }
            } else if (accessor.type == CesiumGltf::Accessor::Type::VEC2 && accessor.componentType == CesiumGltf::Accessor::ComponentType::FLOAT) {
                CesiumGltf::AccessorView<glm::vec2> typedView(model, accessorIndex);
                if (typedView.status() == CesiumGltf::AccessorViewStatus::Valid) {
                    rawDataPtr = reinterpret_cast<const std::byte*>(typedView.data());
                    typedViewValid = true;
                } else if (logDetailsForThisAccessor) {
                     spdlog::warn("      DEBUG_SIGNATURE: AccessorView<glm::vec2> for Accessor {} failed with status: {}", accessorIndex, static_cast<int>(typedView.status()));
                }
            } else if (accessor.type == CesiumGltf::Accessor::Type::SCALAR && accessor.componentType == CesiumGltf::Accessor::ComponentType::UNSIGNED_SHORT) {
                CesiumGltf::AccessorView<uint16_t> typedView(model, accessorIndex);
                if (typedView.status() == CesiumGltf::AccessorViewStatus::Valid) {
                    rawDataPtr = reinterpret_cast<const std::byte*>(typedView.data());
                    typedViewValid = true;
                } else if (logDetailsForThisAccessor) {
                     spdlog::warn("      DEBUG_SIGNATURE: AccessorView<uint16_t> for Accessor {} failed with status: {}", accessorIndex, static_cast<int>(typedView.status()));
                }
            }
            // Add more 'else if' blocks here for other common accessor type/componentType combinations if needed

            if (typedViewValid && rawDataPtr) {
                std::string_view data_view(reinterpret_cast<const char*>(rawDataPtr), totalByteSizeForAccessor);
                size_t currentHash = std::hash<std::string_view>{}(data_view);
                if (logDetailsForThisAccessor) {
                    logMessage("      DEBUG_SIGNATURE: Hashed using typed AccessorView. Accessor: " + std::to_string(accessorIndex) + ", Type: " + accessor.type + ", CompType: " + std::to_string(accessor.componentType) + ". Hash: " + std::to_string(currentHash));
                    // Optionally log first few bytes if needed for extreme debugging
                    // std::ostringstream oss_bytes;
                    // oss_bytes << "        First 16 bytes: ";
                    // for (size_t k=0; k < std::min(totalByteSizeForAccessor, static_cast<size_t>(16)); ++k) {
                    //    oss_bytes << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(rawDataPtr[k]) << " ";
                    // }
                    // logMessage(oss_bytes.str());
                }
                return currentHash;
            } else if (logDetailsForThisAccessor) {
                 logMessage("      DEBUG_SIGNATURE: Typed AccessorView approach failed or not applicable for Accessor " + std::to_string(accessorIndex) + ". Type: " + accessor.type + ", CompType: " + std::to_string(accessor.componentType) + ". Proceeding to fallback hash.");
            }
        } // End of if (numComponents > 0 && componentSizeInBytes > 0)

        // Fallback: Hash accessor properties including min/max if typed view failed or was not applicable
        // The detailed logging that was previously inside the 'if (byteView.status() != CesiumGltf::AccessorViewStatus::Valid)' block
        // regarding accessor.type, componentType, bufferView, buffer, etc. is now implicitly covered by the fact we reached here.
        // If we still want that explicit logging, it can be re-added here, perhaps conditioned on logDetailsForThisAccessor.
        // For now, assume it was logged if byteView failed initially with logDetailsForThisAccessor.

        if (logDetailsForThisAccessor) { // This implies we are already logging details for this accessor
             spdlog::info("      DEBUG_SIGNATURE: Fallback hashing for Accessor Index: {}. Original byteView status: {} (likely WrongSizeT)", 
                         accessorIndex, static_cast<int>(byteView.status()));
             // We can re-add the very detailed logs (type hex, buffer details etc.) here if the typed view strategy
             // fails often and we need to re-investigate the byteView path.
             // Example:
             // std::string typeDetailsMsg = "      DEBUG_SIGNATURE: Fallback - Detailed Type Check for Accessor " + ...
             // logMessage(typeDetailsMsg);
             // logMessage("      DEBUG_SIGNATURE: Fallback - Accessor Details: Index: " + ...);
        }
        
        size_t fallback_seed = 0;
        hash_combine(fallback_seed, accessor.type);
        hash_combine(fallback_seed, accessor.componentType);
        hash_combine(fallback_seed, accessor.count);
        hash_combine(fallback_seed, accessor.normalized);

        if (!accessor.min.empty()) {
            if (logDetailsForThisAccessor) {
                logMessage("        DEBUG_SIGNATURE: (Fallback) Hashing accessor.min values.");
            }
            for (double val : accessor.min) {
                hash_combine(fallback_seed, val);
            }
        } else {
             hash_combine(fallback_seed, static_cast<size_t>(0xDECAFBAD)); 
        }

        if (!accessor.max.empty()) {
            if (logDetailsForThisAccessor) {
                logMessage("        DEBUG_SIGNATURE: (Fallback) Hashing accessor.max values.");
            }
            for (double val : accessor.max) {
                hash_combine(fallback_seed, val);
            }
        } else {
            hash_combine(fallback_seed, static_cast<size_t>(0xBAADF00D)); 
        }
        
        // Include original byteView status in fallback hash to differentiate if needed
        hash_combine(fallback_seed, static_cast<int>(byteView.status())); 

        if (logDetailsForThisAccessor) {
             logMessage("      DEBUG_SIGNATURE: Accessor data hash (from FALLBACK properties): " + std::to_string(fallback_seed));
        }
        return fallback_seed;
    }

    // Helper function to convert component type int to string
    std::string componentTypeToString(int32_t componentType) {
        switch (componentType) {
            case CesiumGltf::AccessorSpec::ComponentType::BYTE:
                return "BYTE (5120)";
            case CesiumGltf::AccessorSpec::ComponentType::UNSIGNED_BYTE:
                return "UNSIGNED_BYTE (5121)";
            case CesiumGltf::AccessorSpec::ComponentType::SHORT:
                return "SHORT (5122)";
            case CesiumGltf::AccessorSpec::ComponentType::UNSIGNED_SHORT:
                return "UNSIGNED_SHORT (5123)";
            case CesiumGltf::AccessorSpec::ComponentType::UNSIGNED_INT:
                return "UNSIGNED_INT (5125)";
            case CesiumGltf::AccessorSpec::ComponentType::FLOAT:
                return "FLOAT (5126)";
            default:
                return "UNKNOWN (" + std::to_string(componentType) + ")";
        }
    }

    InstancingDetector::InstancingDetector(double tolerance, 
                                           const std::set<std::string>& skipAttributes,
                                           double normalSpecificTolerance,
                                           int instanceLimit)
        : geometryTolerance(tolerance), 
          attributesToSkipDataHashInToleranceMode(skipAttributes),
          normalTolerance(normalSpecificTolerance),
          _instanceLimit(instanceLimit) {
        if (tolerance > 0.0) {
            logMessage("InstancingDetector initialized with geometry tolerance: " + std::to_string(tolerance));
            if (normalTolerance > 0.0 && !attributesToSkipDataHashInToleranceMode.count("NORMAL")) {
                 logMessage("  Will use NORMAL attribute tolerance: " + std::to_string(normalTolerance));
            } else if (attributesToSkipDataHashInToleranceMode.count("NORMAL")) {
                 logMessage("  NORMAL attribute data hashing will be SKIPPED as per --skip-attribute-data-hash.");
            } else {
                 logMessage("  NORMAL attribute data will be hashed EXACTLY (normal-tolerance is 0 or not applicable).");
            }

            if (!attributesToSkipDataHashInToleranceMode.empty()) {
                std::string skippedAttrsMsg = "  Will also skip data hashing for explicitly specified attributes: ";
                bool first = true;
                for (const auto& attr : attributesToSkipDataHashInToleranceMode) {
                    if (attr == "NORMAL") continue; 
                    if (!first) skippedAttrsMsg += ", ";
                    skippedAttrsMsg += attr;
                    first = false;
                }
                if (!first) { 
                    logMessage(skippedAttrsMsg);
                }
            }
        } else {
            logMessage("InstancingDetector initialized with exact matching (geometry tolerance <= 0.0).");
        }
        logMessage("Instance limit for forming groups: " + std::to_string(_instanceLimit));
    }

    // Helper to hash binary data (e.g., from accessors)
    size_t hashAccessorData(const CesiumGltf::Model& model, int32_t accessorIndex, bool logDetailsForThisAccessor); // Forward declare if not already

    // Definition for calculatePrimitiveSignatureExact
    size_t InstancingDetector::calculatePrimitiveSignatureExact(
        const CesiumGltf::Model& model,
        const CesiumGltf::MeshPrimitive& primitive,
        bool logDetailsForThisPrimitive,
        const std::string& meshName) {
        size_t seed = 0;
        // Existing hash_combine calls from the original calculatePrimitiveSignature
        // This includes hashing material, attributes (including POSITION), and morph targets exactly.

        // Ensure attributes are processed in a consistent order (e.g., by name)
        // The original code iterated over primitive.attributes directly, which is a std::map,
        // so it's already sorted by key (attribute name).

        if (logDetailsForThisPrimitive) {
            logMessage("    DEBUG_SIGNATURE_EXACT: Calculating EXACT signature for primitive.");
            logMessage("    DEBUG_SIGNATURE_EXACT: Material index: " + std::to_string(primitive.material));
            logMessage("    DEBUG_SIGNATURE_EXACT: Indices accessor index: " + std::to_string(primitive.indices));
            logMessage("    DEBUG_SIGNATURE_EXACT: Mode: " + std::to_string(primitive.mode));
        }

        GltfInstancing::hash_combine(seed, primitive.material); 
        GltfInstancing::hash_combine(seed, primitive.mode); // Primitive mode (POINTS, LINES, TRIANGLES, etc.)

        // Hash indices accessor if it exists
        if (primitive.indices >= 0 && static_cast<size_t>(primitive.indices) < model.accessors.size()) {
                const auto& indicesAccessor = model.accessors[primitive.indices];
                GltfInstancing::hash_combine(seed, indicesAccessor.type);
                GltfInstancing::hash_combine(seed, indicesAccessor.componentType);
                GltfInstancing::hash_combine(seed, indicesAccessor.count);
            GltfInstancing::hash_combine(seed, GltfInstancing::hashAccessorData(model, primitive.indices, logDetailsForThisPrimitive, meshName, "INDICES", 0.0));
        } else {
            // Hash a placeholder if no indices or invalid index
            GltfInstancing::hash_combine(seed, -1); // For type
            GltfInstancing::hash_combine(seed, -1); // For componentType
            GltfInstancing::hash_combine(seed, 0);  // For count
            GltfInstancing::hash_combine(seed, -1); // For data hash
        }


        // Attributes (including POSITION, NORMALS, TEXCOORD_0, etc.)
        // Iterating over std::map ensures consistent order by attribute name.
        for (const auto& attr_pair : primitive.attributes) {
            const std::string& attrName = attr_pair.first;
            int32_t accessorIndexForAttribute = attr_pair.second;
            // Corrected usage of meshName for determining logThisAttribute
            bool logThisAttribute = logDetailsForThisPrimitive && GltfInstancing::TARGET_MESH_NAMES.count(meshName);
            // If only primitive-level logging is generally on, but not for a specific mesh, this logic is fine.
            // If the intent is to always log attributes if logDetailsForThisPrimitive is true, then it should be:
            // bool logThisAttribute = logDetailsForThisPrimitive;
            // The line below makes it so attributes are only logged in detail if the mesh itself is a target *and* primitive logging is on.
            // This seems a reasonable interpretation of the original intent of TARGET_MESH_NAMES combined with primitive-specific logging flag.

            if (logDetailsForThisPrimitive) {
                 logMessage("    DEBUG_SIGNATURE_EXACT: Processing Attribute: " + attrName + " (Accessor Index: " + std::to_string(accessorIndexForAttribute) + ") for mesh " + meshName);
            }

            GltfInstancing::hash_combine(seed, attrName); 
            if (accessorIndexForAttribute >= 0 && static_cast<size_t>(accessorIndexForAttribute) < model.accessors.size()) {
                const auto& accessor = model.accessors[accessorIndexForAttribute];
                if (logThisAttribute) {
                    logMessage("        Accessor Type: " + accessor.type + ", ComponentType: " + componentTypeToString(accessor.componentType) + ", Count: " + std::to_string(accessor.count));
                    if (!accessor.min.empty()) {
                        std::string min_str = "        Min: [";
                        for (size_t k = 0; k < accessor.min.size(); ++k) min_str += std::to_string(accessor.min[k]) + (k == accessor.min.size() - 1 ? "" : ", ");
                        min_str += "]";
                        logMessage(min_str);
                    }
                    if (!accessor.max.empty()) {
                        std::string max_str = "        Max: [";
                        for (size_t k = 0; k < accessor.max.size(); ++k) max_str += std::to_string(accessor.max[k]) + (k == accessor.max.size() - 1 ? "" : ", ");
                        max_str += "]";
                        logMessage(max_str);
                    }
                }
                GltfInstancing::hash_combine(seed, accessor.type);
                GltfInstancing::hash_combine(seed, accessor.componentType);
                GltfInstancing::hash_combine(seed, accessor.count);
                GltfInstancing::hash_combine(seed, accessor.normalized);
                GltfInstancing::hash_combine(seed, GltfInstancing::hashAccessorData(model, accessorIndexForAttribute, logThisAttribute, meshName, attrName, 0.0)); 
            }
            else {
                if (logDetailsForThisPrimitive) logMessage("    DEBUG_SIGNATURE_EXACT: Invalid accessor index for attribute " + attrName + ": " + std::to_string(accessorIndexForAttribute));
                GltfInstancing::hash_combine(seed, -1); // Placeholder for invalid accessor attribute data
            }
        }
        
        if (logDetailsForThisPrimitive && !primitive.targets.empty()) {
            logMessage("    DEBUG_SIGNATURE_EXACT: Primitive has " + std::to_string(primitive.targets.size()) + " morph targets.");
        }

        // Morph Targets
        for (const auto& target : primitive.targets) {
            // Iterate over map for consistent order
            for (const auto& attr_pair : target) { 
                const std::string& attrName = attr_pair.first;
                int32_t accessorIndexForMorph = attr_pair.second;

                GltfInstancing::hash_combine(seed, attrName); 
                if (accessorIndexForMorph >= 0 && static_cast<size_t>(accessorIndexForMorph) < model.accessors.size()) {
                    const auto& accessor = model.accessors[accessorIndexForMorph];
                    GltfInstancing::hash_combine(seed, accessor.type);
                    GltfInstancing::hash_combine(seed, accessor.componentType);
                    GltfInstancing::hash_combine(seed, GltfInstancing::hashAccessorData(model, accessorIndexForMorph, false, meshName, attrName, 0.0)); 
                }
                else {
                    GltfInstancing::hash_combine(seed, -1); // Placeholder for invalid morph target accessor
                }
            }
        }
        if (logDetailsForThisPrimitive) {
            logMessage("    DEBUG_SIGNATURE_EXACT: Calculated EXACT Primitive Signature: " + std::to_string(seed));
        }
        return seed;
    }


    size_t InstancingDetector::calculatePrimitiveSignature(
        const CesiumGltf::Model& model,
        const CesiumGltf::MeshPrimitive& primitive,
        bool logDetailsForThisPrimitive,
        const std::string& meshName) {

        if (geometryTolerance <= 1e-9) { // Use a small epsilon for floating point comparison
            if (logDetailsForThisPrimitive) logMessage("    DEBUG_SIGNATURE: Using EXACT signature calculation (tolerance = " + std::to_string(geometryTolerance) + ") for mesh " + meshName);
            return calculatePrimitiveSignatureExact(model, primitive, logDetailsForThisPrimitive, meshName);
        }

        if (logDetailsForThisPrimitive) {
            logMessage("    DEBUG_SIGNATURE: Using TOLERANCE-BASED signature calculation (tolerance = " + std::to_string(geometryTolerance) + ") for mesh " + meshName + ", primitive.");
            logMessage("    DEBUG_SIGNATURE: Material index: " + std::to_string(primitive.material));
            logMessage("    DEBUG_SIGNATURE: Indices accessor index: " + std::to_string(primitive.indices));
            logMessage("    DEBUG_SIGNATURE: Mode: " + std::to_string(primitive.mode));
        }

        size_t seed = 0;

        // Hash material (exact match still required)
        GltfInstancing::hash_combine(seed, primitive.material);
        // Hash primitive mode (POINTS, LINES, TRIANGLES, etc. - exact match required)
        GltfInstancing::hash_combine(seed, primitive.mode);

        // Hash counts for vertices (from POSITION) and indices (if they exist)
        // These counts must be exact for primitives to be considered similar candidates.
        int32_t positionAccessorIndex = -1;
        auto posAttrIt = primitive.attributes.find("POSITION");
        if (posAttrIt != primitive.attributes.end()) {
            positionAccessorIndex = posAttrIt->second;
        }

        if (positionAccessorIndex >= 0 && static_cast<size_t>(positionAccessorIndex) < model.accessors.size()) {
            const auto& posAccessor = model.accessors[positionAccessorIndex];
            GltfInstancing::hash_combine(seed, posAccessor.count); // Vertex count
            if (logDetailsForThisPrimitive) logMessage("    DEBUG_SIGNATURE: Position Accessor Count (Vertex Count): " + std::to_string(posAccessor.count));
        } else {
            GltfInstancing::hash_combine(seed, static_cast<int64_t>(-1)); // Invalid/No position accessor count
            if (logDetailsForThisPrimitive) logMessage("    DEBUG_SIGNATURE: No/Invalid Position Accessor. Hashing count as -1.");
        }

        if (primitive.indices >= 0 && static_cast<size_t>(primitive.indices) < model.accessors.size()) {
            const auto& indicesAccessor = model.accessors[primitive.indices];
            GltfInstancing::hash_combine(seed, indicesAccessor.count); // Index count
             if (logDetailsForThisPrimitive) logMessage("    DEBUG_SIGNATURE: Indices Accessor Count: " + std::to_string(indicesAccessor.count));
        } else {
            // If no indices, hash 0 for count. If invalid index, also hash 0 (or a specific value).
            // Let's use 0 for "no indices" and -1 for "invalid index accessor" if we wanted to distinguish.
            // For simplicity, if not valid and positive, hash 0 for count.
            GltfInstancing::hash_combine(seed, static_cast<int64_t>(0)); 
            if (logDetailsForThisPrimitive) logMessage("    DEBUG_SIGNATURE: No/Invalid Indices Accessor. Hashing count as 0.");
        }
        
        // Hash other attributes (excluding POSITION). Their data is still hashed exactly.
        // Iterating over std::map ensures consistent order by attribute name.
        for (const auto& attr_pair : primitive.attributes) {
            const std::string& attrName = attr_pair.first;
            int32_t accessorIndexForAttribute = attr_pair.second;

            // Initialize flags/variables for each attribute inside the loop
            bool skipDataHashingForThisAttribute = false;
            double attributeSpecificToleranceForHashing = 0.0;
            // logDataForThisBaseAttribute determines if hashAccessorData logs raw data points.
            // It's for attributes that are NOT skipped and NOT POSITION, and part of a TARGET_MESH.
            bool logDataForThisBaseAttribute = false; 

            if (attrName == "POSITION") { 
                // Position attribute data is NOT hashed into the base signature in tolerance mode.
                // Its count and basic accessor properties are already hashed.
                // Its actual geometry will be compared by bounding box later if signatures match.
                skipDataHashingForThisAttribute = true;
                if (logDetailsForThisPrimitive) {
                    logMessage("    DEBUG_SIGNATURE: (Tolerance Mode) Skipping direct data hash for POSITION attribute: " + meshName);
                }
            } else if (attributesToSkipDataHashInToleranceMode.count(attrName)) {
                skipDataHashingForThisAttribute = true;
                if (logDetailsForThisPrimitive) {
                    logMessage("    DEBUG_SIGNATURE: (Tolerance Mode) Skipping direct data hash for attribute '" + attrName + "' as per user setting: " + meshName);
                }
            } else if (attrName == "NORMAL" && this->normalTolerance > 1e-9) {
                // Not skipping NORMAL, and a specific normalTolerance is active.
                // We will pass this tolerance to hashAccessorData.
                attributeSpecificToleranceForHashing = this->normalTolerance;
                if (logDetailsForThisPrimitive) {
                    logMessage("    DEBUG_SIGNATURE: (Tolerance Mode) Will use specific tolerance " + std::to_string(this->normalTolerance) + " for hashing NORMAL data: " + meshName);
                }
            }
            // For attributes not skipped and not POSITION with special tolerance (like NORMAL above),
            // their data will be hashed exactly by hashAccessorData if attributeSpecificToleranceForHashing remains 0.0.

            if (skipDataHashingForThisAttribute) {
                 // Hash only the attribute name to confirm its presence and type (handled by loop structure and accessor hashing below)
                GltfInstancing::hash_combine(seed, attrName); // Ensure attribute presence affects signature
                if (logDetailsForThisPrimitive) logMessage("    DEBUG_SIGNATURE: (Tolerance Mode) Hashed attribute name '" + attrName + "' for presence, skipped data hash.");
            } else {
                // Determine if detailed data logging should be enabled for this specific attribute
                if (logDetailsForThisPrimitive && GltfInstancing::TARGET_MESH_NAMES.count(meshName)) {
                    // Only enable detailed data logging if it's a target mesh, primitive logging is on,
                    // AND it's not POSITION (already handled) and not NORMAL if normal tolerance is active 
                    // (because NORMAL with tolerance has its own specific logging within hashAccessorData).
                    if (attrName != "POSITION" && !(attrName == "NORMAL" && this->normalTolerance > 1e-9)) {
                        logDataForThisBaseAttribute = true; 
                        if (logDetailsForThisPrimitive) { 
                            logMessage("      DEBUG_SIGNATURE: For TARGET MESH '" + meshName + "', Attribute '" + attrName + "' (NOT SKIPPED, NO SPECIFIC TOLERANCE), enabling detailed data hashing log for hashAccessorData.");
                        }
                    }
                }

                // Hash the actual attribute data (potentially with tolerance for NORMAL)
                size_t attributeDataHash = GltfInstancing::hashAccessorData(
                    model, 
                    accessorIndexForAttribute, 
                    logDataForThisBaseAttribute, // This flag is for very verbose data point logging
                    meshName, 
                    attrName,
                    attributeSpecificToleranceForHashing // Pass the specific tolerance (0.0 for exact, >0 for NORMAL if applicable)
                );
                GltfInstancing::hash_combine(seed, attributeDataHash);
                if (logDetailsForThisPrimitive) {
                    if (attributeSpecificToleranceForHashing > 0.0) {
                        logMessage("    DEBUG_SIGNATURE: (Tolerance Mode) Hashed attribute '" + attrName + "' data with tolerance " + std::to_string(attributeSpecificToleranceForHashing) + ". Hash: " + std::to_string(attributeDataHash));
                    } else {
                        logMessage("    DEBUG_SIGNATURE: (Tolerance Mode) Hashed attribute '" + attrName + "' data EXACTLY. Hash: " + std::to_string(attributeDataHash));
                    }
                }
            }
        }
        
        // Morph targets: For the base signature, we will hash them exactly.
        // If morph targets also need tolerance, this part would need a more complex mechanism.
        if (logDetailsForThisPrimitive && !primitive.targets.empty()) {
            logMessage("    DEBUG_SIGNATURE: Primitive has " + std::to_string(primitive.targets.size()) + " morph targets. Hashing them exactly for base signature.");
        }
        for (const auto& target : primitive.targets) {
            // Iterate over map for consistent order for morph target attributes
            for (const auto& attr_pair : target) {
                const std::string& attrName = attr_pair.first;
                int32_t accessorIndexForMorph = attr_pair.second;
                GltfInstancing::hash_combine(seed, attrName); 
                if (accessorIndexForMorph >= 0 && static_cast<size_t>(accessorIndexForMorph) < model.accessors.size()) {
                    const auto& accessor = model.accessors[accessorIndexForMorph];
                    GltfInstancing::hash_combine(seed, accessor.type);
                    GltfInstancing::hash_combine(seed, accessor.componentType);
                    GltfInstancing::hash_combine(seed, GltfInstancing::hashAccessorData(model, accessorIndexForMorph, false, meshName, attrName, 0.0)); 
                }
                else {
                    GltfInstancing::hash_combine(seed, -1);
                }
            }
        }

        if (logDetailsForThisPrimitive) {
            logMessage("    DEBUG_SIGNATURE (Tolerance Mode): Calculated Base Primitive Signature: " + std::to_string(seed));
        }
        return seed;
    }

    size_t InstancingDetector::calculateMeshSignature(
        const CesiumGltf::Model& model,
        const CesiumGltf::Mesh& mesh,
        const std::string& meshName) { 
        size_t seed = 0;
        bool logDetailsForThisMesh = GltfInstancing::TARGET_MESH_NAMES.count(meshName) > 0;

        if (logDetailsForThisMesh) {
            logMessage("  DEBUG_SIGNATURE: Calculating signature for TARGET MESH: " + meshName + 
                       " (Original Mesh Index: " + std::to_string(&mesh - &model.meshes[0]) + ")");
        }

        for (size_t i = 0; i < mesh.primitives.size(); ++i) {
            const auto& primitive = mesh.primitives[i];
            if (logDetailsForThisMesh) {
                logMessage("  DEBUG_SIGNATURE: Processing Primitive " + std::to_string(i) + " for mesh " + meshName);
            }
            // Pass meshName here
            GltfInstancing::hash_combine(seed, calculatePrimitiveSignature(model, primitive, logDetailsForThisMesh, meshName));
        }
        
        if (logDetailsForThisMesh) {
            logMessage("  DEBUG_SIGNATURE: Calculated MESH Signature for " + meshName + ": " + std::to_string(seed));
        }
        return seed;
    }

    void InstancingDetector::traverseNode(
        const LoadedGltfModel& loadedGltf,
        int32_t nodeIndex,
        const glm::dmat4& currentWorldTransform,
        std::map<size_t, InstancedMeshGroup>& potentialInstanceGroups,
        std::vector<NonInstancedMeshInfo>& nonInstancedItems,
        std::map<std::pair<int, int>, size_t>& meshToSignatureCache,
        std::vector<int32_t>& parentNodeIndicesChainForChildren 
    ) {
        if (nodeIndex < 0 || static_cast<size_t>(nodeIndex) >= loadedGltf.model.nodes.size()) {
            logError("traverseNode: Invalid nodeIndex " + std::to_string(nodeIndex) + " for model " + loadedGltf.originalPath.string());
            return;
        }

        const CesiumGltf::Node& node = loadedGltf.model.nodes[nodeIndex];
        
        glm::dmat4 localTransform = getLocalTransformMatrix(node);
        glm::dmat4 worldTransform = currentWorldTransform * localTransform;
        
        if (node.mesh >= 0) {
            if (static_cast<size_t>(node.mesh) < loadedGltf.model.meshes.size()) {
                const CesiumGltf::Mesh& mesh = loadedGltf.model.meshes[node.mesh];
                // std::string meshNameForLog = mesh.name.empty() ? ("Mesh_" + std::to_string(node.mesh)) : mesh.name;

                // EXT_mesh_gpu_instancing PRECEDES custom instancing detection for that mesh node
                auto instancingExtIt = node.extensions.find("EXT_mesh_gpu_instancing");
                if (instancingExtIt != node.extensions.end()) {
                    try {
                        const auto* extData = std::any_cast<CesiumGltf::ExtensionExtMeshGpuInstancing>(&instancingExtIt->second);
                        if (extData) {
                            size_t baseMeshSignature;
                            auto cacheKey = std::make_pair(loadedGltf.uniqueId, node.mesh);
                            auto cacheIt = meshToSignatureCache.find(cacheKey);
                            if (cacheIt != meshToSignatureCache.end()) {
                                baseMeshSignature = cacheIt->second;
                            } else {
                                baseMeshSignature = calculateMeshSignature(loadedGltf.model, mesh, mesh.name);
                                meshToSignatureCache[cacheKey] = baseMeshSignature;
                            }

                            int32_t translationAccessorIdx = extData->attributes.count("TRANSLATION") ? extData->attributes.at("TRANSLATION") : -1;
                            int32_t rotationAccessorIdx = extData->attributes.count("ROTATION") ? extData->attributes.at("ROTATION") : -1;
                            int32_t scaleAccessorIdx = extData->attributes.count("SCALE") ? extData->attributes.at("SCALE") : -1;
                            
                            int64_t instanceCount = 0;
                            // Determine instance count more robustly
                            if (translationAccessorIdx != -1 && static_cast<size_t>(translationAccessorIdx) < loadedGltf.model.accessors.size()) {
                                instanceCount = loadedGltf.model.accessors[translationAccessorIdx].count;
                            } else if (rotationAccessorIdx != -1 && static_cast<size_t>(rotationAccessorIdx) < loadedGltf.model.accessors.size()) {
                                instanceCount = loadedGltf.model.accessors[rotationAccessorIdx].count;
                            } else if (scaleAccessorIdx != -1 && static_cast<size_t>(scaleAccessorIdx) < loadedGltf.model.accessors.size()) {
                                instanceCount = loadedGltf.model.accessors[scaleAccessorIdx].count;
                            } else {
                                // Log if attributes are present but all accessors are invalid or missing count
                                if (!extData->attributes.empty()){
                                    logError("Node " + std::to_string(nodeIndex) + " has EXT_mesh_gpu_instancing but cannot determine instance count from its attributes.");
                                }
                                // instanceCount remains 0 if no valid accessor provides a count
                            }

                            if (instanceCount > 0) {
                                auto& group = potentialInstanceGroups[baseMeshSignature];
                                if (group.instances.empty()) { // First time encountering this base mesh (for exact or tolerance mode)
                                    group.representativeGltfModelIndex = loadedGltf.uniqueId;
                                    group.representativeMeshIndexInModel = node.mesh;
                                    group.meshSignature = baseMeshSignature;
                                    group.representativeMeshName = mesh.name;
                                    if (geometryTolerance > 1e-9) { // Tolerance mode: compute and store representative bounding boxes
                                        for (const auto& prim : mesh.primitives) {
                                            group.representativePrimitiveBoundingBoxes.push_back(GltfInstancing::getPrimitiveBoundingBox(loadedGltf.model, prim));
                                        }
                                }
                            }
                            
                            for (int64_t i = 0; i < instanceCount; ++i) {
                                MeshInstanceInfo instanceInfo;
                                instanceInfo.originalGltfIndex = loadedGltf.uniqueId;
                                instanceInfo.originalNodeIndex = nodeIndex;
                                instanceInfo.originalMeshIndex = node.mesh;
                                
                                    
                                    glm::dvec3 instTranslation(0.0);
                                    glm::dquat instRotation(1.0, 0.0, 0.0, 0.0); // Identity: w,x,y,z
                                    glm::dvec3 instScale(1.0);
                                    
                                    // 1. 读取平移 (TRANSLATION) 数据
                                    if (translationAccessorIdx != -1) {
                                        CesiumGltf::AccessorView<glm::vec3> tView(loadedGltf.model, translationAccessorIdx);
                                        if (tView.status() == CesiumGltf::AccessorViewStatus::Valid && i < tView.size()) {
                                            instTranslation = glm::dvec3(tView[i]);
                                        } else if (tView.status() != CesiumGltf::AccessorViewStatus::Valid) {
                                            logError("EXT_mesh_gpu_instancing: Invalid AccessorView for TRANSLATION on node " + std::to_string(nodeIndex));
                                        }
                                    }
                                    // 2. 读取旋转 (ROTATION) 数据
                                    if (rotationAccessorIdx != -1) {
                                        CesiumGltf::AccessorView<glm::vec4> rView(loadedGltf.model, rotationAccessorIdx);
                                        if (rView.status() == CesiumGltf::AccessorViewStatus::Valid && i < rView.size()) {
                                            glm::vec4 q_xyzw = rView[i];
                                            instRotation = glm::normalize(glm::dquat(q_xyzw.w, q_xyzw.x, q_xyzw.y, q_xyzw.z));
                                        } else if (rView.status() != CesiumGltf::AccessorViewStatus::Valid) {
                                            logError("EXT_mesh_gpu_instancing: Invalid AccessorView for ROTATION on node " + std::to_string(nodeIndex));
                                        }
                                    }
                                    // 3. 读取缩放 (SCALE) 数据
                                    if (scaleAccessorIdx != -1) {
                                        CesiumGltf::AccessorView<glm::vec3> sView(loadedGltf.model, scaleAccessorIdx);
                                        if (sView.status() == CesiumGltf::AccessorViewStatus::Valid && i < sView.size()) {
                                            instScale = glm::dvec3(sView[i]);
                                        } else if (sView.status() != CesiumGltf::AccessorViewStatus::Valid) {
                                            logError("EXT_mesh_gpu_instancing: Invalid AccessorView for SCALE on node " + std::to_string(nodeIndex));
                                        }
                                    }
                                    // 4. 计算实例的局部变换矩阵
                                    glm::dmat4 instanceLocalTRSMatrix = glm::translate(glm::dmat4(1.0), instTranslation) *
                                                                   glm::mat4_cast(instRotation) *
                                                                   glm::scale(glm::dmat4(1.0), instScale);                                    
                                    
                                    // 5. 计算实例的世界变换矩阵
                                    // worldTransform 是持有 EXT_mesh_gpu_instancing 扩展的那个节点的自身世界变换
                                    glm::dmat4 finalInstanceWorldTransform = worldTransform * instanceLocalTRSMatrix;

                                    // 6. 将实例的世界变换矩阵转换为 TransformComponents 结构
                                    instanceInfo.transform = TransformComponents::fromMat4(finalInstanceWorldTransform);
                                    
                                    group.instances.push_back(instanceInfo);
                                }
                            }
                        }
                    } catch (const std::bad_any_cast& e) {
                        logError("Failed to cast EXT_mesh_gpu_instancing extension data: " + std::string(e.what()) + " for node " + std::to_string(nodeIndex));
                    }

                } else { // Regular node mesh, apply custom instancing logic
                    size_t signature;
                    auto cacheKey = std::make_pair(loadedGltf.uniqueId, node.mesh); // Use uniqueId and mesh index
                    auto cacheIt = meshToSignatureCache.find(cacheKey);
                    if (cacheIt != meshToSignatureCache.end()) {
                        signature = cacheIt->second;
                    } else {
                        // calculateMeshSignature internally calls calculatePrimitiveSignature, which now handles tolerance
                        signature = calculateMeshSignature(loadedGltf.model, mesh, mesh.name);
                        meshToSignatureCache[cacheKey] = signature;
                    }

                    MeshInstanceInfo instanceInfo;
                    instanceInfo.originalGltfIndex = loadedGltf.uniqueId;
                    instanceInfo.originalNodeIndex = nodeIndex;
                    instanceInfo.originalMeshIndex = node.mesh;
                    instanceInfo.transform = TransformComponents::fromMat4(worldTransform);
                    
                    if (GltfInstancing::TARGET_MESH_NAMES.count(mesh.name)) { 
                        logMessage("Node " + std::to_string(nodeIndex) + " (mesh name: " + mesh.name + ", mesh index: " + std::to_string(node.mesh) + ") uses mesh with signature: " + std::to_string(signature) + (geometryTolerance > 1e-9 ? " (Tolerance Mode)" : " (Exact Mode)"));
                    }

                    if (geometryTolerance <= 1e-9) { // Exact matching mode
                    auto& group = potentialInstanceGroups[signature]; 
                    if (group.instances.empty()) { 
                        group.representativeGltfModelIndex = loadedGltf.uniqueId;
                        group.representativeMeshIndexInModel = node.mesh;
                        group.meshSignature = signature;
                        group.representativeMeshName = mesh.name; 
                    }
                    group.instances.push_back(instanceInfo);
                    } else { // Tolerance-based matching mode
                        auto groupIt = potentialInstanceGroups.find(signature);
                        bool foundMatchingGroup = false;
                        if (groupIt != potentialInstanceGroups.end()) {
                            // Group with matching base signature found, now compare bounding boxes
                            InstancedMeshGroup& existingGroup = groupIt->second;
                            if (existingGroup.representativePrimitiveBoundingBoxes.size() == mesh.primitives.size()) {
                                bool allPrimitivesSimilar = true;
                                std::vector<BoundingBox> currentPrimitiveBoundingBoxes;
                                for(const auto& prim : mesh.primitives) {
                                    currentPrimitiveBoundingBoxes.push_back(GltfInstancing::getPrimitiveBoundingBox(loadedGltf.model, prim));
                                }

                                for (size_t i = 0; i < mesh.primitives.size(); ++i) {
                                    if (!GltfInstancing::areBoundingBoxesSimilar(existingGroup.representativePrimitiveBoundingBoxes[i],
                                                                               currentPrimitiveBoundingBoxes[i], 
                                                                               geometryTolerance)) {
                                        allPrimitivesSimilar = false;
                                        if (GltfInstancing::TARGET_MESH_NAMES.count(mesh.name)) {
                                            // logMessage("    Mesh " + mesh.name + ": Primitive " + std::to_string(i) + " BBox mismatch with group representative.");
                                            const auto& repBox = existingGroup.representativePrimitiveBoundingBoxes[i];
                                            const auto& currentBox = currentPrimitiveBoundingBoxes[i];
                                            std::string reason = "    Mesh " + mesh.name + " (Node " + std::to_string(nodeIndex) + 
                                                                 ", Signature: " + std::to_string(signature) +")" +
                                                                 ": Primitive " + std::to_string(i) + " BBox mismatch with group representative.\n" +
                                                                 "      Tolerance: " + std::to_string(geometryTolerance) + "\n" +
                                                                 "      Rep. BBox Min: (" + std::to_string(repBox.min.x) + ", " + std::to_string(repBox.min.y) + ", " + std::to_string(repBox.min.z) + ")\n" +
                                                                 "      Rep. BBox Max: (" + std::to_string(repBox.max.x) + ", " + std::to_string(repBox.max.y) + ", " + std::to_string(repBox.max.z) + ")\n" +
                                                                 "      Cur. BBox Min: (" + std::to_string(currentBox.min.x) + ", " + std::to_string(currentBox.min.y) + ", " + std::to_string(currentBox.min.z) + ")\n" +
                                                                 "      Cur. BBox Max: (" + std::to_string(currentBox.max.x) + ", " + std::to_string(currentBox.max.y) + ", " + std::to_string(currentBox.max.z) + ")";
                                            GltfInstancing::logError(reason); // Using logError to make it stand out
                                        }
                                        break;
                                    }
                                }
                                if (allPrimitivesSimilar) {
                                    existingGroup.instances.push_back(instanceInfo);
                                    foundMatchingGroup = true;
                                    if (GltfInstancing::TARGET_MESH_NAMES.count(mesh.name)) {
                                         logMessage("    Mesh " + mesh.name + ": Added to existing group (Signature: " + std::to_string(signature) + ") based on BBox tolerance.");
                                    }
                                }
                            }
                        }

                        if (!foundMatchingGroup) {
                            // No existing group fully matched, or no group with this base signature yet.
                            // This mesh becomes a new representative for this base signature (if none existed) or a non-instanced mesh.
                            // For simplicity, if a group with this base signature existed but BBoxes didn't match,
                            // we treat this as non-instanced. A more complex strategy could form a new group.
                            
                            if (groupIt == potentialInstanceGroups.end()) { // This is the first mesh with this base signature
                                auto& newGroup = potentialInstanceGroups[signature]; // Creates new group
                                newGroup.representativeGltfModelIndex = loadedGltf.uniqueId;
                                newGroup.representativeMeshIndexInModel = node.mesh;
                                newGroup.meshSignature = signature;
                                newGroup.representativeMeshName = mesh.name;
                                for (const auto& prim : mesh.primitives) {
                                    newGroup.representativePrimitiveBoundingBoxes.push_back(GltfInstancing::getPrimitiveBoundingBox(loadedGltf.model, prim));
                                }
                                newGroup.instances.push_back(instanceInfo);
                                if (GltfInstancing::TARGET_MESH_NAMES.count(mesh.name)) {
                                     logMessage("    Mesh " + mesh.name + ": Created NEW group (Signature: " + std::to_string(signature) + ") and set as representative.");
                                }
                            } else {
                                // Base signature matched an existing group, but BBoxes didn't. Treat as non-instanced.
                                NonInstancedMeshInfo nonInstanced;
                                nonInstanced.originalGltfModelIndex = loadedGltf.uniqueId;
                                nonInstanced.originalMeshIndexInModel = node.mesh;
                                nonInstanced.originalNodeIndexInModel = nodeIndex;
                                nonInstanced.transform = instanceInfo.transform;
                                nonInstancedItems.push_back(nonInstanced);
                                if (GltfInstancing::TARGET_MESH_NAMES.count(mesh.name)) {
                                     // logMessage("    Mesh " + mesh.name + ": Base signature match, but BBox FAILED. Marked as non-instanced.");
                                     std::string reason = "WARNING: Mesh " + mesh.name + " (Node " + std::to_string(nodeIndex) +
                                                          ", Signature: " + std::to_string(signature) +
                                                          "): Base signature matched existing group, but BBox comparison FAILED overall. Marked as non-instanced.\n" +
                                                          "      Check BBox mismatch details for its primitives above if logged (search for logError with this mesh name/signature).";
                                     GltfInstancing::logMessage(reason); // Using logMessage with WARNING prefix
                                }
                            }
                        }
                    }
                }
            } else {
                logError("Node " + std::to_string(nodeIndex) + " in " + loadedGltf.originalPath.string() + " references invalid mesh index " + std::to_string(node.mesh));
            }
        }

        parentNodeIndicesChainForChildren.push_back(nodeIndex);
        for (int32_t childNodeIndex : node.children) {
            traverseNode(loadedGltf, childNodeIndex, worldTransform, potentialInstanceGroups, nonInstancedItems, meshToSignatureCache, parentNodeIndicesChainForChildren);
        }
        parentNodeIndicesChainForChildren.pop_back(); 
    }


    InstancingDetectionResult InstancingDetector::detect(const std::vector<LoadedGltfModel>& loadedModels) {
        logMessage("Starting instancing detection with instance limit: " + std::to_string(_instanceLimit));
        InstancingDetectionResult result;
        std::map<size_t, InstancedMeshGroup> potentialInstanceGroups; 
        std::map<std::pair<int, int>, size_t> meshToSignatureCache; 

        std::map<std::string, int> fileHashToRepresentativeModelId;
        std::map<int, int> modelIdToRepresentativeModelId; 

        for (const auto& loadedGltf : loadedModels) {
            if (loadedGltf.fileHash.empty()) continue; 

            auto it = fileHashToRepresentativeModelId.find(loadedGltf.fileHash);
            if (it == fileHashToRepresentativeModelId.end()) {
                fileHashToRepresentativeModelId[loadedGltf.fileHash] = loadedGltf.uniqueId;
                modelIdToRepresentativeModelId[loadedGltf.uniqueId] = loadedGltf.uniqueId; 
            } else {
                modelIdToRepresentativeModelId[loadedGltf.uniqueId] = it->second; 
                logMessage("GLB " + loadedGltf.originalPath.string() + " (ID: " + std::to_string(loadedGltf.uniqueId) +
                    ") is identical to GLB with ID: " + std::to_string(it->second) + ". Its meshes will be treated as instances of the first.");
            }
        }

        for (const auto& loadedGltf : loadedModels) {
            if (loadedGltf.model.scenes.empty()) {
                logMessage("Model " + loadedGltf.originalPath.string() + " has no scenes. Skipping node traversal.");
                continue;
            }

            int32_t sceneIndex = loadedGltf.model.scene >= 0 ? loadedGltf.model.scene : 0;
            if (static_cast<size_t>(sceneIndex) >= loadedGltf.model.scenes.size()) {
                logError("Model " + loadedGltf.originalPath.string() + " has invalid default scene index. Skipping node traversal.");
                continue;
            }
            const CesiumGltf::Scene& scene = loadedGltf.model.scenes[sceneIndex];

            std::vector<int32_t> initialParentChain; 
            for (int32_t rootNodeIndex : scene.nodes) {
                traverseNode(loadedGltf, rootNodeIndex, glm::dmat4(1.0), 
                    potentialInstanceGroups, result.nonInstancedMeshes, meshToSignatureCache, initialParentChain);
            }
        }

        for (auto const& [signature, group] : potentialInstanceGroups) {
            bool isTargetGroup = false;
            if (!group.instances.empty()){
                if(GltfInstancing::TARGET_MESH_NAMES.count(group.representativeMeshName)){
                    isTargetGroup = true;
                    logMessage("DEBUG_SIGNATURE: Evaluating potential group for TARGET mesh name: " + group.representativeMeshName +
                               " with signature: " + std::to_string(signature) + 
                               " and " + std::to_string(group.instances.size()) + " potential instances. Limit: " + std::to_string(_instanceLimit));
                }
            }

            // Apply instanceLimit logic HERE
            if (group.instances.size() >= static_cast<size_t>(_instanceLimit)) {
                InstancedMeshGroup finalGroup = group;
                if (modelIdToRepresentativeModelId.count(group.representativeGltfModelIndex)) {
                    int representativeModelId = modelIdToRepresentativeModelId.at(group.representativeGltfModelIndex);
                    finalGroup.representativeGltfModelIndex = representativeModelId;
                } else {
                    logError("Error: representativeGltfModelIndex " + std::to_string(group.representativeGltfModelIndex) + " not found in modelIdToRepresentativeModelId map.");
                }

                for (auto& instance : finalGroup.instances) {
                    if (modelIdToRepresentativeModelId.count(instance.originalGltfIndex)) {
                        instance.originalGltfIndex = modelIdToRepresentativeModelId.at(instance.originalGltfIndex);
                    } else {
                         logError("Error: instance.originalGltfIndex " + std::to_string(instance.originalGltfIndex) + " not found in map during group finalization.");
                    }
                }
                result.instancedGroups.push_back(finalGroup);
                
                if (isTargetGroup || GltfInstancing::TARGET_MESH_NAMES.count(group.representativeMeshName)) { 
                     logMessage("  DEBUG_SIGNATURE: Instanced group FORMED for signature " + std::to_string(signature) +
                        " (Mesh Name: " + group.representativeMeshName + ")" + 
                        " with " + std::to_string(finalGroup.instances.size()) + " instances (limit was " + std::to_string(_instanceLimit) + "). " +
                        "Representative: Model ID " + std::to_string(finalGroup.representativeGltfModelIndex) +
                        ", Mesh Index " + std::to_string(finalGroup.representativeMeshIndexInModel));
                }
            } else if (!group.instances.empty()) { 
                // Not enough instances to form a group, move all to non-instanced
                if (isTargetGroup || GltfInstancing::TARGET_MESH_NAMES.count(group.representativeMeshName)) {
                    logMessage("  DEBUG_SIGNATURE: Mesh group for " + group.representativeMeshName + " (Sig: " + std::to_string(signature) +
                               ") has " + std::to_string(group.instances.size()) + " instances, which is LESS than limit " + std::to_string(_instanceLimit) + ". Moving to non-instanced.");
                }
                for (const auto& instanceData : group.instances) {
                    NonInstancedMeshInfo niInfo;
                    if (modelIdToRepresentativeModelId.count(instanceData.originalGltfIndex)) {
                        niInfo.originalGltfModelIndex = modelIdToRepresentativeModelId.at(instanceData.originalGltfIndex);
                    } else {
                        logError("Error: instanceData.originalGltfIndex " + std::to_string(instanceData.originalGltfIndex) + " not found in map for non-instanced.");
                        niInfo.originalGltfModelIndex = instanceData.originalGltfIndex; // Fallback
                    }
                    // The representativeMeshIndexInModel from the group is the correct mesh index for these non-instanced items
                    niInfo.originalMeshIndexInModel = instanceData.originalMeshIndex; 
                    niInfo.originalNodeIndexInModel = instanceData.originalNodeIndex; 
                    niInfo.transform = instanceData.transform;
                    result.nonInstancedMeshes.push_back(niInfo);
                    if (isTargetGroup || GltfInstancing::TARGET_MESH_NAMES.count(group.representativeMeshName)) { 
                        logMessage("    DEBUG_SIGNATURE: Moved instance (Orig Node: " + std::to_string(niInfo.originalNodeIndexInModel) + 
                                   ", Orig Model ID: " + std::to_string(instanceData.originalGltfIndex) + ") of mesh " + group.representativeMeshName +
                                   " to non-instanced list.");
                    }
                }
            }
        }

        logMessage("Instancing detection complete. Found " + std::to_string(result.instancedGroups.size()) + " instanced groups (limit: " + std::to_string(_instanceLimit) + ") and " +
            std::to_string(result.nonInstancedMeshes.size()) + " non-instanced meshes.");
        return result;
    }

} // namespace GltfInstancing