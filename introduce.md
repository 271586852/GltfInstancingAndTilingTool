# GltfInstancingAndTilingTool 技术路线

## 概述

该工具面向批量 GLB 模型的实例化检测与 3D Tiles 输出，并可选生成多层 LOD。核心流程以「读入 → 分析 → 重组 → 输出」为主线，结合语义与几何特征进行分层组织，最终生成可用于流式加载的 GLB/tileset。

## 流程图

- **diagrams/技术路线.drawio**：技术路线总览（drawio 格式）
- **diagrams/技术流程图_mermaid.md**：完整技术流程图（Mermaid 格式，含实例化检测、LOD、HLOD、语义映射等详细流程）
- **diagrams/完整流程与参数配置.drawio**：流程与参数配置对应关系
- **diagrams/HLOD建立流程.drawio**：Quadtree HLOD 建立流程
- **diagrams/几何相似度判定流程.drawio**：Hausdorff 几何相似度判定流程

## 技术路线（处理流程）

1. **配置与日志初始化**
   - 支持配置文件与命令行参数覆盖（输入/输出目录、相似度阈值、实例阈值、LOD 参数等）。
   - 统一日志系统记录流程与统计信息。
2. **输入发现与加载**
   - 递归扫描输入目录中的 GLB 文件。
   - 通过 `GlbReader` 加载为 `CesiumGltf::Model`，并记录文件 Hash 与唯一 ID。
3. **实例化检测**
   - `SemanticMaterialGeometricDetector` 以**语义分组**（category、family、type）为基础，结合**材质过滤**与 **Hausdorff 几何相似度**进行 mesh 级聚类。
   - 支持 `similarity_thresholds`、`hausdorff_max_sample_points`、`material_filter_mode` 等参数，以提升「近似重复」识别能力。
4. **分析与报告**
   - 输出文本与 CSV 分析结果（实例化分组、数量统计等）。输出位于 `01_instancing/analysis/`。
5. **GLB 重组与输出**
   - 使用 `GlbWriter` 生成：
     - 仅实例化 mesh 的 GLB（`instanced.glb`）
     - 仅非实例化 mesh 的 GLB（`non_instanced.glb`）
   - 支持合并输出或分离输出。
6. **Tileset 生成**
   - 基于输出 GLB 计算包围盒与误差，写出 `instanced.json`、`non_instanced.json`。
7. **可选：非实例化 LOD 简化**
   - `NonInstancingLODManager` 对非实例化 GLB 进行网格简化，生成 LOD 链并写出独立 tileset。
8. **可选：网格分片**
   - 将 GLB 拆分为单 mesh GLB，便于进一步细粒度处理。
9. **可选：语义+几何驱动的实例化 LOD 生成**
   - 解析外部语义数据（XML），并结合几何特征作为分层依据。
   - `InstancingLODManager` 在 LOD5（原始层）基础上，按语义与几何规则联合驱动，逐级生成 LOD4~LOD1。
   - 逐层输出 LOD GLB，并构建层级 Tileset。
10. **CSV 后处理**
    - 若配置目录存在，进行 CSV 与 GLB 的对齐处理。

## 关键模块与职责

- `GlbReader`：扫描与加载 GLB，维护原始模型信息与文件 Hash。
- `SemanticMaterialGeometricDetector`：按语义分组 + 材质过滤 + Hausdorff 几何相似度进行实例化检测与分组。
- `SemanticParser`：解析语义 XML（RISCRVT），提供构件语义信息（category、family、type）。
- `GlbWriter`：根据检测结果重组 GLB，支持 instanced/non-instanced/LOD 输出。
- `TilesetWriter`：生成单层或层级 tileset.json。
- `InstancingLODManager`：结合语义+几何特征完成 LOD 分层与代表体选择。
- `NonInstancingLODManager`：对非实例化模型进行网格简化生成 LOD。
- `utilities`：日志、变换、包围盒、数据对比等基础工具。

## 主要输入与输出

- **输入**：GLB 文件目录（可选语义 XML、CSV 辅助数据）。
- **输出**（按 `01_instancing/` 等分层结构）：
  - `01_instancing/instanced.glb` / `non_instanced.glb`
  - `01_instancing/instanced.json` / `non_instanced.json`
  - `01_instancing/analysis/instancing.csv`、`instancing.txt` 等分析报告
  - 可选 `02_instance_lod/` 的 LOD*.glb 与层级 tileset.json
  - 可选 `03_hlod/` 的瓦片与 tileset.json
