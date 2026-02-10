#ifndef EXPERIMENT_UTILS_H
#define EXPERIMENT_UTILS_H

#include <string>
#include <vector>
#include <filesystem>
#include "ToolConfiguration.h"

namespace ExperimentUtils {
    // 创建实验文件夹结构
    bool createExperimentDirectories(const ToolConfiguration& config);

    // 复制或创建符号链接文件
    bool copyOrLinkFile(const std::filesystem::path& source,
                       const std::filesystem::path& destination,
                       bool useSymbolicLink);

    // 复制整个目录的内容
    bool copyDirectoryContents(const std::filesystem::path& source,
                              const std::filesystem::path& destination,
                              bool useSymbolicLink);

    // 生成实验对比报告
    void writeExperimentComparisonReport(const ToolConfiguration& config,
                                        const std::string& experimentName,
                                        const std::vector<std::pair<std::string, std::vector<std::pair<std::string, std::string>>>>& reportData);

    // 获取实验文件夹路径
    std::filesystem::path getExperiment1Path(const ToolConfiguration& config);
    std::filesystem::path getExperiment2Path(const ToolConfiguration& config);
    std::filesystem::path getExperiment3Path(const ToolConfiguration& config);
}

#endif // EXPERIMENT_UTILS_H