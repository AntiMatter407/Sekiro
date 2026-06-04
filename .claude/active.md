# active — Sekiro → UE5.2 迁移进度

> **active.md 是本项目唯一的进度文件。** 其他文件（memory/、docs/ 等）仅作参考，进度以本文件为准。

## 当前阶段
Blender FLVER→FBX 管线开发

## 正在进行
- [ ] **UE5 中验证 BD_M_9000_Built.fbx**（骨骼是否与模型对齐）

## Blender FLVER→FBX 管线 (2026-06-01)

### 工具链
```
UXM解包 → Yabber解BND4 → .NET工具(FlverToFbx)导出JSON → Blender脚本(build_from_json.py)构建FBX → UE5导入
```

### 文件清单
| 文件 | 路径 | 说明 |
|------|------|------|
| C# 工具 | `Tools/FlverToFbx/FlverToFbx/Program.cs` | 读取 skeleton+body FLVER，计算世界变换，输出JSON |
| Blender 脚本 | `Tools/FlverToFbx/build_from_json.py` | 读取JSON，创建骨架+网格，导出FBX |
| JSON 数据 | `Extracted/BD_M_9000_blender.json` | 中间数据（37骨骼, 27网格, 20,647顶点） |
| 输出 FBX | `Extracted/BD_M_9000_Built.fbx` | 最终产物 |

### 已修复的关键 Bug
1. **`mesh.BoneIndices` 为空** — Sekiro FLVER 没有骨骼调色板，顶点骨骼索引直接引用 FLVER Nodes。C# 工具添加了回退逻辑。
2. **顶点权重全部丢失** — `vgroup.add()` 全部用 `'REPLACE'` 导致只有最后一个骨骼的权重生效。改为：第一个 `REPLACE`，后续 `ADD`。
3. **骨骼链不连通** — 骨骼尾部距离用 `距离*0.5`，导致父子骨骼不连接。改为：尾部直接指向最近子骨骼头部。
4. **过多无用骨骼** — 从 467 减少到 37（仅网格实际引用的骨骼 + 层级祖先）。
5. **变换计算不一致** — C# 和 Python 各自计算世界变换，可能产生差异。改为 C# 统一计算 WorldPos/WorldRot，Blender 直接使用。

### 当前状态
- 骨架：37 根骨骼（Master → RootPos → RootRotY → RootRotXZ → Spine → Spine1 → Spine2 为主链）
- 网格：27 个子网格，20,647 顶点，111,006 三角形
- 骨骼变换：使用 skeleton FLVER 层级 + body FLVER 局部变换（37根骨骼中两者一致）
- **待 UE5 验证**

### FLVER 数据发现
- Skeleton FLVER (c0000): 467 骨骼，有层级关系（ParentIndex）
- Body FLVER (BD_M_9000): 172 骨骼，大部分 ParentIndex=-1（扁平），有正确的 rest-pose 变换
- 172 根 body 骨骼中，154 根在 skeleton 中存在，18 根为 body 专用（BD_M_*, cloth, collision 等）
- 实际被顶点权重引用的骨骼只有 30 根（+ 7 祖先 = 37）
- 80 根 body 骨骼的变换与 skeleton 不同，但这 80 根都未被身体网格使用

## 待办
- [ ] UE5 验证 BD_M_9000_Built.fbx
- [ ] 提取其他身体部件（手臂 AM_M_9000, 腿部 LG_M_9000, 头部 HD_M_9510）
- [ ] 动画管线（HKX → FBX → UE5）
- [ ] UE5.2 项目创建 & UnLua 集成
- [ ] 核心战斗系统设计

## 已完成
- 2026-05-30: 项目启动，技术栈确定（UE5.2 + C++ + UnLua）
- 2026-05-30: 目录结构确定
- 2026-05-30: 动画管线运行成功（771个DAE，11分类）
- 2026-05-31: 角色模型合并管线建立（5部件→OBJ+JSON→FBX）
- 2026-05-31: 骨骼位置计算修复
- 2026-05-31: 贴图全量转换 & 材质全覆盖
- 2026-06-01: FlverToFbx 管线建立（C# + Blender 直接处理 FLVER）
- 2026-06-01: 修复 5 个关键 bug（骨骼调色板、权重覆盖、骨骼链、骨骼过滤、变换计算）

## 阻塞项
- BD_M_9000_Built.fbx 骨骼与模型对齐待 UE5 验证
