# 实验框架增强功能说明

## 概述

本代码修改添加了**增强型实验框架**，实现了硕士论文对比验证实验的标准化输出结构。

---

## 新增文件

| 文件 | 说明 |
|------|------|
| `src/experiment_framework.h` | 实验框架头文件，定义核心类和数据结构 |
| `src/experiment_framework.cpp` | 实验框架实现，包含目录管理、CSV生成、README生成 |
| `experiment_config_template.txt` | 实验配置文件模板 |
| `experiment_output_design.md` | 输出文件夹结构设计文档 |
| `experiment_design.md` | 完整实验设计方案（含新增实验6） |

---

## 修改的文件

| 文件 | 修改内容 |
|------|----------|
| `src/main.cpp` | 添加新框架引用、增强版CSV输出函数、实验6执行函数 |
| `src/ToolConfiguration.h` | 添加实验6配置参数 |

---

## 核心功能

### 1. 标准化输出文件夹结构

```
experiments/
├── 01_InstancingStrategy/              # 实验1：实例化检测策略
│   ├── dataset_住宅标准层/
│   │   ├── 01_Strict_0.00m/           # 策略1输出
│   │   │   ├── config.json            # 完整配置
│   │   │   ├── instancing_analysis.csv # 标准化CSV
│   │   │   ├── screenshots/            # 渲染截图
│   │   │   └── README.md               # 策略说明
│   │   ├── 02_Moderate_0.05m/         # 策略2输出
│   │   ├── 03_Lenient_0.10m/          # 策略3输出
│   │   └── comparison/                 # 对比分析
│   │       ├── metrics_comparison.csv
│   │       └── charts/
│   └── summary/                        # 实验汇总
│
├── 06_CrossGLBHLOD/                    # 实验6：跨GLB HLOD（新增）
│   └── dataset_多楼层建筑/
│       ├── A_MergedHLOD/              # 策略A：合并构建
│       ├── B_SeparateHLOD/            # 策略B：独立构建
│       └── comparison/
│           ├── spatial_analysis.csv
│           ├── performance_metrics.csv
│           └── strategy_recommendation.md
│
└── _summary/                          # 总体汇总
    └── all_experiments_summary.csv
```

### 2. 标准化CSV格式

**instancing_analysis.csv**
```csv
Metric,Value,Unit
Input Models,1,count
Initial Nodes,372,count
Node Reduction (%),81.18,%
File Size Reduction (%),91.77,%
```

**lod_analysis.csv**
```csv
LOD Level,Original (Input),Instanced (LOD5),LOD4 (Variant),LOD3 (Class),LOD2 (Abstract),LOD1 (Proxy)
File Size (MB),46.78,3.85,3.85,0.12,0.12,0.01
Vertices (Loaded),1717176,141846,141846,3288,3288,24
```

### 3. 实验6：跨GLB HLOD构建策略对比（新增）

对比两种策略：
- **Merged HLOD**: 多个GLB合并构建统一四叉树
- **Separate HLOD**: 每个GLB独立构建HLOD

测量指标：
- 空间结构：总瓦片数、树深度分布、瓦片重叠数
- 文件效率：tileset大小、根节点误差、重复资源
- 渲染性能：视锥裁剪批次、LOD切换一致性
- 加载性能：初始请求数、首瓦片加载时间

---

## 使用方法

### 1. 基础使用（保持向后兼容）

原有功能不受影响，CSV文件仍输出到原有位置。

### 2. 启用新实验框架

在配置文件中添加：
```ini
enable_experiment_mode = true
experiment_dataset_name = 住宅标准层
experiment_strategy_id = 02_Moderate_0.05m
```

### 3. 运行实验6（跨GLB HLOD）

```ini
# 1. 准备包含多个GLB的输入目录
input_directory = D:/Data/多楼层建筑  # 包含 Floor_01.glb, Floor_02.glb...

# 2. 启用实验6
run_cross_glb_hlod_experiment = true
experiment_dataset_name = 多楼层建筑

# 3. 运行工具
# 将自动生成 A_MergedHLOD 和 B_SeparateHLOD 的对比
```

### 4. 命令行参数

```bash
# 基础运行
GltfInstancingTool.exe --config experiment_config.txt

# 启用实验模式
GltfInstancingTool.exe --input_directory D:/Data/BIM --enable-experiment-mode

# 运行实验6
GltfInstancingTool.exe --input_directory D:/Data/多楼层建筑 \
                       --run-cross-glb-hlod-experiment \
                       --experiment-dataset-name 多楼层建筑 \
                       --enable-experiment-mode
```

---

## 配置参数

### 新增配置参数

| 参数 | 说明 | 默认值 |
|------|------|--------|
| `enable_experiment_mode` | 启用实验模式 | false |
| `run_cross_glb_hlod_experiment` | 运行实验6 | false |
| `experiment_dataset_name` | 数据集名称 | "" |
| `experiment_strategy_id` | 策略ID | "" |
| `use_symbolic_links` | 使用符号链接 | false |

### 命令行参数

| 参数 | 说明 |
|------|------|
| `--enable-experiment-mode` | 启用实验模式 |
| `--run-cross-glb-hlod-experiment` | 运行实验6 |
| `--experiment-dataset-name <name>` | 数据集名称 |
| `--experiment-strategy-id <id>` | 策略ID |
| `--use-symbolic-links` | 使用符号链接 |

---

## 类图

```
ExperimentFramework
├── ExperimentDirectoryManager     # 创建和管理实验目录结构
│   ├── createExperimentStructure()
│   ├── getComparisonDir()
│   └── getSummaryDir()
│
├── CsvReportGenerator             # 生成标准化CSV报告
│   ├── writeInstancingAnalysis()
│   ├── writeLODAnalysis()
│   ├── writeHLODAnalysis()
│   └── writeCrossGlbHLODComparison()
│
├── ReadmeGenerator                # 生成README文档
│   ├── writeExperimentReadme()
│   ├── writeDatasetReadme()
│   └── writeStrategyReadme()
│
├── ConfigGenerator                # 生成配置JSON
│   └── writeConfigJson()
│
└── CrossGlbHLODExperiment         # 实验6专用工具
    ├── setupMergedHLODOutput()
    ├── setupSeparateHLODOutput()
    ├── generateComparisonReport()
    ├── calculateAABBOverlap()
    └── generateStrategyRecommendation()
```

---

## 编译说明

新框架依赖现有项目结构，编译时只需将新文件添加到CMakeLists.txt：

```cmake
# 在 add_executable 中添加新源文件
add_executable(GltfInstancingTool
    src/main.cpp
    src/experiment_utils.cpp
    src/experiment_framework.cpp  # 新增
    # ... 其他源文件
)
```

---

## 后续工作

1. **添加Python分析脚本**（可选）
   - `scripts/generate_comparison.py` - 自动生成对比图表
   - `scripts/collect_results.py` - 汇总所有实验结果

2. **增强可视化**（可选）
   - 集成matplotlib生成热力图、雷达图
   - 自动生成桑基图展示数据流

3. **扩展实验类型**（可选）
   - 支持更多对比维度
   - 添加统计显著性检验

---

## 注意事项

1. **向后兼容**: 原有功能不受影响，新功能需显式启用
2. **文件路径**: 自动处理路径中的非法字符
3. **符号链接**: Windows需要管理员权限或启用开发者模式
4. **实验6要求**: 输入目录需包含至少2个GLB文件

---

*最后更新: 2024*
