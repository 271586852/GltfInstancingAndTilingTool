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
    bool allowNonUniformScaleInstancing = false;

    // --- Semantic + Hausdorff Instancing Mode (Alternative to legacy hash-based) ---
    // instancing_detection_mode: "legacy" (hash+bbox) or "semantic_hausdorff" (semantic hash + Hausdorff similarity)
    std::string instancingDetectionMode = "legacy";
    // semantic_hash_fields: comma-separated, e.g. "category,family,type" (maps to Element_Category, Element_Family, Element_Type)
    std::string semanticHashFields = "category,family,type";
    // similarity_thresholds: per-LOD thresholds, comma-separated, e.g. "0.95,0.90,0.85,0.80,0.75" (LOD0=finest to LOD4=coarsest)
    std::string similarityThresholds = "0.95,0.90,0.85,0.80,0.75";
    std::vector<double> similarityThresholdsParsed;  // Parsed from similarityThresholds
    double hlodSimilarityThreshold = 0.70;
    // Limit point cloud size for Hausdorff computation (0 disables sampling).
    size_t hausdorffMaxSamplePoints = 2000;
    // If false, semantic key "unknown" will NOT do cross-mesh clustering.
    bool allowUnknownCrossMeshClustering = false;

    // --- Instance LOD (Instancing LOD) Configuration ---
    bool enableInstanceLodGeneration = false;
    int lodLevelCount = 5;
    double targetScreenSSE = 16.0;
    bool enableSemanticCheck = true;
    bool enableGeometricCheck = true;
    double lod4SizeTolerance = 0.05;
    double lod3AspectRatioTolerance = 0.20;
    std::string semanticDataPath;  // 单文件路径或文件夹；若为文件夹，按 input_directory 下 GLB 文件名匹配同名 .RISCRVT
    std::string semanticInputDirectory;  // 可选：当 semantic_data_path 为文件夹时，用于匹配的 GLB 来源目录（默认用 input_directory）
    
    // --- HLOD Instancing Detection Parameters (Independent from Stage 1) ---
    // These parameters are used for instancing detection in HLOD/LOD generation pipelines
    // If not set, they will default to Stage 1 parameters
    double hlodGeometryTolerance = -1.0; // -1.0 means "use Stage 1 value"
    double hlodNormalTolerance = -1.0;   // -1.0 means "use Stage 1 value"
    std::set<std::string> hlodAttributesToSkipDataHash;
    int hlodInstanceLimit = -1;          // -1 means "use Stage 1 value"
    bool hlodAllowNonUniformScaleInstancing = false;
    bool hlodGeometryToleranceSet = false;
    bool hlodNormalToleranceSet = false;
    bool hlodAttributesToSkipDataHashSet = false;
    bool hlodInstanceLimitSet = false;

    // --- Non-Instance LOD Configuration ---
    bool enableNonInstancedLodGeneration = false;
    int nonInstancedLodLevelCount = 3;
    double nonInstancedLodRatio = 0.5;
    size_t nonInstancedMinSimplifyIndexCount = 300;
    bool enableNonInstancedLodInstancing = false;

    // --- Quadtree Pipeline Configuration ---
    bool enableQuadtree = false;
    int quadtreeMaxDepth = 6;
    int quadtreeMaxObjectsPerTile = 50;

    // --- Experiment Mode Configuration ---
    bool enableExperimentMode = false;
    bool useSymbolicLinks = false;  // 如果为true，使用符号链接节省空间；如果为false，复制文件
    std::string experiment1Name = "experiment1_baseline_comparison";
    std::string experiment2Name = "experiment2_lod_comparison";
    std::string experiment3Name = "experiment3_mixed_hlod";

    // --- Experiment 6: Cross-GLB HLOD Configuration ---
    bool runCrossGlbHLODExperiment = false;     // 是否运行实验6
    std::string experimentDatasetName = "";      // 当前数据集名称
    std::string experimentStrategyId = "";       // 当前策略ID

    // Default transform to place the model on Earth (User provided)
    std::vector<double> rootTransform = {
        -0.9023136427, 0.4310860309, 0.0, 0.0,
        0.3731804153, 0.7899661139, 0.4899996041, 0.0,
        0.2117562093, 0.4431713488, -0.8716388481, 0.0,
        -2418525.0442296155, 5374967.3619212005, 2429440.091217066, 1.0
    };

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

