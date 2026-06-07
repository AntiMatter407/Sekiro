---
name: 动画管线
description: Blender标准管线——FLVER直接导入+HKX→SMD动画转换，替代旧的自定义C#工具链
type: project
originSessionId: 550233ff-87bb-4074-8a41-bb53e0f0536c
---

## 管线方案（Blender 标准路线）

### 核心理念
放弃自定义 C# 转换工具，改用 FromSoftware 模组社区的标准工具链。

### 模型管线
```
FLVER (.flver) → Blender (io_scene_flver 插件) → FBX → UE5.2
```

### 动画管线
```
HKX (.hkx) → HKXPack(解压) → HavokTool(转换) → SMD → Blender → FBX → UE5.2
```

### 纹理管线
```
TPF/DDS → texconv → PNG/TGA → Blender材质 → FBX(嵌入) → UE5.2
```

## 工具清单

| 工具 | 用途 | 状态 |
|------|------|------|
| UXM 2.4 | 解包游戏档案 | ✅ Tools/UXM 2.4/ |
| Yabber 1.3.1 | BND4/DCX解包 | ✅ Tools/Yabber 1.3.1/ |
| io_scene_flver | Blender导入FLVER | ❌ 待安装 |
| HKXPack | 解压HKX动画文件 | ❌ 待获取 |
| HavokTool/HavokContentTools | HKX→SMD转换 | ❌ 待获取 |
| texconv | DDS纹理转换 | ✅ Tools/ |
| Blender 4.4 | 整合+FBX导出 | ✅ D:/Program Files (x86)/blender-4.4.0-windows-x64/ |

## 关键注意事项

### FLVER2格式
- Sekiro使用FLVER2格式，io_scene_flver对其有成熟支持
- 角色模型可能拆分多个FLVER（身体/腿/手臂/头/脸），需在Blender中合并
- 骨骼层级和蒙皮权重由插件自动处理

### HKX动画
- Sekiro使用Havok 2014格式的HKX
- HKXPack负责解压skeletal animation容器
- HavokTool转换为SMD（Valve Studiomdl Data）中间格式
- SMD可在Blender中导入并映射到FLVER骨骼

### 坐标系
- FromSoftware: Y-up, 右手坐标系
- Blender: Z-up (需在导入/导出时处理)
- UE5: Z-up, 左手坐标系
- FBX导出时设置正确的轴映射

## 旧管线（已废弃）
之前使用自定义C#工具链(HkxToFbx+SAP DLL)和Python脚本的管线已废弃。
该方案存在以下问题：
- 依赖修改版SAP DLL，维护成本高
- 自定义CombineParts合并逻辑复杂易出错
- 社区支持少，遇到问题难以求助
- Euler旋转顺序、骨骼合并等坑多

## 依赖
- Blender 4.4.0 (D:/Program Files (x86)/blender-4.4.0-windows-x64/)
- io_scene_flver (Blender插件)
- HKXPack + HavokTool (FromSoftware社区工具)
- texconv.exe (Microsoft DirectXTex)
