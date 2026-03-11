# GltfInstancingAndTilingTool 前端

本目录为 GltfInstancingAndTilingTool 的 Web 前端，基于 React + TypeScript + Vite 构建，用于与 C++ 核心工具配合，提供模型上传、参数配置与 CesiumJS 三维可视化展示。

## 架构

- **C++ 核心**：项目根目录下的 C++ 工具，负责 GLB 实例化检测、LOD 生成、HLOD 构建等。
- **Node.js 后端**：调用 C++ 可执行文件，接收上传的 GLB，返回处理后的 `tileset.json` 及瓦片。
- **本前端**：React + CesiumJS，提供上传界面、参数配置，并加载 3D Tiles 进行渲染。

## 与 C++ 后端的接口对应

C++ 工具支持配置文件与命令行参数，主要参数包括：

- `input_directory`：输入 GLB 目录
- `output_directory`：输出目录
- `similarity_thresholds`：相似度阈值（如 0.95,0.90,0.85,0.80,0.75）
- `semantic_hash_fields`：语义分组字段（category,family,type）
- `hausdorff_max_sample_points`：点云采样上限
- `instance_limit`：最小实例数

后端通过 `child_process` 调用 C++ 可执行文件，传入上述参数，输出 `01_instancing/instanced.glb`、`non_instanced.glb`、`tileset.json` 等。

## 开发

```bash
npm install
npm run dev
```

## 构建

```bash
npm run build
```

详见 [流程.md](流程.md) 了解完整集成流程。
