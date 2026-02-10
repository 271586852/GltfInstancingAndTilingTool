#include "experiment_utils.h"
#include "utilities.h" // For logging
#include <fstream>
#include <iomanip>
#include <ctime>

namespace ExperimentUtils {

bool createExperimentDirectories(const ToolConfiguration& config) {
    if (!config.enableExperimentMode) {
        GltfInstancing::logDebug("Experiment mode is disabled.");
        return true;
    }

    try {
        // 创建实验1目录结构
        std::filesystem::path exp1Path = getExperiment1Path(config);
        std::filesystem::create_directories(exp1Path / "instanced");
        std::filesystem::create_directories(exp1Path / "non_instanced");
        std::filesystem::create_directories(exp1Path / "analysis");

        // 创建实验2目录结构
        std::filesystem::path exp2Path = getExperiment2Path(config);
        std::filesystem::create_directories(exp2Path / "instanced_lod");
        std::filesystem::create_directories(exp2Path / "non_instanced_lod");
        std::filesystem::create_directories(exp2Path / "comparison");

        // 创建实验3目录结构
        std::filesystem::path exp3Path = getExperiment3Path(config);
        std::filesystem::create_directories(exp3Path / "instanced_lod");
        std::filesystem::create_directories(exp3Path / "non_instanced_hlod");
        std::filesystem::create_directories(exp3Path / "mixed_analysis");

        GltfInstancing::logInfo("Experiment directories created successfully.");
        return true;
    } catch (const std::filesystem::filesystem_error& e) {
        GltfInstancing::logError("Failed to create experiment directories: " + std::string(e.what()));
        return false;
    }
}

bool copyOrLinkFile(const std::filesystem::path& source,
                   const std::filesystem::path& destination,
                   bool useSymbolicLink) {
    try {
        // 确保源文件存在
        if (!std::filesystem::exists(source)) {
            GltfInstancing::logError("Source file does not exist: " + source.string());
            return false;
        }

        // 确保目标目录存在
        std::filesystem::create_directories(destination.parent_path());

        if (useSymbolicLink) {
            // 创建符号链接 - 使用绝对路径避免相对路径问题
            try {
                // 检查目标是否已是符号链接，如果是先删除
                if (std::filesystem::is_symlink(destination)) {
                    std::filesystem::remove(destination);
                }

                // 使用绝对路径创建符号链接
                auto absoluteSource = std::filesystem::absolute(source);
                std::filesystem::create_symlink(absoluteSource, destination);
                GltfInstancing::logDebug("Created symbolic link: " + destination.string() + " -> " + absoluteSource.string());
                return true;
            } catch (const std::filesystem::filesystem_error& e) {
                // 如果符号链接创建失败（权限问题或系统不支持），回退到复制
                GltfInstancing::logWarning("Failed to create symbolic link, falling back to copy: " + std::string(e.what()));
                // 回退到文件复制
                std::filesystem::copy_file(source, destination, std::filesystem::copy_options::overwrite_existing);
                GltfInstancing::logDebug("Copied file (fallback): " + source.string() + " -> " + destination.string());
                return true;
            } catch (const std::exception& e) {
                // 处理其他异常
                GltfInstancing::logError("Failed to create symbolic link: " + std::string(e.what()));
                return false;
            }
        } else {
            // 复制文件
            std::filesystem::copy_file(source, destination, std::filesystem::copy_options::overwrite_existing);
            GltfInstancing::logDebug("Copied file: " + source.string() + " -> " + destination.string());
            return true;
        }
    } catch (const std::filesystem::filesystem_error& e) {
        GltfInstancing::logError("Failed to copy/link file: " + source.string() + " -> " + destination.string() +
                                ". Error: " + std::string(e.what()));
        return false;
    } catch (const std::exception& e) {
        GltfInstancing::logError("Unexpected error when copying/linking file: " + std::string(e.what()));
        return false;
    }
}

bool copyDirectoryContents(const std::filesystem::path& source,
                          const std::filesystem::path& destination,
                          bool useSymbolicLink) {
    try {
        if (!std::filesystem::exists(source)) {
            GltfInstancing::logWarning("Source directory does not exist: " + source.string());
            return false;
        }

        std::filesystem::create_directories(destination);

        for (const auto& entry : std::filesystem::directory_iterator(source)) {
            const auto& srcPath = entry.path();
            auto dstPath = destination / srcPath.filename();

            if (entry.is_directory()) {
                // 递归复制子目录
                if (!copyDirectoryContents(srcPath, dstPath, useSymbolicLink)) {
                    return false;
                }
            } else if (entry.is_regular_file()) {
                // 复制文件
                if (!copyOrLinkFile(srcPath, dstPath, useSymbolicLink)) {
                    return false;
                }
            }
        }
        return true;
    } catch (const std::filesystem::filesystem_error& e) {
        GltfInstancing::logError("Failed to copy directory contents: " + std::string(e.what()));
        return false;
    }
}

void writeExperimentComparisonReport(const ToolConfiguration& config,
                                    const std::string& experimentName,
                                    const std::vector<std::pair<std::string, std::vector<std::pair<std::string, std::string>>>>& reportData) {
    if (!config.enableExperimentMode) {
        return;
    }

    std::filesystem::path reportPath;

    // 根据实验名称确定报告路径
    if (experimentName == "experiment1") {
        reportPath = getExperiment1Path(config) / "analysis" / "comparison_report.txt";
    } else if (experimentName == "experiment2") {
        reportPath = getExperiment2Path(config) / "comparison" / "lod_comparison_report.txt";
    } else if (experimentName == "experiment3") {
        reportPath = getExperiment3Path(config) / "mixed_analysis" / "strategy_comparison.txt";
    } else {
        GltfInstancing::logError("Unknown experiment name: " + experimentName);
        return;
    }

    std::ofstream reportFile(reportPath);
    if (!reportFile.is_open()) {
        GltfInstancing::logError("Failed to open report file for writing: " + reportPath.string());
        return;
    }

    // 获取当前时间
    auto now = std::time(nullptr);
    auto localTime = *std::localtime(&now);

    // 写入报告标题
    reportFile << "========================================\n";
    reportFile << experimentName << " - Experiment Report\n";
    reportFile << "Generated: " << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S") << "\n";
    reportFile << "========================================\n\n";

    // 写入报告数据
    for (const auto& [sectionTitle, data] : reportData) {
        reportFile << sectionTitle << ":\n";
        for (const auto& [key, value] : data) {
            reportFile << "  " << key << ": " << value << "\n";
        }
        reportFile << "\n";
    }

    reportFile.close();
    GltfInstancing::logInfo("Experiment comparison report written to: " + reportPath.string());
}

std::filesystem::path getExperiment1Path(const ToolConfiguration& config) {
    return std::filesystem::path(config.outputDirectory) / config.experiment1Name;
}

std::filesystem::path getExperiment2Path(const ToolConfiguration& config) {
    return std::filesystem::path(config.outputDirectory) / config.experiment2Name;
}

std::filesystem::path getExperiment3Path(const ToolConfiguration& config) {
    return std::filesystem::path(config.outputDirectory) / config.experiment3Name;
}

} // namespace ExperimentUtils