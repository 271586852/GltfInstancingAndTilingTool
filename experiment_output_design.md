# 实验输出文件夹结构设计

## 设计原则

1. **一目了然**：打开根目录即可看到所有实验
2. **自包含**：每个实验文件夹包含完整的数据、结果和说明
3. **可追溯**：通过README和配置文件记录实验参数
4. **易对比**：相同类型实验数据文件命名一致，便于批量分析

---

## 输出文件夹结构

```
experiments/                                      # 实验根目录
├── README.md                                     # 总说明：所有实验列表和摘要
│
├── 01_InstancingStrategy/                        # 实验1：实例化检测策略对比
│   ├── README.md                                 # 实验说明：目的、参数、结论
│   ├── config_summary.csv                        # 实验配置汇总
│   │
│   ├── dataset_住宅标准层/                        # 数据集1结果
│   │   ├── README.md                             # 数据集说明
│   │   ├── 01_Strict_0.99/                       # 策略1：严格相似度（similarity_threshold=0.99）
│   │   │   ├── config.json                       # 完整配置参数
│   │   │   ├── output.glb                        # 输出模型
│   │   │   ├── tileset.json                      # 3D Tiles文件
│   │   │   ├── instancing_report.txt             # 文本报告（详细分组信息）
│   │   │   ├── instancing_analysis.csv           # 关键指标CSV
│   │   │   └── screenshots/                      # 渲染截图
│   │   │       ├── overview.png
│   │   │       └── detail.png
│   │   │
│   │   ├── 02_Moderate_0.95/                     # 策略2：中等相似度（similarity_threshold=0.95）
│   │   │   ├── config.json
│   │   │   ├── output.glb
│   │   │   ├── tileset.json
│   │   │   ├── instancing_report.txt
│   │   │   ├── instancing_analysis.csv
│   │   │   └── screenshots/
│   │   │
│   │   ├── 03_Lenient_0.90/                      # 策略3：宽松相似度（similarity_threshold=0.90）
│   │   │   ├── config.json
│   │   │   ├── output.glb
│   │   │   ├── tileset.json
│   │   │   ├── instancing_report.txt
│   │   │   ├── instancing_analysis.csv
│   │   │   └── screenshots/
│   │   │
│   │   └── comparison/                           # 对比分析（自动生成）
│   │       ├── comparison_report.txt             # 文字对比报告
│   │       ├── metrics_comparison.csv            # 指标对比表
│   │       └── charts/                           # 对比图表
│   │           ├── instancing_rate.png
│   │           ├── file_size.png
│   │           └── compression_ratio.png
│   │
│   ├── dataset_办公楼MEP/                         # 数据集2结果
│   │   ├── 01_Strict_0.00m/
│   │   ├── 02_Moderate_0.05m/
│   │   ├── 03_Lenient_0.10m/
│   │   └── comparison/
│   │
│   └── summary/                                  # 实验1总体汇总
│       ├── cross_dataset_comparison.csv          # 跨数据集对比
│       └── conclusion.md                         # 实验结论
│
├── 02_LODStrategy/                               # 实验2：LOD策略对比
│   ├── README.md
│   │
│   ├── dataset_住宅标准层/
│   │   ├── README.md
│   │   │
│   │   ├── A_InstancingLOD/                      # A组：实例化LOD
│   │   │   ├── config.json
│   │   │   ├── instancing_lod/
│   │   │   │   ├── LOD5_Original.glb             # LOD5：原始精度
│   │   │   │   ├── LOD4_Variant.glb              # LOD4：Family级
│   │   │   │   ├── LOD3_Class.glb                # LOD3：Category级
│   │   │   │   ├── LOD2_Abstract.glb             # LOD2：抽象级
│   │   │   │   ├── LOD1_Proxy.glb                # LOD1：代理盒
│   │   │   │   └── tileset_lod.json              # LOD层级tileset
│   │   │   ├── lod_analysis.csv                  # LOD指标汇总
│   │   │   └── lod_report.txt                    # 详细报告
│   │   │
│   │   ├── B_NonInstancingLOD/                   # B组：非实例化LOD
│   │   │   ├── config.json
│   │   │   ├── non_instancing_lod/
│   │   │   │   ├── LOD0_Original.glb
│   │   │   │   ├── LOD1_Simplified.glb           # 简化率50%
│   │   │   │   ├── LOD2_Simplified.glb           # 简化率25%
│   │   │   │   ├── LOD3_Simplified.glb           # 简化率12.5%
│   │   │   │   └── tileset_lod.json
│   │   │   ├── non_instanced_lod_report.csv
│   │   │   └── lod_report.txt
│   │   │
│   │   └── comparison/                           # A vs B对比
│   │       ├── lod_comparison_report.txt
│   │       ├── lod_metrics_comparison.csv
│   │       └── charts/
│   │           ├── filesize_by_lodlevel.png
│   │           ├── vertex_reduction.png
│   │           └── quality_vs_compression.png
│   │
│   └── summary/
│       └── conclusion.md
│
├── 03_HLODParams/                                # 实验3：HLOD参数评估
│   ├── README.md
│   │
│   ├── dataset_医院整体/
│   │   ├── README.md
│   │   │
│   │   ├── 01_Depth4_Obj30/                      # 配置1：深度4，30对象/瓦片
│   │   │   ├── config.json
│   │   │   ├── quadtree_output/
│   │   │   │   ├── tileset.json
│   │   │   │   ├── T0_root.glb
│   │   │   │   ├── T1_0_0.glb
│   │   │   │   ├── T1_0_1.glb
│   │   │   │   └── ...
│   │   │   ├── hlod_analysis.csv                 # HLOD指标
│   │   │   └── tree_structure.txt                # 树结构可视化
│   │   │
│   │   ├── 02_Depth4_Obj50/
│   │   ├── 03_Depth4_Obj100/
│   │   ├── 04_Depth6_Obj30/
│   │   ├── 05_Depth6_Obj50/
│   │   ├── 06_Depth6_Obj100/
│   │   ├── 07_Depth8_Obj30/
│   │   ├── 08_Depth8_Obj50/
│   │   ├── 09_Depth8_Obj100/
│   │   │
│   │   └── comparison/
│   │       ├── param_matrix.csv                  # 参数矩阵
│   │       ├── performance_heatmap.png           # 性能热力图
│   │       └── optimal_params.md                 # 最优参数推荐
│   │
│   └── summary/
│       └── conclusion.md
│
├── 04_EndToEnd/                                  # 实验4：端到端综合对比
│   ├── README.md
│   │
│   ├── dataset_小型_住宅/
│   │   ├── README.md
│   │   │
│   │   ├── A_RawGLB/                             # 策略A：原始GLB
│   │   │   ├── raw.glb
│   │   │   └── metrics.json                      # 性能指标
│   │   │
│   │   ├── B_InstancingOnly/                     # 策略B：仅实例化
│   │   ├── C_Instancing_LOD/                     # 策略C：实例化+LOD
│   │   ├── D_Instancing_HLOD/                    # 策略D：实例化+HLOD
│   │   ├── E_FullPipeline/                       # 策略E：完整流程
│   │   │
│   │   └── comparison/
│   │       ├── all_metrics.csv                   # 所有策略指标汇总
│   │       ├── radar_chart.png                   # 雷达图对比
│   │       └── ranking.md                        # 排名与建议
│   │
│   ├── dataset_中型_MEP/
│   └── dataset_大型_医院/
│
├── 05_NonUniformScale/                           # 实验5：非均匀缩放
│   ├── README.md
│   │
│   └── dataset_通风管道测试/
│       ├── 01_UniformOnly/                       # 仅均匀缩放
│       ├── 02_NonUniformAllowed/                 # 允许非均匀缩放
│       └── comparison/
│           ├── detection_comparison.csv
│           └── extra_instances_found.txt
│
├── _scripts/                                     # 辅助脚本
│   ├── run_experiment1.py                        # 批量运行实验1
│   ├── run_experiment2.py
│   ├── run_experiment3.py
│   ├── run_experiment4.py
│   ├── generate_comparison.py                    # 生成对比报告
│   ├── plot_charts.py                            # 绘制图表
│   └── collect_results.py                        # 汇总所有结果
│
└── _summary/                                     # 总体汇总
    ├── all_experiments_summary.csv               # 所有实验摘要
    ├── cross_experiment_analysis.md              # 跨实验分析
    └── final_conclusion.md                       # 最终结论
```

---

## 关键文件格式规范

### 1. 每个实验文件夹必含文件

| 文件 | 内容 | 作用 |
|------|------|------|
| `README.md` | 实验目的、方法、参数、结论 | 让人快速理解实验 |
| `config.json` | 完整的工具配置参数 | 可复现实验 |
| `*_analysis.csv` | 核心指标数据 | 用于分析和绘图 |
| `*_report.txt` | 详细文字报告 | 深入细节 |

### 2. CSV文件命名规范

```
{实验类型}_{数据集}_{策略/参数}_analysis.csv

示例：
- instancing_住宅标准层_Strict_analysis.csv
- lod_办公楼MEP_InstancingLOD_analysis.csv
- hlod_医院整体_Depth6_Obj50_analysis.csv
```

### 3. CSV文件结构标准

**instancing_analysis.csv**
```csv
Metric,Value,Unit
Input Models,1,count
Initial Nodes,372,count
Initial Meshes,155,count
Initial Instances,0,count
Instanced Groups,25,count
Final Instances,271,count
Non-instanced Meshes,45,count
Final Nodes,70,count
Final Meshes,70,count
Total Displayed Meshes,316,count
Node Reduction (%),81.18,%
Initial Instancing Ratio (%),0.00,%
Final Instancing Ratio (%),85.76,%
Instancing Increase (%),85.76,%
File Size Input (MB),46.78,MB
File Size Output (MB),3.85,MB
File Size Reduction (%),91.77,%
Processing Time (s),2.35,s
```

**lod_analysis.csv**
```csv
LOD Level,File Size (MB),Unique Meshes,Total Instances,Vertices,Triangles,Reduction Rate (%),Geometric Error (m)
LOD5_Original,3.85,25,271,141846,82964,0.00,0.00
LOD4_Variant,3.85,25,271,141846,82964,0.00,0.00
LOD3_Class,0.12,10,271,3288,1824,97.68,0.15
LOD2_Abstract,0.12,10,271,3288,1824,97.68,0.15
LOD1_Proxy,0.01,1,271,24,12,99.98,2.50
```

**hlod_analysis.csv**
```csv
Level,Tile,File Size (KB),Unique Meshes,Instances,Stored Triangles,Rendered Triangles,Reduction Ratio,Center X,Center Y,Center Z
1,T1_0_0.glb,3779.02,21,0,45544,45544,1.0000,100.5,200.3,15.0
1,T1_0_1.glb,12834.29,24,0,155944,155944,1.0000,150.2,200.3,15.0
0,T0_0_0.glb,3120.21,68,0,0,0,1.0000,125.0,180.0,15.0
```

---

## README.md 模板

### 实验级 README.md

```markdown
# 实验X：实验名称

## 实验目的
简述要解决什么科学问题

## 实验设计
- 变量：改了什么参数
- 对照：用什么做基准
- 数据集：用了哪些数据

## 文件夹结构
说明子文件夹组织方式

## 关键发现
简要结论（1-3条）

## 详细结果
链接到各数据集的 comparison 文件夹
```

### 数据集级 README.md

```markdown
# 数据集名称

## 数据描述
- 来源：Revit版本、项目类型
- 规模：构件数量、文件大小
- 特点：为什么选这个数据集

## 实验配置
| 参数 | 值 | 说明 |
|------|-----|------|
| tolerance | 0.05 | 几何容差 |
| instance_limit | 3 | 最小实例数 |

## 结果摘要
关键数据对比表

## 输出文件说明
各文件夹/文件含义
```

---

## 自动化脚本设计

### 批量运行脚本

```python
# run_experiment1.py 伪代码

datasets = ["住宅标准层", "办公楼MEP"]
strategies = [
    {"name": "Strict", "tolerance": 0.00, "limit": 5},
    {"name": "Moderate", "tolerance": 0.05, "limit": 3},
    {"name": "Lenient", "tolerance": 0.10, "limit": 2}
]

for dataset in datasets:
    for strategy in strategies:
        output_dir = f"01_InstancingStrategy/{dataset}/{strategy['name']}_{strategy['tolerance']:.2f}m"

        # 生成配置文件
        config = generate_config(strategy, output_dir)

        # 运行工具
        run_tool(config, dataset_input_path)

        # 复制/生成截图
        capture_screenshots(output_dir)

# 自动生成对比报告
generate_comparison_report("01_InstancingStrategy")
```

### 结果汇总脚本

```python
# collect_results.py 伪代码

def collect_experiment1():
    """汇总实验1所有结果到总表"""
    results = []

    for dataset_dir in glob("01_InstancingStrategy/dataset_*/"):
        dataset_name = extract_name(dataset_dir)

        for strategy_dir in glob(f"{dataset_dir}/*_0.*/"):
            strategy_name = extract_name(strategy_dir)
            csv_path = f"{strategy_dir}/instancing_analysis.csv"

            metrics = parse_csv(csv_path)
            results.append({
                "Dataset": dataset_name,
                "Strategy": strategy_name,
                **metrics
            })

    # 保存汇总表
    save_to_csv(results, "01_InstancingStrategy/summary/cross_dataset_comparison.csv")

    # 生成可视化
    generate_charts(results, "01_InstancingStrategy/summary/charts/")
```

---

## 使用流程示例

### 执行单个实验

```bash
# 1. 进入实验目录
cd experiments/01_InstancingStrategy

# 2. 查看实验说明
cat README.md

# 3. 运行批量脚本
python ../_scripts/run_experiment1.py

# 4. 查看结果
ls dataset_住宅标准层/comparison/
# charts/
# comparison_report.txt
# metrics_comparison.csv
```

### 对比分析

```bash
# 查看某个数据集的所有策略对比
cat experiments/01_InstancingStrategy/dataset_住宅标准层/comparison/metrics_comparison.csv

# 查看跨数据集汇总
cat experiments/01_InstancingStrategy/summary/cross_dataset_comparison.csv
```

### 论文写作

```bash
# 所有实验汇总
cat experiments/_summary/all_experiments_summary.csv

# 最终结论
cat experiments/_summary/final_conclusion.md
```

---

## 优势说明

| 特性 | 说明 |
|------|------|
| **自解释** | 每个文件夹都有README，无需记忆 |
| **可复现** | config.json保存完整参数 |
| **易对比** | 相同命名规范，便于脚本批量处理 |
| **可视化** | 自动生成图表，直接用于论文 |
| **可扩展** | 新增实验只需新建文件夹 |
| **版本控制友好** | 文本报告+CSV，适合git管理 |

---

## 实施建议

1. **逐步实现**：先做实验1的完整流程，验证设计合理后再扩展
2. **脚本先行**：写好生成对比报告的脚本，避免手动整理
3. **及时记录**：每次运行后立即填写该实验的README结论部分
4. **定期汇总**：每完成一个实验就运行汇总脚本，及早发现问题

