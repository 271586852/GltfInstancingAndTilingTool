#ifndef EXPERIMENT_FRAMEWORK_H
#define EXPERIMENT_FRAMEWORK_H

#include <string>
#include <vector>
#include <filesystem>
#include <map>
#include <fstream>
#include <iomanip>
#include <ctime>
#include "ToolConfiguration.h"

namespace ExperimentFramework {

// 实验类型枚举
enum class ExperimentType {
    INSTANCING_STRATEGY,    // 实验1：实例化检测策略
    LOD_STRATEGY,           // 实验2：LOD策略对比
    HLOD_PARAMS,            // 实验3：HLOD参数评估
    END_TO_END,             // 实验4：端到端性能
    NON_UNIFORM_SCALE,      // 实验5：非均匀缩放
    CROSS_GLB_HLOD          // 实验6：跨GLB HLOD构建
};

// 数据集信息
struct DatasetInfo {
    std::string name;           // 数据集名称
    std::string description;    // 描述
    std::string path;           // 输入路径
    size_t meshCount;          // 构件数量
    size_t fileCount;          // GLB文件数量
};

// 策略/配置信息
struct StrategyInfo {
    std::string id;             // 策略ID (如 "01_Strict_0.00m")
    std::string name;           // 策略名称
    std::string description;    // 策略描述
    std::map<std::string, std::string> parameters;  // 参数键值对
};

// 实验运行上下文
struct ExperimentContext {
    ExperimentType type;
    std::string experimentName;     // 如 "01_InstancingStrategy"
    std::string datasetName;        // 如 "dataset_住宅标准层"
    std::string strategyId;         // 如 "01_Strict_0.00m"
    std::filesystem::path baseOutputDir;  // 基础输出目录
};

// 指标数据点
struct MetricValue {
    std::string name;
    double value;
    std::string unit;
    std::string description;
};

// 增强型实验目录管理器
class ExperimentDirectoryManager {
public:
    ExperimentDirectoryManager(const std::filesystem::path& baseExperimentsDir);

    // 创建完整的实验目录结构
    std::filesystem::path createExperimentStructure(
        ExperimentType type,
        const std::string& datasetName,
        const StrategyInfo& strategy
    );

    // 获取实验对比目录
    std::filesystem::path getComparisonDir(ExperimentType type, const std::string& datasetName);

    // 获取实验汇总目录
    std::filesystem::path getSummaryDir(ExperimentType type);

    // 获取总汇总目录
    std::filesystem::path getGlobalSummaryDir();

private:
    std::filesystem::path baseDir_;
    std::map<ExperimentType, std::string> experimentNames_;
};

// CSV报告生成器 - 标准化CSV格式
class CsvReportGenerator {
public:
    CsvReportGenerator(const std::filesystem::path& outputPath);

    // 添加指标
    void addMetric(const std::string& metric, double value, const std::string& unit = "");
    void addMetric(const MetricValue& metric);

    // 添加LOD层级数据
    void addLODLevel(const std::string& level, const std::map<std::string, double>& metrics);

    // 保存到文件
    bool save();

    // 静态方法：生成实例化分析CSV
    static bool writeInstancingAnalysis(
        const std::filesystem::path& outputPath,
        const std::map<std::string, MetricValue>& metrics
    );

    // 静态方法：生成LOD分析CSV
    static bool writeLODAnalysis(
        const std::filesystem::path& outputPath,
        const std::vector<std::string>& lodLevels,
        const std::map<std::string, std::vector<double>>& metrics
    );

    // 静态方法：生成HLOD分析CSV
    static bool writeHLODAnalysis(
        const std::filesystem::path& outputPath,
        const std::vector<std::map<std::string, std::string>>& tileData
    );

    // 静态方法：生成跨GLB HLOD对比CSV
    static bool writeCrossGlbHLODComparison(
        const std::filesystem::path& outputPath,
        const std::map<std::string, std::map<std::string, double>>& comparisonData
    );

private:
    std::filesystem::path outputPath_;
    std::vector<MetricValue> metrics_;
    std::vector<std::string> lodLevels_;
    std::map<std::string, std::vector<double>> lodMetrics_;
};

// README生成器
class ReadmeGenerator {
public:
    // 生成实验级README
    static bool writeExperimentReadme(
        const std::filesystem::path& outputPath,
        ExperimentType type,
        const std::string& description,
        const std::vector<StrategyInfo>& strategies
    );

    // 生成数据集级README
    static bool writeDatasetReadme(
        const std::filesystem::path& outputPath,
        const DatasetInfo& dataset,
        const std::vector<StrategyInfo>& strategies
    );

    // 生成策略级README
    static bool writeStrategyReadme(
        const std::filesystem::path& outputPath,
        const StrategyInfo& strategy,
        const std::map<std::string, MetricValue>& keyMetrics
    );
};

// JSON配置生成器
class ConfigGenerator {
public:
    // 生成完整的配置JSON
    static bool writeConfigJson(
        const std::filesystem::path& outputPath,
        const ToolConfiguration& config,
        const StrategyInfo& strategy
    );
};

// 对比报告生成器
class ComparisonReportGenerator {
public:
    // 生成同一数据集不同策略的对比
    static bool generateStrategyComparison(
        const std::filesystem::path& outputDir,
        const std::string& datasetName,
        const std::vector<std::string>& strategyIds,
        const std::map<std::string, std::map<std::string, double>>& allMetrics
    );

    // 生成跨数据集汇总
    static bool generateCrossDatasetSummary(
        const std::filesystem::path& outputDir,
        const std::vector<std::string>& datasetNames,
        const std::map<std::string, std::map<std::string, double>>& summaryMetrics
    );
};

// 实验6专用：跨GLB HLOD对比工具
class CrossGlbHLODExperiment {
public:
    struct CrossGlbMetrics {
        // 空间结构指标
        int totalTiles;
        int maxDepth;
        double depthVariance;
        int overlappingTiles;
        double aabbUtilization;

        // 文件效率指标
        double tilesetSizeKB;
        double rootGeometricError;
        int duplicateResources;

        // 渲染性能指标 (运行时测量)
        double avgFrustumQueryTiles;
        double lodSwitchConsistency;
        int drawCalls;

        // 加载性能指标 (运行时测量)
        int initialRequests;
        double firstTileLoadTime;
        double memoryPeakMB;
    };

    // 为Merged策略生成输出结构
    static std::filesystem::path setupMergedHLODOutput(
        const std::filesystem::path& experimentDir,
        const std::string& datasetName,
        const std::vector<std::string>& inputGlbs
    );

    // 为Separate策略生成输出结构
    static std::filesystem::path setupSeparateHLODOutput(
        const std::filesystem::path& experimentDir,
        const std::string& datasetName,
        const std::vector<std::string>& inputGlbs
    );

    // 生成跨GLB对比报告
    static bool generateComparisonReport(
        const std::filesystem::path& outputDir,
        const std::string& datasetName,
        const CrossGlbMetrics& mergedMetrics,
        const CrossGlbMetrics& separateMetrics,
        const CrossGlbMetrics* separateSingleEntryMetrics = nullptr
    );

    // 计算空间重叠度（用于决策建议）
    static double calculateAABBOverlap(
        const std::vector<std::filesystem::path>& glbFiles
    );

    // 生成决策建议
    static std::string generateStrategyRecommendation(
        double overlapRatio,
        const std::vector<std::filesystem::path>& glbFiles
    );
};

// 辅助函数
std::string experimentTypeToString(ExperimentType type);
std::string getCurrentTimestamp();
std::filesystem::path sanitizePathName(const std::string& name);

} // namespace ExperimentFramework

#endif // EXPERIMENT_FRAMEWORK_H
