# GltfInstancingAndTilingTool 完整技术流程图

## 一、总体流程概览

```mermaid
flowchart TB
    subgraph 输入
        A1[GLB 文件目录<br/>input_directory]
        A2[语义 XML<br/>semantic_data_path]
        A3[配置文件 / 命令行参数]
    end

    subgraph Stage1["Stage 1: 实例化检测"]
        B1[GlbReader<br/>发现与加载 GLB]
        B2[SemanticParser<br/>解析 RISCRVT]
        B3[SemanticMaterialGeometricDetector<br/>语义分组 + 材质过滤 + Hausdorff 相似度]
        B4[InstancingDetectionResult<br/>实例组 / 非实例 mesh]
        B5[GlbWriter<br/>instanced.glb / non_instanced.glb]
        B6[TilesetWriter<br/>instanced.json / non_instanced.json]
        B7[Analysis<br/>instancing.csv / per_glb.csv]
    end

    subgraph Stage2["Stage 2: 可选处理"]
        C1{mesh_segmentation?}
        C2[writeMeshesAsSeparateGlbs<br/>每 mesh 单独 GLB]
        C3[CSV 后处理<br/>csv_directory]
    end

    subgraph Stage3["Stage 3: LOD 生成"]
        D1{enable_instance_lod?}
        D2[InstancingLODManager<br/>语义+几何 LOD5→LOD1]
        D3{enable_non_instanced_lod?}
        D4[NonInstancingLODManager<br/>网格简化 LOD 链]
        D5{enable_non_instanced_lod_instancing?}
        D6[后处理实例化检测<br/>每级 LOD 实例化]
    end

    subgraph Stage4["Stage 4: HLOD"]
        E1{enable_quadtree?}
        E2[Split non_instanced.glb<br/>单 mesh GLB]
        E3[QuadtreePipeline<br/>四叉树空间划分]
        E4[自底向上生成 tile<br/>merge → simplify → 实例化检测]
        E5[hlod_analysis.csv<br/>tileset.json]
    end

    A1 --> B1
    A2 --> B2
    A3 --> B1
    B2 --> B3
    B1 --> B3
    B3 --> B4
    B4 --> B5
    B5 --> B6
    B4 --> B7

    B5 --> C1
    C1 -->|是| C2
    B5 --> C3

    B4 --> D1
    D1 -->|是| D2
    B5 --> D3
    D3 -->|是| D4
    D4 --> D5
    D5 -->|是| D6

    B5 --> E1
    E1 -->|是| E2
    E2 --> E3
    E3 --> E4
    E4 --> E5
```

## 二、实例化检测详细流程（SemanticMaterialGeometricDetector）

```mermaid
flowchart TB
    subgraph 输入
        M1[LoadedGltfModel 集合]
    end

    subgraph 语义分组
        S1[semantic_hash_fields 为空?]
        S2[__GLOBAL__ 单组]
        S3[buildSemanticHashKey<br/>category|family|type]
        S4[mesh 名含 glbStem|meshHashId?]
        S5[解析 glbStem 用于 RISCRVT 查找]
        S6[按 semanticKey 分组]
    end

    subgraph 组内比较
        G1[同组内两两比较]
        G2[material_filter_mode<br/>hash/index/none]
        G3[allow_unknown_cross_mesh_clustering<br/>unknown 是否跨 mesh]
        G4[点云采样<br/>hausdorff_max_sample_points]
        G5[computeHausdorffDistance<br/>双向 Hausdorff]
        G6[similarity = 1/(1+distance)]
        G7[similarity >= threshold?]
    end

    subgraph 输出
        O1[InstancedMeshGroup<br/>instances ≥ instance_limit]
        O2[NonInstancedMeshInfo]
    end

    M1 --> S1
    S1 -->|是| S2
    S1 -->|否| S3
    S3 --> S4
    S4 --> S5
    S5 --> S6
    S2 --> S6

    S6 --> G1
    G1 --> G2
    G2 --> G3
    G3 --> G4
    G4 --> G5
    G5 --> G6
    G6 --> G7
    G7 -->|是| O1
    G7 -->|否| O2
```

## 三、Instance LOD 生成流程（InstancingLODManager）

```mermaid
flowchart LR
    subgraph 输入
        L1[instanced groups<br/>+ loadedModels]
        L2[语义 XML<br/>LOD4/LOD3 聚类]
    end

    subgraph LOD5
        P1[LOD5 = 原始<br/>最精细]
    end

    subgraph 聚类
        C1[enable_semantic_check<br/>Family/Category 语义]
        C2[enable_geometric_check<br/>体积/长宽比]
        C3[lod4_size_tolerance<br/>体积聚类回退]
        C4[lod3_aspect_ratio_tolerance<br/>长宽比聚类回退]
    end

    subgraph 输出
        O1[LOD5→LOD1 GLB]
        O2[tileset.json]
        O3[instance_lod.csv<br/>InstancingRatio / InstancingIncrease]
    end

    L1 --> P1
    L2 --> C1
    P1 --> C1
    C1 --> C2
    C2 --> C3
    C3 --> C4
    C4 --> O1
    O1 --> O2
    O1 --> O3
```

## 四、Non-instanced LOD 流程

```mermaid
flowchart TB
    subgraph 输入
        N1[non_instanced.glb]
    end

    subgraph 网格简化
        N2[NonInstancingLODManager<br/>generateLODFilesOnly]
        N3[meshopt 简化<br/>non_instanced_lod_ratio]
        N4[non_instanced_LOD0..LODn.glb]
    end

    subgraph 可选实例化
        N5{enable_non_instanced_lod_instancing?}
        N6[每级 LOD 实例化检测<br/>hlod 参数]
        N7{enable_non_instanced_lod_clustering?}
        N8[Family/Category 聚类]
    end

    subgraph 输出
        N9[non_instance_lod_analysis.csv<br/>几何简化统计]
        N10[non_instance_lod_instancing_analysis.csv<br/>InstancingRatio / InstancingIncrease]
    end

    N1 --> N2
    N2 --> N3
    N3 --> N4
    N4 --> N5
    N5 -->|否| N9
    N5 -->|是| N6
    N6 --> N7
    N7 -->|是| N8
    N7 -->|否| N10
    N8 --> N10
```

## 五、Quadtree HLOD 流程

```mermaid
flowchart TB
    subgraph 输入准备
        Q1{mesh_segmentation?}
        Q2[使用 segmented 输出]
        Q3[Split non_instanced.glb<br/>writeMeshesAsSeparateGlbs]
        Q4[单 mesh GLB 目录]
    end

    subgraph 阶段1_空间划分
        Q5[scanInputDirectory<br/>扫描 GLB]
        Q6[buildQuadtree<br/>XZ 平面四叉树]
        Q7[recursiveSplit<br/>max_objects_per_tile]
        Q8[calculateTightBounds]
    end

    subgraph 阶段2_自底向上
        Q9[generateLeafTile<br/>mergeModel + sourceStem]
        Q10[processParentTile]
        Q11[unpackInstancing<br/>展开 EXT_mesh_gpu_instancing]
        Q12[mergeModel 子 tile]
        Q13[simplifyModel 0.5]
        Q14[SemanticMaterialGeometricDetector<br/>语义+几何实例化]
        Q15{enable_hlod_clustering?}
        Q16[InstancingLODManager.clusterInstancingResult]
    end

    subgraph 输出
        Q17[tileset.json]
        Q18[hlod_analysis.csv<br/>Per-Tile + Per-Level Summary]
    end

    Q1 -->|是| Q2
    Q1 -->|否| Q3
    Q2 --> Q4
    Q3 --> Q4

    Q4 --> Q5
    Q5 --> Q6
    Q6 --> Q7
    Q7 --> Q8
    Q8 --> Q9
    Q8 --> Q10
    Q9 --> Q10
    Q10 --> Q11
    Q11 --> Q12
    Q12 --> Q13
    Q13 --> Q14
    Q14 --> Q15
    Q15 -->|是| Q16
    Q15 -->|否| Q17
    Q16 --> Q17
    Q17 --> Q18
```

## 六、语义映射保留机制

```mermaid
flowchart LR
    subgraph 写入时保留
        W1[writeNonInstancedMeshesOnly<br/>mesh.name = glbStem|meshName]
        W2[writeMeshesAsSeparateGlbs<br/>mesh 名含 | 则不重复前缀]
        W3[mergeModel + sourceStem<br/>Leaf tile 合并时添加 stem]
    end

    subgraph 查找时解析
        R1[SemanticMaterialGeometricDetector<br/>mesh 名 find '|']
        R2[glbStem = 前半部分<br/>meshHashId = 后半部分]
        R3[getSemanticInfo glbStem meshHashId]
    end

    W1 --> R1
    W2 --> R1
    W3 --> R1
    R1 --> R2
    R2 --> R3
```

## 七、输出目录结构

```mermaid
flowchart TB
    subgraph output_directory
        O[output_directory]
    end

    subgraph 01_instancing
        I1[instanced.glb]
        I2[non_instanced.glb]
        I3[instanced.json / non_instanced.json]
        I4[analysis/instancing.csv]
        I5[analysis/per_glb.csv]
    end

    subgraph 02_instance_lod
        L1[LOD1..LOD5.glb]
        L2[tileset.json]
        L3[analysis/instance_lod.csv]
    end

    subgraph 02_non_instance_lod
        N1[non_instanced_LOD0..LODn.glb]
        N2[analysis/non_instance_lod_analysis.csv]
        N3[instanced_lods/ 若启用实例化]
        N4[analysis/non_instance_lod_instancing_analysis.csv]
    end

    subgraph 03_hlod
        H1[tiles/*.glb]
        H2[tileset.json]
        H3[hlod_analysis.csv]
    end

    O --> 01_instancing
    O --> 02_instance_lod
    O --> 02_non_instance_lod
    O --> 03_hlod
```

## 八、参数依赖关系

```mermaid
flowchart TB
    subgraph Stage1参数
        P1[similarity_thresholds]
        P2[semantic_hash_fields]
        P3[instance_limit]
        P4[hausdorff_max_sample_points]
        P5[material_filter_mode]
        P6[allow_unknown_cross_mesh_clustering]
    end

    subgraph 复用至NonInstLOD_HLOD
        P2
        P4
        P5
        P6
    end

    subgraph HLOD特有
        P7[hlod_similarity_thresholds]
        P8[hlod_instance_limit]
        P9[enable_hlod_clustering]
    end

    subgraph NonInstLOD特有
        P10[non_instanced_lod_similarity_thresholds]
        P11[enable_non_instanced_lod_instancing]
        P12[enable_non_instanced_lod_clustering]
    end

    subgraph InstanceLOD特有
        P13[instance_lod_similarity_thresholds]
        P14[lod4_size_tolerance]
        P15[lod3_aspect_ratio_tolerance]
    end
```
