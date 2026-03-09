# 实验输出文件结构说明

## 概述

工具支持**两种输出模式**：
1. **普通模式**（向后兼容）：输出到指定目录，保持原有结构
2. **实验模式**（新增）：额外生成标准化的实验目录结构，便于对比分析

工具支持**两种输出结构**（`output_structure_mode`）：
- **staged**（默认）：按流水线阶段分层，结构清晰
- **legacy**：原有扁平结构，向后兼容

---

## 一、输出目录配置

### 1.1 配置方式

**方式1：配置文件**
```ini
output_directory = D:/Experiments/Results
```

**方式2：命令行**
```bash
--output_directory D:/Experiments/Results
```

**方式3：默认（不指定）**
```
默认输出目录 = <input_directory>/processed_output
```

### 1.2 输出结构模式

```ini
# staged（默认）：按流水线阶段分层
output_structure_mode = staged

# legacy：原有扁平结构，向后兼容
output_structure_mode = legacy
```

命令行：`--output-structure-mode staged` 或 `--output-structure-mode legacy`

### 1.3 实验模式开关

```ini
# 启用实验模式（生成额外的标准化实验目录）
enable_experiment_mode = true

# 数据集名称（用于组织实验文件夹）
experiment_dataset_name = 住宅标准层

# 策略ID（标识当前运行配置）
experiment_strategy_id = 02_Moderate_0.05m
```

---

## 二、Staged 模式输出结构（默认，推荐）

当 `output_structure_mode = staged` 时，按流水线阶段分层：

```
output_directory/
├── run_manifest.json                  # 运行元数据（时间、配置、启用阶段）
│
├── 01_instancing/                      # Stage 1：实例化检测
│   ├── instanced.glb                  # 实例化网格
│   ├── non_instanced.glb              # 非实例化网格
│   ├── instanced.json                 # 实例化 tileset
│   ├── non_instanced.json             # 非实例化 tileset
│   └── analysis/
│       ├── instancing.csv             # 实例化分析指标
│       ├── instancing.txt             # 详细文字报告
│       ├── per_glb.csv                # 每个 GLB 的统计
│       └── optimization_summary.txt   # 优化汇总
│
├── 02_instance_lod/                   # Stage 2a：实例化 LOD（如启用）
│   ├── LOD1.glb ~ LOD5.glb
│   ├── tileset.json
│   └── analysis/
│       └── instance_lod.csv
│
├── 02_non_instance_lod/               # Stage 2b：非实例化 LOD（如启用）
│   ├── non_instanced_LOD0.glb ~ LODn.glb
│   ├── tileset.json
│   └── analysis/
│       └── non_instance_lod.csv
│
├── 03_hlod/                           # Stage 3：HLOD（如启用）
│   ├── tileset.json
│   ├── hlod_analysis.csv
│   └── tiles/
│       ├── T0_0_0.glb
│       ├── T1_0_0.glb, T1_1_0.glb, ...
│       └── ...
│
├── 04_segmented/                      # Stage 4：分割输出（如启用）
│   └── *.glb
│
└── _analysis/                         # 汇总分析（CSV 处理等）
    └── *_results.csv
```

---

## 三、Legacy 模式输出结构（向后兼容）

当 `output_structure_mode = legacy` 时，保持原有扁平结构：

```
output_directory/
├── instancing_analysis.csv
├── instancing_analysis.txt
├── instanced_meshes.glb
├── non_instanced_meshes.glb
├── tileset_instanced.json
├── tileset_non_instanced.json
├── instancing_per_glb.csv
├── instancing_optimization_summary.txt
├── *_results.csv
│
├── instance_lod_output/
│   ├── instance_lod_analysis.csv
│   ├── LOD1.glb ~ LOD5.glb
│   └── tileset.json
│
├── non_instance_lod_output/
│   ├── non_instance_lod_analysis.csv
│   └── non_instanced_LOD*.glb
│
├── quadtree_output/
│   ├── hlod_analysis.csv
│   ├── tileset.json
│   └── tiles/
│       └── T*.glb
│
└── segmented_glb_output/
    └── *.glb
```

---

## 四、实验模式输出结构（新增）

当 `enable_experiment_mode = true` 时，**保留原有输出**，同时额外生成：

```
output_directory/experiments/         # 实验输出在 output_directory 内
│
├── README.md                          # 所有实验总览
│
├── 01_InstancingStrategy/             # 实验1：实例化检测策略
│   ├── README.md                      # 实验说明
│   ├── config_summary.csv             # 配置汇总
│   │
│   ├── dataset_住宅标准层/             # 数据集1
│   │   ├── README.md                  # 数据集说明
│   │   │
│   │   ├── 01_Strict_0.00m/          # 策略1
│   │   │   ├── config.json           # 完整配置参数
│   │   │   ├── output.glb            # 输出模型（链接或复制）
│   │   │   ├── tileset.json          # 3D Tiles文件
│   │   │   ├── instancing_analysis.csv # 标准化CSV
│   │   │   ├── instancing_report.txt # 详细报告
│   │   │   └── screenshots/          # 渲染截图
│   │   │
│   │   ├── 02_Moderate_0.05m/        # 策略2
│   │   ├── 03_Lenient_0.10m/         # 策略3
│   │   │
│   │   └── comparison/               # 自动生成的对比分析
│   │       ├── comparison_report.txt
│   │       ├── metrics_comparison.csv
│   │       └── charts/
│   │           ├── instancing_rate.png
│   │           ├── file_size.png
│   │           └── compression_ratio.png
│   │
│   ├── dataset_办公楼MEP/              # 数据集2
│   └── summary/                       # 实验1总体汇总
│       ├── cross_dataset_comparison.csv
│       └── conclusion.md
│
├── 02_LODStrategy/                    # 实验2：LOD策略对比
│   └── dataset_*/
│       ├── A_InstancingLOD/           # 实例化LOD
│       ├── B_NonInstancingLOD/        # 非实例化LOD
│       └── comparison/
│
├── 03_HLODParams/                     # 实验3：HLOD参数评估
│   └── dataset_*/
│       ├── 01_Depth4_Obj30/
│       ├── 02_Depth4_Obj50/
│       ├── ...（9种参数组合）
│       └── comparison/
│           ├── param_matrix.csv
│           └── performance_heatmap.png
│
├── 04_EndToEnd/                       # 实验4：端到端性能
│   └── dataset_*/
│       ├── A_RawGLB/
│       ├── B_InstancingOnly/
│       ├── C_Instancing_LOD/
│       ├── D_Instancing_HLOD/
│       ├── E_FullPipeline/
│       └── comparison/
│
├── 05_NonUniformScale/                # 实验5：非均匀缩放
│   └── dataset_*/
│       ├── 01_UniformOnly/
│       ├── 02_NonUniformAllowed/
│       └── comparison/
│
├── 06_CrossGLBHLOD/                   # 实验6：跨GLB HLOD（新增）
│   └── dataset_多楼层建筑/
│       ├── input/                     # 原始GLB文件索引
│       │   └── input_files.txt
│       │
│       ├── A_MergedHLOD/              # 策略A：合并构建
│       │   ├── config.json
│       │   ├── input_files.txt        # 输入文件列表
│       │   ├── tileset.json           # 统一tileset
│       │   ├── T0_*.glb               # HLOD瓦片
│       │   ├── T1_*.glb
│       │   └── ...
│       │
│       ├── B_SeparateHLOD/            # 策略B：独立构建
│       │   ├── Floor_01/              # 每个GLB独立目录
│       │   │   ├── tileset.json
│       │   │   └── ...
│       │   ├── Floor_02/
│       │   └── ...
│       │
│       └── comparison/                # 对比分析
│           ├── spatial_analysis.csv   # 空间结构对比
│           ├── performance_metrics.csv # 性能指标对比
│           ├── comparison_report.txt  # 文字报告
│           ├── strategy_recommendation.md # 策略推荐
│           └── charts/
│
├── _scripts/                          # 辅助脚本（可选）
│   ├── run_experiment1.py
│   ├── generate_comparison.py
│   └── collect_results.py
│
└── _summary/                          # 总体汇总
    ├── all_experiments_summary.csv
    └── final_conclusion.md
```

---

## 五、关键文件说明

### 4.1 原有输出文件（保留）

| 文件 | 说明 | 适用场景 |
|------|------|----------|
| `instancing_analysis.csv` | 实例化分析指标 | 所有实验 |
| `instancing_analysis.txt` | 详细文字报告 | 调试分析 |
| `*_results.csv` | 详细分组信息 | 深入研究 |
| `instance_lod_analysis.csv` | Instance LOD 5级指标 | 实验2,4 |
| `non_instance_lod_analysis.csv` | Non-Instance LOD 指标 | 实验2 |
| `hlod_analysis.csv` | HLOD指标 | 实验3,6 |

### 4.2 实验模式新增文件

| 文件 | 说明 | 作用 |
|------|------|------|
| `config.json` | 完整配置参数 | **可复现实验** |
| `README.md` | 实验/策略说明 | 快速理解 |
| `metrics_comparison.csv` | 多策略对比 | 论文数据 |
| `comparison_report.txt` | 自动对比报告 | 分析结论 |
| `strategy_recommendation.md` | 策略推荐（实验6） | 工程指导 |

---

## 六、文件关联关系

```
普通模式输出（保留）          实验模式输出（新增）
    │                              │
    ├── instancing_analysis.csv ──┼──► experiments/01_InstancingStrategy/
    │                              │       dataset_xxx/02_Moderate_0.05m/
    │                              │           ├── instancing_analysis.csv
    │                              │           ├── config.json
    │                              │           └── README.md
    │                              │
    ├── instance_lod_output/ ──────┼──► experiments/02_LODStrategy/
    │   instance_lod_analysis.csv  │       dataset_xxx/A_InstancingLOD/
    │                              │           ├── instance_lod_analysis.csv
    │                              │           └── ...
    │                              │
    └── quadtree_output/ ──────────┼──► experiments/06_CrossGLBHLOD/
            hlod_analysis.csv      │       dataset_xxx/A_MergedHLOD/
                                   │           ├── hlod_analysis.csv
                                   │           └── ...
                                   │
                                   └── comparison/（自动生成）
                                       ├── metrics_comparison.csv
                                       └── charts/
```

---

## 七、使用示例

### 示例1：普通运行（不启用实验模式）

```ini
# config.txt
input_directory = D:/Data/BIM
output_directory = D:/Output/Results
enable_experiment_mode = false
```

**输出结果**：
```
D:/Output/Results/
├── instancing_analysis.csv
├── instanced_meshes.glb
├── non_instanced_meshes.glb
└── ...
```

### 示例2：启用实验模式

```ini
# config.txt
input_directory = D:/Data/BIM
output_directory = D:/Output/Results
enable_experiment_mode = true
experiment_dataset_name = 住宅标准层
experiment_strategy_id = 02_Moderate_0.05m
tolerance = 0.05
instance_limit = 3
```

**输出结果**：
```
D:/Output/
├── Results/                              # 原有输出（保留）
│   ├── instancing_analysis.csv
│   ├── instanced_meshes.glb
│   └── ...
│
└── experiments/                          # 新增实验目录
    └── 01_InstancingStrategy/
        └── dataset_住宅标准层/
            ├── 02_Moderate_0.05m/
            │   ├── config.json
            │   ├── instancing_analysis.csv
            │   └── README.md
            └── comparison/
```

### 示例3：实验6（跨GLB HLOD）

```ini
# config.txt
input_directory = D:/Data/多楼层建筑    # 包含 Floor_01.glb, Floor_02.glb...
output_directory = D:/Output/Results
enable_experiment_mode = true
run_cross_glb_hlod_experiment = true
experiment_dataset_name = 多楼层建筑
```

**输出结果**：
```
D:/Output/
├── Results/                              # 原有输出
│   └── ...
│
└── experiments/
    └── 06_CrossGLBHLOD/
        └── dataset_多楼层建筑/
            ├── A_MergedHLOD/             # 合并策略输出
            ├── B_SeparateHLOD/           # 独立策略输出
            │   ├── Floor_01/
            │   └── Floor_02/
            └── comparison/               # 自动对比
                ├── spatial_analysis.csv
                ├── performance_metrics.csv
                └── strategy_recommendation.md
```

---

## 八、注意事项

1. **磁盘空间**：实验模式会生成额外文件，建议使用符号链接节省空间：
   ```ini
   use_symbolic_links = true
   ```

2. **文件链接**：
   - 实验目录中的 `.glb` 文件默认是**符号链接**（节省空间）
   - CSV和JSON文件是**独立副本**（便于版本管理）

3. **路径长度**：Windows有260字符路径限制，建议：
   - 使用短路径名（如 `D:/Exp/` 而非 `D:/Dissertation/Experiments/`）
   - 使用简洁的数据集名称

4. **覆盖策略**：
   - 同名策略目录会**保留旧文件**（不自动删除）
   - 重新运行前建议手动清理旧目录

---

## 九、快速查找指南

| 需要查找 | 位置 |
|----------|------|
| 原始分析数据 | `output_directory/instancing_analysis.csv` |
| 实验对比数据 | `experiments/01_InstancingStrategy/dataset_xxx/comparison/` |
| 可复现配置 | `experiments/01_InstancingStrategy/dataset_xxx/02_Moderate/config.json` |
| 论文图表数据 | `experiments/01_InstancingStrategy/dataset_xxx/comparison/charts/` |
| 实验结论 | `experiments/_summary/final_conclusion.md` |

---

*该文档说明实验框架的输出结构，便于用户理解文件组织方式和快速定位所需数据。*
