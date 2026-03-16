#include "experiment_framework.h"
#include "utilities.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <numeric>
#include <sstream>
#include <iomanip>

namespace ExperimentFramework {

// ==================== ExperimentDirectoryManager ====================

ExperimentDirectoryManager::ExperimentDirectoryManager(const std::filesystem::path& baseExperimentsDir)
    : baseDir_(baseExperimentsDir) {
    experimentNames_[ExperimentType::INSTANCING_STRATEGY] = "01_InstancingStrategy";
    experimentNames_[ExperimentType::LOD_STRATEGY] = "02_LODStrategy";
    experimentNames_[ExperimentType::HLOD_PARAMS] = "03_HLODParams";
    experimentNames_[ExperimentType::CROSS_GLB_HLOD] = "04_CrossGLBHLOD";
}

std::filesystem::path ExperimentDirectoryManager::createExperimentStructure(
    ExperimentType type,
    const std::string& datasetName,
    const StrategyInfo& strategy) {

    std::string expName = experimentNames_[type];
    std::filesystem::path expDir = baseDir_ / expName;

    // 创建数据集目录
    std::string sanitizedDataset = "dataset_" + sanitizePathName(datasetName).string();
    std::filesystem::path datasetDir = expDir / sanitizedDataset;

    // 创建策略目录
    std::filesystem::path strategyDir = datasetDir / strategy.id;
    std::filesystem::create_directories(strategyDir);

    // 创建标准子目录
    std::filesystem::create_directories(strategyDir / "screenshots");

    // 创建对比目录
    std::filesystem::create_directories(datasetDir / "comparison" / "charts");

    // 创建汇总目录
    std::filesystem::create_directories(expDir / "summary" / "charts");

    return strategyDir;
}

std::filesystem::path ExperimentDirectoryManager::getComparisonDir(ExperimentType type, const std::string& datasetName) {
    std::string expName = experimentNames_[type];
    std::string sanitizedDataset = "dataset_" + sanitizePathName(datasetName).string();
    return baseDir_ / expName / sanitizedDataset / "comparison";
}

std::filesystem::path ExperimentDirectoryManager::getSummaryDir(ExperimentType type) {
    return baseDir_ / experimentNames_[type] / "summary";
}

std::filesystem::path ExperimentDirectoryManager::getGlobalSummaryDir() {
    return baseDir_ / "_summary";
}

// ==================== CsvReportGenerator ====================

CsvReportGenerator::CsvReportGenerator(const std::filesystem::path& outputPath)
    : outputPath_(outputPath) {}

void CsvReportGenerator::addMetric(const std::string& metric, double value, const std::string& unit) {
    metrics_.push_back({metric, value, unit, ""});
}

void CsvReportGenerator::addMetric(const MetricValue& metric) {
    metrics_.push_back(metric);
}

void CsvReportGenerator::addLODLevel(const std::string& level, const std::map<std::string, double>& metrics) {
    lodLevels_.push_back(level);
    for (const auto& [key, value] : metrics) {
        lodMetrics_[key].push_back(value);
    }
}

bool CsvReportGenerator::save() {
    std::ofstream file(outputPath_);
    if (!file.is_open()) return false;

    // 写入表头
    file << "Metric,Value,Unit\n";

    // 写入指标
    for (const auto& m : metrics_) {
        file << m.name << "," << std::fixed << std::setprecision(4) << m.value;
        if (!m.unit.empty()) {
            file << "," << m.unit;
        } else {
            file << ",";
        }
        file << "\n";
    }

    file.close();
    return true;
}

bool CsvReportGenerator::writeInstancingAnalysis(
    const std::filesystem::path& outputPath,
    const std::map<std::string, MetricValue>& metrics) {

    std::ofstream file(outputPath);
    if (!file.is_open()) return false;

    file << "Metric,Value,Unit,Description\n";

    // 按预定顺序写入关键指标（实例化检测核心指标优先）
    std::vector<std::string> keyOrder = {
        "SO", "SC", "CR",
        "Eo", "Ec", "ECR",
        "EIc", "IR", "Ic", "PIC",
        "Input Models", "Initial Nodes", "Initial Meshes", "Initial Instances",
        "Instanced Groups", "Final Instances", "Non-instanced Meshes",
        "Final Nodes", "Final Meshes", "Total Displayed Meshes",
        "Node Reduction (%)", "Initial Instancing Ratio (%)", "Final Instancing Ratio (%)", "Instancing Increase (%)",
        "File Size Input (MB)", "File Size Output (MB)", "File Size Reduction (%)",
        "Processing Time (s)"
    };

    for (const auto& key : keyOrder) {
        auto it = metrics.find(key);
        if (it != metrics.end()) {
            file << it->second.name << ","
                 << std::fixed << std::setprecision(4) << it->second.value << ","
                 << it->second.unit << ","
                 << (it->second.description.empty() ? "" : it->second.description) << "\n";
        }
    }

    file.close();
    return true;
}

bool CsvReportGenerator::writeLODAnalysis(
    const std::filesystem::path& outputPath,
    const std::vector<std::string>& lodLevels,
    const std::map<std::string, std::vector<double>>& metrics) {

    std::ofstream file(outputPath);
    if (!file.is_open()) return false;

    // 写入表头
    file << "LOD Level";
    for (const auto& level : lodLevels) {
        file << "," << level;
    }
    file << "\n";

    // 写入每行指标
    for (const auto& [metricName, values] : metrics) {
        file << metricName;
        for (size_t i = 0; i < values.size() && i < lodLevels.size(); ++i) {
            file << "," << std::fixed << std::setprecision(4) << values[i];
        }
        file << "\n";
    }

    file.close();
    return true;
}

bool CsvReportGenerator::writeHLODAnalysis(
    const std::filesystem::path& outputPath,
    const std::vector<std::map<std::string, std::string>>& tileData) {

    std::ofstream file(outputPath);
    if (!file.is_open() || tileData.empty()) return false;

    // 获取所有列名（从第一行）
    std::vector<std::string> columns;
    for (const auto& [key, _] : tileData[0]) {
        columns.push_back(key);
    }

    // 写入表头
    for (size_t i = 0; i < columns.size(); ++i) {
        if (i > 0) file << ",";
        file << columns[i];
    }
    file << "\n";

    // 写入数据行
    for (const auto& row : tileData) {
        for (size_t i = 0; i < columns.size(); ++i) {
            if (i > 0) file << ",";
            auto it = row.find(columns[i]);
            if (it != row.end()) {
                file << it->second;
            }
        }
        file << "\n";
    }

    file.close();
    return true;
}

bool CsvReportGenerator::writeCrossGlbHLODComparison(
    const std::filesystem::path& outputPath,
    const std::map<std::string, std::map<std::string, double>>& comparisonData) {

    std::ofstream file(outputPath);
    if (!file.is_open() || comparisonData.empty()) return false;

    // 获取所有指标名称
    std::vector<std::string> metrics;
    for (const auto& [_, data] : comparisonData) {
        for (const auto& [metric, _] : data) {
            if (std::find(metrics.begin(), metrics.end(), metric) == metrics.end()) {
                metrics.push_back(metric);
            }
        }
        break; // 只需要从第一个策略获取指标列表
    }

    // 写入表头
    file << "Metric";
    for (const auto& [strategy, _] : comparisonData) {
        file << "," << strategy;
    }
    file << ",Advantage\n";

    // 写入每行指标
    for (const auto& metric : metrics) {
        file << metric;
        double mergedValue = 0, separateValue = 0;
        for (const auto& [strategy, data] : comparisonData) {
            auto it = data.find(metric);
            if (it != data.end()) {
                file << "," << std::fixed << std::setprecision(4) << it->second;
                if (strategy.find("Merged") != std::string::npos) mergedValue = it->second;
                if (strategy.find("Separate") != std::string::npos) separateValue = it->second;
            } else {
                file << ",";
            }
        }
        // 标记优势方
        if (mergedValue < separateValue) {
            file << ",Merged";
        } else if (separateValue < mergedValue) {
            file << ",Separate";
        } else {
            file << ",Tie";
        }
        file << "\n";
    }

    file.close();
    return true;
}

// ==================== ReadmeGenerator ====================

bool ReadmeGenerator::writeExperimentReadme(
    const std::filesystem::path& outputPath,
    ExperimentType type,
    const std::string& description,
    const std::vector<StrategyInfo>& strategies) {

    std::ofstream file(outputPath);
    if (!file.is_open()) return false;

    file << "# " << experimentTypeToString(type) << "\n\n";
    file << "## 实验目的\n" << description << "\n\n";

    file << "## 实验设计\n\n";
    file << "### 对比策略\n\n";
    file << "| ID | 名称 | 描述 |\n";
    file << "|------|------|------|\n";
    for (const auto& s : strategies) {
        file << "| " << s.id << " | " << s.name << " | " << s.description << " |\n";
    }
    file << "\n";

    file << "## 文件夹结构\n\n";
    file << "```\n";
    file << experimentTypeToString(type) << "/\n";
    file << "├── dataset_*/              # 各数据集结果\n";
    file << "│   ├── [策略ID]/           # 具体策略输出\n";
    file << "│   │   ├── config.json     # 配置文件\n";
    file << "│   │   ├── *.glb           # 输出模型\n";
    file << "│   │   ├── *_analysis.csv  # 指标数据\n";
    file << "│   │   └── screenshots/    # 渲染截图\n";
    file << "│   └── comparison/         # 对比分析\n";
    file << "└── summary/                # 实验汇总\n";
    file << "```\n\n";

    file << "## 关键发现\n\n";
    file << "（运行后填写）\n\n";

    file << "---\n\n";
    file << "*Generated: " << getCurrentTimestamp() << "*\n";

    file.close();
    return true;
}

bool ReadmeGenerator::writeDatasetReadme(
    const std::filesystem::path& outputPath,
    const DatasetInfo& dataset,
    const std::vector<StrategyInfo>& strategies) {

    std::ofstream file(outputPath);
    if (!file.is_open()) return false;

    file << "# " << dataset.name << "\n\n";
    file << "## 数据描述\n";
    file << "- **来源**: " << dataset.description << "\n";
    file << "- **构件数量**: " << dataset.meshCount << "\n";
    file << "- **GLB文件数**: " << dataset.fileCount << "\n";
    file << "- **输入路径**: " << dataset.path << "\n\n";

    file << "## 实验配置\n\n";
    file << "| 策略ID | 名称 | 关键参数 |\n";
    file << "|--------|------|----------|\n";
    for (const auto& s : strategies) {
        file << "| " << s.id << " | " << s.name << " | ";
        // 输出前3个参数
        int count = 0;
        for (const auto& [k, v] : s.parameters) {
            if (count++ > 0) file << ", ";
            file << k << "=" << v;
            if (count >= 3) break;
        }
        file << " |\n";
    }
    file << "\n";

    file << "## 结果摘要\n\n";
    file << "（运行后填写）\n\n";

    file << "## 输出文件说明\n\n";
    for (const auto& s : strategies) {
        file << "- `" << s.id << "/`: " << s.name << " 策略输出\n";
    }
    file << "- `comparison/`: 策略对比分析\n";

    file.close();
    return true;
}

bool ReadmeGenerator::writeStrategyReadme(
    const std::filesystem::path& outputPath,
    const StrategyInfo& strategy,
    const std::map<std::string, MetricValue>& keyMetrics) {

    std::ofstream file(outputPath);
    if (!file.is_open()) return false;

    file << "# " << strategy.name << "\n\n";
    file << "## 策略描述\n" << strategy.description << "\n\n";

    file << "## 参数配置\n\n";
    file << "| 参数 | 值 |\n";
    file << "|------|-----|\n";
    for (const auto& [k, v] : strategy.parameters) {
        file << "| " << k << " | " << v << " |\n";
    }
    file << "\n";

    file << "## 关键指标\n\n";
    file << "| 指标 | 值 | 单位 |\n";
    file << "|------|------|------|\n";
    for (const auto& [_, m] : keyMetrics) {
        file << "| " << m.name << " | " << std::fixed << std::setprecision(4) << m.value << " | " << m.unit << " |\n";
    }
    file << "\n";

    file.close();
    return true;
}

// ==================== ConfigGenerator ====================

bool ConfigGenerator::writeConfigJson(
    const std::filesystem::path& outputPath,
    const ToolConfiguration& config,
    const StrategyInfo& strategy) {

    nlohmann::json j;

    // 基础配置
    j["input_directory"] = config.inputDirectory;
    j["output_directory"] = config.outputDirectory;
    j["timestamp"] = getCurrentTimestamp();

    // 策略信息
    j["strategy"]["id"] = strategy.id;
    j["strategy"]["name"] = strategy.name;
    j["strategy"]["description"] = strategy.description;
    j["strategy"]["parameters"] = strategy.parameters;

    // 关键参数
    j["parameters"]["similarity_thresholds"] = config.similarityThresholds;
    j["parameters"]["instance_limit"] = config.instanceLimit;
    j["parameters"]["allow_non_uniform_scale"] = config.allowNonUniformScaleInstancing;
    j["parameters"]["lod_ratio"] = config.nonInstancedLodRatio;
    j["parameters"]["quadtree_max_depth"] = config.quadtreeMaxDepth;
    j["parameters"]["quadtree_max_objects"] = config.quadtreeMaxObjectsPerTile;

    std::ofstream file(outputPath);
    if (!file.is_open()) return false;
    file << j.dump(4, ' ', false, nlohmann::json::error_handler_t::replace);
    file.close();
    return true;
}

// ==================== CrossGlbHLODExperiment ====================

std::filesystem::path CrossGlbHLODExperiment::setupMergedHLODOutput(
    const std::filesystem::path& experimentDir,
    const std::string& datasetName,
    const std::vector<std::string>& inputGlbs) {

    std::filesystem::path outputDir = experimentDir / "A_MergedHLOD";
    std::filesystem::create_directories(outputDir);

    // 记录输入文件列表
    std::ofstream listFile(outputDir / "input_files.txt");
    for (const auto& f : inputGlbs) {
        listFile << f << "\n";
    }
    listFile.close();

    return outputDir;
}

std::filesystem::path CrossGlbHLODExperiment::setupSeparateHLODOutput(
    const std::filesystem::path& experimentDir,
    const std::string& datasetName,
    const std::vector<std::string>& inputGlbs) {

    std::filesystem::path outputDir = experimentDir / "B_SeparateHLOD";

    // 为每个GLB创建子目录
    for (const auto& glb : inputGlbs) {
        std::filesystem::path glbPath(glb);
        std::string subdirName = glbPath.stem().string();
        std::filesystem::create_directories(outputDir / subdirName);
    }

    // 创建聚合tileset（可选）
    std::ofstream listFile(outputDir / "input_files.txt");
    for (const auto& f : inputGlbs) {
        listFile << f << "\n";
    }
    listFile.close();

    return outputDir;
}

bool CrossGlbHLODExperiment::generateComparisonReport(
    const std::filesystem::path& outputDir,
    const std::string& datasetName,
    const CrossGlbMetrics& mergedMetrics,
    const CrossGlbMetrics& separateMetrics,
    const CrossGlbMetrics* separateSingleEntryMetrics) {

    // 生成空间结构对比CSV
    std::map<std::string, std::map<std::string, double>> spatialData;
    spatialData["Merged"]["Total Tiles"] = mergedMetrics.totalTiles;
    spatialData["Merged"]["Max Depth"] = mergedMetrics.maxDepth;
    spatialData["Merged"]["Depth Variance"] = mergedMetrics.depthVariance;
    spatialData["Merged"]["Overlapping Tiles"] = mergedMetrics.overlappingTiles;
    spatialData["Merged"]["AABB Utilization (%)"] = mergedMetrics.aabbUtilization * 100;

    spatialData["Separate"]["Total Tiles"] = separateMetrics.totalTiles;
    spatialData["Separate"]["Max Depth"] = separateMetrics.maxDepth;
    spatialData["Separate"]["Depth Variance"] = separateMetrics.depthVariance;
    spatialData["Separate"]["Overlapping Tiles"] = separateMetrics.overlappingTiles;
    spatialData["Separate"]["AABB Utilization (%)"] = separateMetrics.aabbUtilization * 100;
    if (separateSingleEntryMetrics != nullptr) {
        spatialData["SeparateSingleEntry"]["Total Tiles"] = separateSingleEntryMetrics->totalTiles;
        spatialData["SeparateSingleEntry"]["Max Depth"] = separateSingleEntryMetrics->maxDepth;
        spatialData["SeparateSingleEntry"]["Depth Variance"] = separateSingleEntryMetrics->depthVariance;
        spatialData["SeparateSingleEntry"]["Overlapping Tiles"] = separateSingleEntryMetrics->overlappingTiles;
        spatialData["SeparateSingleEntry"]["AABB Utilization (%)"] = separateSingleEntryMetrics->aabbUtilization * 100;
    }

    CsvReportGenerator::writeCrossGlbHLODComparison(
        outputDir / "spatial_analysis.csv", spatialData);

    // 生成性能对比CSV
    std::map<std::string, std::map<std::string, double>> perfData;
    perfData["Merged"]["Tileset Size (KB)"] = mergedMetrics.tilesetSizeKB;
    perfData["Merged"]["Initial Requests"] = mergedMetrics.initialRequests;
    perfData["Merged"]["First Tile Load (ms)"] = mergedMetrics.firstTileLoadTime;
    perfData["Merged"]["Memory Peak (MB)"] = mergedMetrics.memoryPeakMB;
    perfData["Merged"]["Draw Calls"] = mergedMetrics.drawCalls;

    perfData["Separate"]["Tileset Size (KB)"] = separateMetrics.tilesetSizeKB;
    perfData["Separate"]["Initial Requests"] = separateMetrics.initialRequests;
    perfData["Separate"]["First Tile Load (ms)"] = separateMetrics.firstTileLoadTime;
    perfData["Separate"]["Memory Peak (MB)"] = separateMetrics.memoryPeakMB;
    perfData["Separate"]["Draw Calls"] = separateMetrics.drawCalls;
    if (separateSingleEntryMetrics != nullptr) {
        perfData["SeparateSingleEntry"]["Tileset Size (KB)"] = separateSingleEntryMetrics->tilesetSizeKB;
        perfData["SeparateSingleEntry"]["Initial Requests"] = separateSingleEntryMetrics->initialRequests;
        perfData["SeparateSingleEntry"]["First Tile Load (ms)"] = separateSingleEntryMetrics->firstTileLoadTime;
        perfData["SeparateSingleEntry"]["Memory Peak (MB)"] = separateSingleEntryMetrics->memoryPeakMB;
        perfData["SeparateSingleEntry"]["Draw Calls"] = separateSingleEntryMetrics->drawCalls;
    }

    CsvReportGenerator::writeCrossGlbHLODComparison(
        outputDir / "performance_metrics.csv", perfData);

    // 生成文字报告
    std::ofstream report(outputDir / "comparison_report.txt");
    report << "========================================\n";
    report << "跨GLB HLOD构建策略对比报告\n";
    report << "数据集: " << datasetName << "\n";
    report << "生成时间: " << getCurrentTimestamp() << "\n";
    report << "========================================\n\n";

    report << "【空间结构对比】\n";
    report << "Merged总瓦片数: " << mergedMetrics.totalTiles << "\n";
    report << "Separate总瓦片数: " << separateMetrics.totalTiles << "\n";
    if (separateSingleEntryMetrics != nullptr) {
        report << "SeparateSingleEntry总瓦片数: " << separateSingleEntryMetrics->totalTiles << "\n";
    }
    report << "瓦片重叠: Merged=" << mergedMetrics.overlappingTiles
           << ", Separate=" << separateMetrics.overlappingTiles;
    if (separateSingleEntryMetrics != nullptr) {
        report << ", SeparateSingleEntry=" << separateSingleEntryMetrics->overlappingTiles;
    }
    report << "\n\n";

    report << "【加载性能对比】\n";
    report << "初始请求数: Merged=" << mergedMetrics.initialRequests
           << ", Separate=" << separateMetrics.initialRequests;
    if (separateSingleEntryMetrics != nullptr) {
        report << ", SeparateSingleEntry=" << separateSingleEntryMetrics->initialRequests;
    }
    report << "\n";
    report << "首瓦片加载: Merged=" << mergedMetrics.firstTileLoadTime << "ms"
           << ", Separate=" << separateMetrics.firstTileLoadTime << "ms";
    if (separateSingleEntryMetrics != nullptr) {
        report << ", SeparateSingleEntry=" << separateSingleEntryMetrics->firstTileLoadTime << "ms";
    }
    report << "\n\n";

    report << "【推荐策略】\n";
    bool mergedBeatsSeparate = mergedMetrics.initialRequests < separateMetrics.initialRequests &&
        mergedMetrics.overlappingTiles <= separateMetrics.overlappingTiles;
    bool mergedBeatsSingleEntry = true;
    if (separateSingleEntryMetrics != nullptr) {
        mergedBeatsSingleEntry = mergedMetrics.initialRequests < separateSingleEntryMetrics->initialRequests &&
            mergedMetrics.overlappingTiles <= separateSingleEntryMetrics->overlappingTiles;
    }

    if (mergedBeatsSeparate && mergedBeatsSingleEntry) {
        report << "推荐使用: Merged HLOD\n";
        report << "原因: 更少的网络请求，更均衡的空间划分\n";
    } else {
        if (separateSingleEntryMetrics != nullptr &&
            separateSingleEntryMetrics->initialRequests <= separateMetrics.initialRequests) {
            report << "推荐使用: SeparateSingleEntry HLOD\n";
            report << "原因: 保持独立构建优势，同时减少多tileset入口请求开销\n";
        } else {
            report << "推荐使用: Separate HLOD\n";
            report << "原因: 更好的分布式加载性能\n";
        }
    }

    report.close();
    return true;
}

double CrossGlbHLODExperiment::calculateAABBOverlap(
    const std::vector<std::filesystem::path>& glbFiles) {

    if (glbFiles.size() < 2) return 0.0;

    // 简化计算：基于文件名估计重叠度
    // 实际实现应该加载GLB文件并计算AABB重叠
    int overlappingPairs = 0;
    int totalPairs = 0;

    for (size_t i = 0; i < glbFiles.size(); ++i) {
        for (size_t j = i + 1; j < glbFiles.size(); ++j) {
            std::string name1 = glbFiles[i].stem().string();
            std::string name2 = glbFiles[j].stem().string();

            // 如果文件名包含楼层信息，认为它们重叠
            bool isFloor1 = name1.find("Floor") != std::string::npos ||
                           name1.find("楼层") != std::string::npos ||
                           name1.find("F_") != std::string::npos;
            bool isFloor2 = name2.find("Floor") != std::string::npos ||
                           name2.find("楼层") != std::string::npos ||
                           name2.find("F_") != std::string::npos;

            if (isFloor1 && isFloor2) {
                overlappingPairs++;
            }
            totalPairs++;
        }
    }

    return totalPairs > 0 ? static_cast<double>(overlappingPairs) / totalPairs : 0.0;
}

std::string CrossGlbHLODExperiment::generateStrategyRecommendation(
    double overlapRatio,
    const std::vector<std::filesystem::path>& glbFiles) {

    std::ostringstream recommendation;

    recommendation << "基于AABB重叠度分析 (" << std::fixed << std::setprecision(1)
                  << overlapRatio * 100 << "%)，推荐策略如下：\n\n";

    if (overlapRatio > 0.8) {
        recommendation << "### 推荐：Merged HLOD (合并构建)\n\n";
        recommendation << "**原因：**\n";
        recommendation << "1. GLB文件空间重叠度高 (>80%)，合并构建可消除瓦片重叠\n";
        recommendation << "2. 统一的四叉树空间划分保证LOD切换一致性\n";
        recommendation << "3. 减少网络请求数（单个tileset.json）\n\n";
        recommendation << "**适用场景：** 多楼层建筑、同一区域多专业模型\n";
    } else if (overlapRatio < 0.3) {
        recommendation << "### 推荐：Separate HLOD (独立构建)\n\n";
        recommendation << "**原因：**\n";
        recommendation << "1. GLB文件空间分布分散 (<30%重叠)，独立构建更灵活\n";
        recommendation << "2. 按需加载单个GLB的HLOD，减少初始加载负担\n";
        recommendation << "3. 各模型可独立更新，维护性好\n\n";
        recommendation << "**适用场景：** 远距离分散的建筑群、独立地块\n";
    } else {
        recommendation << "### 推荐：进一步分析或Hybrid策略\n\n";
        recommendation << "**原因：**\n";
        recommendation << "1. AABB重叠度中等 (30%-80%)，需要更详细的空间分析\n";
        recommendation << "2. 建议考虑Hybrid策略：邻近GLB合并为组，组间独立\n\n";
        recommendation << "**建议步骤：**\n";
        recommendation << "1. 计算各GLB对的实际AABB重叠\n";
        recommendation << "2. 对重叠度高的GLB子集使用Merged策略\n";
        recommendation << "3. 对独立分布的GLB使用Separate策略\n";
    }

    recommendation << "\n**性能权衡：**\n";
    recommendation << "- 网络延迟高 → 优先 Merged（减少请求数）\n";
    recommendation << "- 内存受限 → 优先 Separate（按需加载）\n";
    recommendation << "- 超大规模 (>100 GLB) → 推荐 Hybrid 分组策略\n";

    return recommendation.str();
}

// ==================== Helper Functions ====================

std::string experimentTypeToString(ExperimentType type) {
    switch (type) {
        case ExperimentType::INSTANCING_STRATEGY: return "01_InstancingStrategy";
        case ExperimentType::LOD_STRATEGY: return "02_LODStrategy";
        case ExperimentType::HLOD_PARAMS: return "03_HLODParams";
        case ExperimentType::CROSS_GLB_HLOD: return "04_CrossGLBHLOD";
        default: return "Unknown";
    }
}

std::string getCurrentTimestamp() {
    auto now = std::time(nullptr);
    auto localTime = *std::localtime(&now);
    std::ostringstream oss;
    oss << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

std::filesystem::path sanitizePathName(const std::string& name) {
    std::string result;
    for (char c : name) {
        if (std::isalnum(c) || c == '_' || c == '-') {
            result += c;
        } else if (c == ' ' || c == '\t') {
            result += '_';
        }
    }
    return result.empty() ? "unnamed" : result;
}

} // namespace ExperimentFramework
