# 配置说明（Strict / Moderate / Lenient）

本目录提供三套实例化策略配置，用于在“实例化率”与“误合并风险”之间做权衡：

- `config_01_strict.txt`：严格策略（保守）
- `config_02_moderate.txt`：中等策略（平衡，推荐先跑）
- `config_03_lenient.txt`：宽松策略（激进）

---

## 推荐使用顺序

建议按以下顺序执行并对照结果：

1. 先跑 `config_02_moderate.txt` 作为基线
2. 再跑 `config_01_strict.txt`（检查误合并是否进一步降低）
3. 最后跑 `config_03_lenient.txt`（观察实例化率上限与潜在误合并）

这样可以快速得到“保守-平衡-激进”三档对比，便于选定最终策略。

---

## 适用场景

### 1) Strict（`config_01_strict.txt`）

适合：

- 对结果准确性要求高，误合并代价大（例如构件语义必须精确）
- 需要更稳健的论文/报告主结果
- 数据中存在大量外观相近但不应合并的构件

特征：

- 更高相似度阈值（更难被判为同类）
- 更严格材质约束（`material_filter_mode = hash`）
- 更高 `instance_limit`（小规模组不轻易实例化）
- 不允许 `Unknown` 跨 mesh 聚类

---

### 2) Moderate（`config_02_moderate.txt`）

适合：

- 日常主流程与默认实验
- 希望在实例化率与正确性之间取得平衡
- 用于与 Strict/Lenient 做对照的基线配置

特征：

- 阈值和限制使用中间档
- 材质约束为 `index`（比 `none` 严、比 `hash` 松）
- 保持语义与几何检查开启

---

### 3) Lenient（`config_03_lenient.txt`）

适合：

- 优先追求高实例化率、减少 unique meshes
- 需要探索“性能上限”或压缩潜力
- 用于敏感性分析（观察宽松策略带来的收益和风险）

特征：

- 更低相似度阈值（更容易合并）
- 更弱材质约束（`material_filter_mode = none`）
- `instance_limit` 更低
- 允许 `Unknown` 跨 mesh 聚类

---

## 关键参数对比（简表）

| 参数 | Strict | Moderate | Lenient | 影响 |
|---|---:|---:|---:|---|
| `instance_limit` | 4 | 3 | 2 | 越小越容易形成实例组 |
| `similarity_thresholds` | 高 | 中 | 低 | 越低越容易合并 |
| `semantic_hash_fields` | `category,family,type` | `category,family,type` | `category,family` | 字段越少分组越粗、更易合并 |
| `material_filter_mode` | `hash` | `index` | `none` | 约束越弱越易合并 |
| `allow_unknown_cross_mesh_clustering` | `false` | `false` | `true` | 开启后 Unknown 更易聚类 |
| `lod4_size_tolerance` | 0.03 | 0.05 | 0.08 | 越大越宽松 |
| `lod3_aspect_ratio_tolerance` | 0.12 | 0.20 | 0.30 | 越大越宽松 |

---

## 结果解读建议

建议重点比较以下输出指标：

- Instancing Ratio
- Instancing Increase
- Instancing Increase Ratio

如果 Lenient 提升明显但出现可见错误（误合并），可回退到 Moderate；若 Moderate 仍有误合并，优先采用 Strict。

