#ifndef TOOL_CONFIGURATION_H
#define TOOL_CONFIGURATION_H

#include <string>
#include <set>
#include <vector>

// Structure to hold all configuration parameters
struct ToolConfiguration {
    std::string inputDirectory;
    std::string outputDirectory;
    double geometryTolerance = 0.0;
    double normalTolerance = 0.0;
    std::set<std::string> attributesToSkipDataHash;
    bool mergeAllGlb = false;
    int instanceLimit = 2; // Default to 2
    bool meshSegmentation = false;
    std::string csvDirectory;
    bool csvDirectorySet = false;

    // --- LOD Configuration ---
    bool enableLodGeneration = false;
    int lodLevelCount = 5;
    double targetScreenSSE = 16.0;
    bool enableSemanticCheck = true;
    bool enableGeometricCheck = true;
    double lod4SizeTolerance = 0.05;
    double lod3AspectRatioTolerance = 0.20;
    std::string semanticDataPath;

    // --- Non-Instanced LOD Configuration ---
    bool enableNonInstancedLodGeneration = false;
    int nonInstancedLodLevelCount = 3;
    double nonInstancedLodRatio = 0.5;
    size_t nonInstancedMinSimplifyIndexCount = 300;
    bool enableNonInstancedLodInstancing = false;

    // --- Quadtree Pipeline Configuration ---
    bool enableQuadtree = false;
    int quadtreeMaxDepth = 6;
    int quadtreeMaxObjectsPerTile = 50;
    // (Strategies could be loaded from JSON, but we use defaults for now)

    // Flags to track if a parameter was set
    bool inputDirectorySet = false;
    bool outputDirectorySet = false;
    bool geometryToleranceSet = false;
    bool normalToleranceSet = false;
    bool attributesToSkipDataHashSet = false;
    bool mergeAllGlbSet = false;
    bool instanceLimitSet = false;
    bool meshSegmentationSet = false;
    
    // New flags
    bool enableQuadtreeSet = false;
};

#endif // TOOL_CONFIGURATION_H

