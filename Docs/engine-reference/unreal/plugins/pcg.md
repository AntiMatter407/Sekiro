# Unreal Engine 5.7 — PCG（程序化内容生成，Procedural Content Generation）

**最后验证：** 2026-02-13
**状态：** 生产就绪（截至 UE 5.7）
**插件：** `PCG`（内置，在 Plugins 中启用）

---

## 概述

**程序化内容生成 (PCG)** 是 Unreal 的基于节点的框架，用于大规模生成程序化内容。它旨在为大型开放世界填充植被、岩石、道具、建筑和其他环境细节。

**适用场景：**
- 程序化植被放置（树木、草地、岩石）
- 基于生物群系的环境生成
- 道路/路径生成
- 建筑/结构放置
- 世界细节填充（道具、杂物）

**不适用场景：**
- 游戏逻辑（使用蓝图/C++）
- 一次性手动放置（使用编辑器工具）

**注意：** PCG 在 UE 5.0-5.6 中为实验性，在 UE 5.7 中成为生产就绪。

---

## 核心概念

### 1. **PCG Graph**（PCG 图）
- 基于节点的图（类似材质编辑器）
- 定义生成规则

### 2. **PCG Component**（PCG 组件）
- 放置在关卡中，执行 PCG Graph
- 在定义的体积内生成内容

### 3. **PCG Data**（PCG 数据）
- 点数据（位置、旋转、缩放）
- 样条线数据（路径、道路、河流）
- 体积数据（密度、生物群系遮罩）

### 4. **节点**
- **采样器 (Samplers)**：生成点（网格、泊松、表面）
- **过滤器 (Filters)**：根据规则移除点（密度、标签、边界）
- **修饰器 (Modifiers)**：变换点（偏移、旋转、缩放）
- **生成器 (Spawners)**：在点处实例化网格体/Actor

---

## 设置

### 1. 启用插件

`Edit > Plugins > PCG > Enabled > Restart`

### 2. 创建 PCG Volume

1. Place Actors > Volumes > PCG Volume
2. 缩放体积到所需的生成区域

### 3. 创建 PCG Graph

1. Content Browser > PCG > PCG Graph
2. 打开 PCG Graph 编辑器

---

## 基本工作流

### 示例：森林生成

#### 1. 创建 PCG Graph

**节点设置：**
```
Input (Volume)
  ↓
Surface Sampler (sample volume surface, points per m²: 0.5)
  ↓
Density Filter (use texture mask or noise)
  ↓
Static Mesh Spawner (tree meshes)
  ↓
Output
```

#### 2. 将图分配到 Volume

1. 选择 PCG Volume
2. Details Panel > PCG Component > Graph = 你的 PCG Graph
3. 点击 "Generate" 按钮

---

## 关键节点类型

### 采样器（点生成）

#### Grid Sampler（网格采样器）
- 规则网格点
- 配置：
  - **Grid Size**：点之间的距离
  - **Offset**：每个点的随机偏移

#### Poisson Disk Sampler（泊松盘采样器）
- 带最小距离的随机点
- 配置：
  - **Points Per m²**：密度
  - **Min Distance**：点之间的间距

#### Surface Sampler（表面采样器）
- 网格体表面或地形上的点
- 配置：
  - **Points Per m²**：密度
  - **Surface Only**：仅表面，非体积

---

### 过滤器（点移除）

#### Density Filter（密度过滤器）
- 根据密度值移除点
- 输入：纹理或噪声
- 用途：生物群系遮罩、空地、路径

#### Tag Filter（标签过滤器）
- 按标签过滤点
- 用途：条件生成

#### Bounds Filter（边界过滤器）
- 仅保留边界内的点
- 用途：将生成限制在特定区域

---

### 修饰器（点变换）

#### Rotate（旋转）
- 随机化点旋转
- 配置：
  - **Min/Max Rotation**：每轴旋转范围

#### Scale（缩放）
- 随机化点缩放
- 配置：
  - **Min/Max Scale**：缩放范围

#### Project to Ground（投射到地面）
- 将点吸附到地形表面

---

### 生成器（网格体/Actor 实例化）

#### Static Mesh Spawner（静态网格体生成器）
- 在点处生成静态网格体
- 配置：
  - **Mesh List**：网格体数组（随机选择）
  - **Culling Distance**：LOD/剔除设置

#### Actor Spawner（Actor 生成器）
- 在点处生成蓝图 Actor
- 用途：游戏玩法 Actor、交互对象

---

## 数据源

### Landscape（地形）
- 使用地形作为采样输入
- 自动投射到地形高度

### Splines（样条线）
- 沿样条线生成内容（道路、河流、路径）
- 示例：沿路径的树木

### Textures（纹理）
- 使用纹理作为密度遮罩
- 绘制生物群系、空地、区域

---

## 生物群系示例（混合森林）

### 图设置

```
Input (Landscape)
  ↓
Surface Sampler (density: 1.0)
  ↓
┌─────────────────┬─────────────────┐
│ Tree Biome      │ Rock Biome      │
│ (density > 0.5) │ (density < 0.5) │
├─────────────────┼─────────────────┤
│ Tree Spawner    │ Rock Spawner    │
└─────────────────┴─────────────────┘
  ↓
Merge
  ↓
Output
```

---

## 基于样条线的生成（带树木的道路）

### 1. 创建 PCG Graph

```
Spline Input
  ↓
Spline Sampler (sample along spline)
  ↓
Offset (offset from spline path)
  ↓
Tree Spawner
  ↓
Output
```

### 2. 向 PCG Volume 添加 Spline Component

1. PCG Volume > Add Component > Spline
2. 绘制样条线路径
3. PCG Graph 读取样条线数据

---

## 运行时生成

### 从 C++ 触发生成

```cpp
#include "PCGComponent.h"

UPCGComponent* PCGComp = /* 获取 PCG Component */;
PCGComp->Generate(); // 执行 PCG 图
```

### 流式生成（大型世界）

- PCG 自动与 World Partition 流式加载
- 仅在已加载的单元格中生成内容

---

## 性能

### 优化提示

- 对生成的网格体使用 **剔除距离**（LOD）
- 限制 **密度**（更少的点 = 更好的性能）
- 对重复网格体使用 **层级实例化静态网格体 (HISM)**
- 大型世界启用 **流式加载**

### 调试性能

```cpp
// 控制台命令：
// pcg.graph.debug 1 - 显示 PCG 调试信息
// stat pcg - 显示 PCG 性能统计
```

---

## 常用模式

### 带空地的森林

```
Surface Sampler
  ↓
Density Filter (noise texture with clearings)
  ↓
Tree Spawner (pine, oak, birch)
```

---

### 陡坡上的岩石

```
Landscape Input
  ↓
Surface Sampler
  ↓
Slope Filter (angle > 30°)
  ↓
Rock Spawner
```

---

### 道路两侧的道具

```
Spline Input (road spline)
  ↓
Spline Sampler
  ↓
Offset (side of road)
  ↓
Street Light Spawner
```

---

## 调试

### PCG 调试可视化

```cpp
// 控制台命令：
// pcg.debug.display 1 - 显示点和生成边界
// pcg.debug.colormode points - 按颜色编码点
```

### 图调试

- PCG Graph 编辑器 > Debug > Show Debug Points
- 可视化图中每个节点的点

---

## 从 UE 5.6（实验性）迁移到 5.7（生产就绪）

### API 变更

```cpp
// ❌ 旧版（5.6 实验性 API）：
// 部分节点重命名，API 不稳定

// ✅ 新版（5.7 生产 API）：
// 稳定的节点类型，有文档的 API
```

**迁移：** 使用稳定的 5.7 节点重建 PCG 图。充分测试。

---

## 限制

- **不适用于游戏逻辑**：游戏规则请使用蓝图/C++
- **大型图可能较慢**：通过过滤器和降低密度来优化
- **运行时生成开销**：尽可能预生成

---

## 来源
- https://docs.unrealengine.com/5.7/en-US/procedural-content-generation-in-unreal-engine/
- https://docs.unrealengine.com/5.7/en-US/pcg-quick-start-in-unreal-engine/
- UE 5.7 发行说明（PCG 生产就绪公告）
