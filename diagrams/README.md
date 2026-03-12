# GltfInstancingAndTilingTool 流程图说明

## 文件列表

| 文件 | 格式 | 说明 |
|------|------|------|
| **技术路线.drawio** | drawio | 技术路线总览：GLB 输入 → 实例化检测（语义+**材质**+几何）→ LOD/HLOD 输出，含 material_filter_mode 与 Analysis CSV |
| **技术流程图_mermaid.md** | Mermaid | 完整技术流程图，含 9 个子图：总体流程、实例化检测、**材质过滤**、Instance LOD、Non-instanced LOD、HLOD、语义映射、输出目录、参数依赖 |
| **完整流程与参数配置.drawio** | drawio | Stage 1~4 与参数配置的对应关系，含输入/输出说明 |
| **HLOD建立流程.drawio** | drawio | Quadtree HLOD 三阶段：空间划分 → 自底向上内容生成 → 输出 |
| **几何相似度判定流程.drawio** | drawio | SemanticMaterialGeometricDetector 内部流程：语义分组 → **材质过滤**（none/hash/index）→ Hausdorff → 实例聚类 |

## 查看方式

- **drawio**：使用 [draw.io](https://app.diagrams.net/) 或 VS Code draw.io 插件打开
- **Mermaid**：在支持 Mermaid 的 Markdown 预览中查看（如 VS Code、GitHub、Typora），或使用 [Mermaid Live Editor](https://mermaid.live/)

## 技术流程图_mermaid.md 子图索引

1. **总体流程概览**：Stage 1~4 主流程与分支
2. **实例化检测详细流程**：SemanticMaterialGeometricDetector 语义分组、**材质过滤**、Hausdorff 比较
3. **材质过滤流程**：material_filter_mode 三种模式（none/hash/index）及 getMeshMaterialHash、getMeshMaterialIndex
4. **Instance LOD 生成流程**：InstancingLODManager 语义+几何聚类
5. **Non-instanced LOD 流程**：网格简化与可选后处理实例化
6. **Quadtree HLOD 流程**：空间划分、自底向上、merge-simplify-detect
7. **语义映射保留机制**：glbStem|meshHashId 写入与解析
8. **输出目录结构**：01_instancing、02_*、03_hlod
9. **参数依赖关系**：Stage 1 参数复用至 Non-inst LOD / HLOD
