# GltfInstancingAndTilingTool 完整技术流程图

> 使用 `graph` 语法以兼容更多 Mermaid 渲染器。若预览异常，可尝试 [Mermaid Live Editor](https://mermaid.live/) 或更新 Mermaid 版本。

## 一、总体流程概览

```mermaid
graph TB
    subgraph input [输入]
        A1[GLB 文件目录 input_directory]
        A2[语义 XML semantic_data_path]
        A3[配置文件 命令行参数]
    end

    subgraph stage1 [Stage 1 实例化检测]
        B1[GlbReader 发现与加载 GLB]
        B2[SemanticParser 解析 RISCRVT]
        B3[SemanticMaterialGeometricDetector 语义分组 材质过滤 Hausdorff]
        B4[InstancingDetectionResult 实例组 非实例 mesh]
        B5[GlbWriter instanced.glb non_instanced.glb]
        B6[TilesetWriter instanced.json non_instanced.json]
        B7[Analysis instancing.csv per_glb.csv]
    end

    subgraph material_param [材质参数]
        M0[material_filter_mode none hash index]
    end

    subgraph stage2 [Stage 2 可选处理]
        C1{mesh_segmentation}
        C2[writeMeshesAsSeparateGlbs 每 mesh 单独 GLB]
        C3[CSV 后处理 csv_directory]
    end

    subgraph stage3 [Stage 3 LOD 生成]
        D1{enable_instance_lod}
        D2[InstancingLODManager 语义几何 LOD5 到 LOD1]
        D3{enable_non_instanced_lod}
        D4[NonInstancingLODManager 网格简化 LOD 链]
        D5{enable_non_instanced_lod_instancing}
        D6[后处理实例化检测 每级 LOD 实例化]
    end

    subgraph stage4 [Stage 4 HLOD]
        E1{enable_quadtree}
        E2[Split non_instanced.glb 单 mesh GLB]
        E3[QuadtreePipeline 四叉树空间划分]
        E4[自底向上生成 tile merge simplify 实例化检测]
        E5[hlod_analysis.csv tileset.json]
    end

    A1 --> B1
    A2 --> B2
    A3 --> B1
    M0 -.参数.-> B3
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
graph TB
    subgraph input2 [输入]
        M1[LoadedGltfModel 集合]
    end

    subgraph semantic [语义分组]
        S1{semantic_hash_fields 为空}
        S2[__GLOBAL__ 单组]
        S3[buildSemanticHashKey category family type]
        S4{mesh 名含 glbStem meshHashId}
        S5[解析 glbStem 用于 RISCRVT 查找]
        S6[按 semanticKey 分组]
    end

    subgraph compare [组内比较 含材质过滤]
        G1[同组内两两比较]
        G2{material_filter_mode}
        G2a[none 不过滤 直接几何比较]
        G2b[hash 材质纹理 hash 相同才比较]
        G2c[index 材质索引相同才比较]
        G3[allow_unknown_cross_mesh_clustering unknown 是否跨 mesh]
        G4[点云采样 hausdorff_max_sample_points]
        G5[computeHausdorffDistance 双向 Hausdorff]
        G6[similarity 等于 1除以1加distance]
        G7{similarity 大于等于 threshold}
    end

    subgraph output1 [输出]
        O1[InstancedMeshGroup instances 大于等于 instance_limit]
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
    G2 -->|none| G2a
    G2 -->|hash| G2b
    G2 -->|index| G2c
    G2a --> G4
    G2b --> G4
    G2c --> G4
    G4 --> G5
    G5 --> G6
    G6 --> G7
    G7 -->|是| O1
    G7 -->|否| O2
```

## 三、材质过滤流程（material_filter_mode）

```mermaid
graph TB
    subgraph input_mat [输入]
        MA1[mesh 候选对 代表 mesh 与候选 mesh]
    end

    subgraph mode [material_filter_mode]
        MB1{mode}
        MB2[hash getMeshMaterialHash]
        MB3[index getMeshMaterialIndex]
    end

    subgraph result [结果]
        MC2[进行 Hausdorff 几何比较]
        MC3[材质不同 跳过 不比较]
    end

    MA1 --> MB1
    MB1 -->|none| MC2
    MB1 -->|hash| MB2
    MB1 -->|index| MB3
    MB2 -->|hash 相同| MC2
    MB2 -->|hash 不同| MC3
    MB3 -->|index 相同| MC2
    MB3 -->|index 不同| MC3
```

**说明**：
- **none**：不做材质过滤，直接进行 Hausdorff 几何相似度比较
- **hash**：`getMeshMaterialHash` 计算 PBR 因子、emissiveFactor、alphaMode、doubleSided 等内容 hash，相同才比较
- **index**：`getMeshMaterialIndex` 取首个 primitive 的材质索引，相同才比较

Instance LOD 另有 `instance_lod_material_filter_mode`，可单独配置。

## 四、Instance LOD 生成流程（InstancingLODManager）

```mermaid
graph LR
    subgraph input3 [输入]
        L1[instanced groups loadedModels]
        L2[语义 XML LOD4 LOD3 聚类]
    end

    subgraph lod5 [LOD5]
        P1[LOD5 原始 最精细]
    end

    subgraph cluster [聚类]
        C1[enable_semantic_check Family Category 语义]
        C2[enable_geometric_check 体积 长宽比]
        C3[lod4_size_tolerance 体积聚类回退]
        C4[lod3_aspect_ratio_tolerance 长宽比聚类回退]
        C5[instance_lod_material_filter_mode 材质过滤]
    end

    subgraph output2 [输出]
        O1[LOD5 到 LOD1 GLB]
        O2[tileset.json]
        O3[instance_lod.csv InstancingRatio InstancingIncrease]
    end

    L1 --> P1
    L2 --> C1
    P1 --> C1
    C1 --> C2
    C2 --> C3
    C3 --> C4
    C4 --> C5
    C5 --> O1
    O1 --> O2
    O1 --> O3
```

## 五、Non-instanced LOD 流程

```mermaid
graph TB
    subgraph input4[输入]
        N1[non_instanced.glb]
    end

    subgraph simplify[网格简化]
        N2[NonInstancingLODManager generateLODFilesOnly]
        N3[meshopt 简化 non_instanced_lod_ratio]
        N4[non_instanced_LOD0 到 LODn.glb]
    end

    subgraph optional[可选实例化]
        N5{enable_non_instanced_lod_instancing}
        N6[每级 LOD 实例化检测 hlod 参数]
        N7{enable_non_instanced_lod_clustering}
        N8[Family Category 聚类]
    end

    subgraph output3[输出]
        N9[non_instance_lod_analysis.csv 几何简化统计]
        N10[non_instance_lod_instancing_analysis.csv InstancingRatio InstancingIncrease]
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

## 六、Quadtree HLOD 流程

```mermaid
graph TB
    subgraph prep[输入准备]
        Q1{mesh_segmentation}
        Q2[使用 segmented 输出]
        Q3[Split non_instanced.glb writeMeshesAsSeparateGlbs]
        Q4[单 mesh GLB 目录]
    end

    subgraph phase1[阶段1 空间划分]
        Q5[scanInputDirectory 扫描 GLB]
        Q6[buildQuadtree XZ 平面四叉树]
        Q7[recursiveSplit max_objects_per_tile]
        Q8[calculateTightBounds]
    end

    subgraph phase2[阶段2 自底向上]
        Q9[generateLeafTile mergeModel sourceStem]
        Q10[processParentTile]
        Q11[unpackInstancing 展开 EXT_mesh_gpu_instancing]
        Q12[mergeModel 子 tile]
        Q13[simplifyModel 0.5]
        Q14[SemanticMaterialGeometricDetector 语义几何实例化]
        Q15{enable_hlod_clustering}
        Q16[InstancingLODManager clusterInstancingResult]
    end

    subgraph output4[输出]
        Q17[tileset.json]
        Q18[hlod_analysis.csv Per-Tile Per-Level Summary]
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

## 七、语义映射保留机制

```mermaid
graph LR
    subgraph write[写入时保留]
        W1[writeNonInstancedMeshesOnly mesh.name 等于 glbStem 竖线 meshName]
        W2[writeMeshesAsSeparateGlbs mesh 名含竖线则不重复前缀]
        W3[mergeModel sourceStem Leaf tile 合并时添加 stem]
    end

    subgraph parse[查找时解析]
        R1[SemanticMaterialGeometricDetector mesh 名 find 竖线]
        R2[glbStem 前半部分 meshHashId 后半部分]
        R3[getSemanticInfo glbStem meshHashId]
    end

    W1 --> R1
    W2 --> R1
    W3 --> R1
    R1 --> R2
    R2 --> R3
```

## 八、输出目录结构

```mermaid
graph TB
    O[output_directory]

    subgraph dir1[01_instancing]
        I1[instanced.glb]
        I2[non_instanced.glb]
        I3[instanced.json non_instanced.json]
        I4[analysis instancing.csv]
        I5[analysis per_glb.csv]
    end

    subgraph dir2[02_instance_lod]
        L1[LOD1 到 LOD5.glb]
        L2[tileset.json]
        L3[analysis instance_lod.csv]
    end

    subgraph dir3[02_non_instance_lod]
        N1[non_instanced_LOD0 到 LODn.glb]
        N2[analysis non_instance_lod_analysis.csv]
        N3[instanced_lods 若启用实例化]
        N4[analysis non_instance_lod_instancing_analysis.csv]
    end

    subgraph dir4[03_hlod]
        H1[tiles 瓦片 GLB]
        H2[tileset.json]
        H3[hlod_analysis.csv]
    end

    O --> I1
    O --> L1
    O --> N1
    O --> H1
```

## 九、参数依赖关系

```mermaid
graph TB
    subgraph s1[Stage1参数]
        P1[similarity_thresholds]
        P2[semantic_hash_fields]
        P3[instance_limit]
        P4[hausdorff_max_sample_points]
        P5[material_filter_mode 材质过滤 none hash index]
        P6[allow_unknown_cross_mesh_clustering]
    end

    subgraph hlod[HLOD特有]
        P7[hlod_similarity_thresholds]
        P8[hlod_instance_limit]
        P9[enable_hlod_clustering]
    end

    subgraph noninst[NonInstLOD特有]
        P10[non_instanced_lod_similarity_thresholds]
        P11[enable_non_instanced_lod_instancing]
        P12[enable_non_instanced_lod_clustering]
    end

    subgraph instlod[InstanceLOD特有]
        P13[instance_lod_similarity_thresholds]
        P14[lod4_size_tolerance]
        P15[lod3_aspect_ratio_tolerance]
    end
```
