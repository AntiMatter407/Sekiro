---
name: sekiro-asset-import
description: "从只狼原版游戏解包 → FLVER提取 → JSON生成 → SekiroImportCommandlet导入UE的完整管线。支持武器、角色等各类模型资产。"
argument-hint: "<资产类型> <部件名>"
user-invocable: true
allowed-tools: Bash, Read, Write, Edit, Glob, Grep, PowerShell, Agent
---

# Sekiro 资产导入技能

## 路径约定

| 含义 | 路径规则 |
|------|---------|
| UE 引擎目录 | `$UE_ENGINE_DIR`（`.claude/settings.local.json` → `env.UE_ENGINE_DIR`） |
| 只狼游戏目录 | `$SEKIRO_GAME_DIR`（`.claude/settings.local.json` → `env.SEKIRO_GAME_DIR`） |
| 项目根目录 | 当前工作目录 `$PROJECT_DIR` |
| 解包输出 | `$SEKIRO_GAME_DIR/parts/<部件名>-partsbnd-dcx/` |
| JSON 输出 | `$PROJECT_DIR/Extracted/<自定义名>_model.json` |
| 转换后 PNG | `$PROJECT_DIR/Content/Weapons/<武器名>/Textures/` |
| UE 导入路径 | `/Game/Weapons/<武器名>/` |

所有路径在执行时通过环境变量获取，禁止写死具体路径。

## 完整导入流程

> **前提**：`Tools/Yabber 1.3.1/Yabber.exe`、`Tools/FlverToFbx/FlverToFbx/bin/Release/net9.0-windows/FlverToFbx.exe`、`Tools/texconv.exe` 存在于项目 `Tools/` 目录下。

### 1. Yabber 解包

```bash
# 解包部件文件
"Tools/Yabber 1.3.1/Yabber.exe" "$SEKIRO_GAME_DIR/parts/<部件文件>.partsbnd.dcx"

# 解包纹理
"Tools/Yabber 1.3.1/Yabber.exe" "$SEKIRO_GAME_DIR/parts/<部件名>-partsbnd-dcx/parts/<类型>/<部件名>/<部件名>.tpf"

# 解包骨骼动画（获取 skeleton.hkx）
"Tools/Yabber 1.3.1/Yabber.exe" "$SEKIRO_GAME_DIR/parts/<部件名>-partsbnd-dcx/parts/<类型>/<部件名>/<部件名>.anibnd"
```

解包后文件定位规则：
- FLVER 文件：`$SEKIRO_GAME_DIR/parts/<部件名>-partsbnd-dcx/parts/<类型>/<部件名>/<部件名>.flver`
- 纹理 DDS：`$SEKIRO_GAME_DIR/parts/<部件名>-partsbnd-dcx/parts/<类型>/<部件名>/<部件名>-tpf/`
- 骨骼 HKX：`$SEKIRO_GAME_DIR/parts/<部件名>-partsbnd-dcx/parts/<类型>/<部件名>/<部件名>-anibnd/.../hkx_compendium/skeleton.hkx`

### 2. FlverToFbx 导出 JSON

```bash
"Tools/FlverToFbx/FlverToFbx/bin/Release/net9.0-windows/FlverToFbx.exe" \
  "<骨架.flver>" \
  "<身体1.flver>" [身体2.flver ...] \
  -o "Extracted/<自定义模型名>_model.json" \
  --skeleton-hkx "<skeleton.hkx路径>"
```

`-o` 参数决定输出 JSON 文件名和路径，也决定了后续资产命名的基准。

### 3. 修复骨骼顺序 + 设置 SkeletonName

```bash
python3 -c "
import json
d=json.load(open('Extracted/<模型名>_model.json'))
# 排序骨骼（父骨骼在前）
bones = d['Bones']
sorted_bones = []
added = set()
while len(sorted_bones) < len(bones):
    for i, b in enumerate(bones):
        if i in added: continue
        p = b.get('ParentName', '') or None
        if p is None or p in [x['Name'] for x in sorted_bones]:
            sorted_bones.append(b); added.add(i)
d['Bones'] = sorted_bones
# 设置 SkeletonName（决定 UE 中网格体资产名前缀）
d['SkeletonName'] = '<自定义模型名>'
json.dump(d, open('Extracted/<模型名>_model.json','w'), indent=2)
"
```

### 4. DDS → PNG 转换

```bash
for dds in "$SEKIRO_GAME_DIR/parts/<部件名>-partsbnd-dcx/parts/<类型>/<部件名>/<部件名>-tpf/"*.dds; do
  name=$(basename "$dds" .dds)
  "Tools/texconv.exe" -f R8G8B8A8_UNORM -o "$PROJECT_DIR/Content/Weapons/<武器名>/Textures" "$dds"
done
```

### 5. SekiroImportCommandlet 导入核心资产

```bash
# 停止编辑器
"$UE_ENGINE_DIR/Engine/Binaries/ThirdParty/Python3/Win64/python.exe" .claude/skills/aibridge/bridge.py editor stop

# 执行导入
"$UE_ENGINE_DIR/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "$PROJECT_DIR/Sekiro.uproject" \
  -run=SekiroImport \
  -Model="$PROJECT_DIR/Extracted/<模型名>_model.json" \
  -Output="/Game/Weapons/<武器名>" \
  -SkeletonName="<骨架资产名>" \
  -unattended
```

导入后可能需要重命名 SkeletalMesh（如果 `SkeletonName` 没设对）。

### 6. 导入纹理到 UE

```bash
# 启动编辑器
"$UE_ENGINE_DIR/Engine/Binaries/ThirdParty/Python3/Win64/python.exe" .claude/skills/aibridge/bridge.py editor start

# 逐个导入 PNG
"$UE_ENGINE_DIR/Engine/Binaries/ThirdParty/Python3/Win64/python.exe" .claude/skills/aibridge/bridge.py asset import_file \
  /Game/Weapons/<武器名>/Textures/<纹理名> \
  "$PROJECT_DIR/Content/Weapons/<武器名>/Textures/<纹理名>.png"
```

### 7. 创建 Blueprint 并配置

```bash
# 创建 BP，父类选择对应 C++ 基类
"$UE_ENGINE_DIR/Engine/Binaries/ThirdParty/Python3/Win64/python.exe" .claude/skills/aibridge/bridge.py blueprint create \
  /Game/Weapons/<武器名>/BP_<武器名> "/Script/Sekiro.SKWeapon"

# 设置 SkeletalMesh 引用
"$UE_ENGINE_DIR/Engine/Binaries/ThirdParty/Python3/Win64/python.exe" .claude/skills/aibridge/bridge.py blueprint set_property \
  /Game/Weapons/<武器名>/BP_<武器名> SkeletalMeshAsset \
  /Game/Weapons/<武器名>/WP_<武器名>.WP_<武器名> WeaponMesh

# 编译 BP
"$UE_ENGINE_DIR/Engine/Binaries/ThirdParty/Python3/Win64/python.exe" .claude/skills/aibridge/bridge.py blueprint compile \
  /Game/Weapons/<武器名>/BP_<武器名>
```

## 游戏部件文件查找规则

| 类型 | 游戏内路径 | 部件目录模式 | 类型目录 |
|------|-----------|-------------|---------|
| 角色 | `chr/c0000.chrbnd.dcx` | 走 `Sekiro_model.json` 整体导入 | — |
| 武器 | `parts/wp_a_<编号>.partsbnd.dcx` | `parts/Weapon/WP_A_<编号>/` | Weapon |
| 防具 | `parts/<部位>_m_<编号>.partsbnd.dcx` | `parts/FullBody/<部位>_M_<编号>/` | FullBody |

## 纹理后缀约定

| 后缀 | 语义 | UE 材质参数 |
|------|------|------------|
| `_a` | Albedo (BaseColor) | BaseColor |
| `_n` | Normal | Normal |
| `_m` | Metallic/Packed (R=AO, G=Roughness, B=Metallic) | ORM |
| `_r` | Roughness | Roughness |
| `_em` | Emissive | Emissive |
| `_mask` | Opacity Mask | Opacity Mask |
| `_metalblend_a` | 金属混合 Albedo | 第二层 BaseColor |
| `_metalblend_n` | 金属混合 Normal | 第二层 Normal |

## 完整示例：楔丸（Kusabimaru）

实际执行过的完整流程（武器编号 `wp_a_0300`，骨架名 `WP_A_0300_Kusabimaru`）：

```bash
# 1. 解包
"Tools/Yabber 1.3.1/Yabber.exe" "$SEKIRO_GAME_DIR/parts/wp_a_0300.partsbnd.dcx"
"Tools/Yabber 1.3.1/Yabber.exe" "$SEKIRO_GAME_DIR/parts/wp_a_0300-partsbnd-dcx/parts/Weapon/WP_A_0300/WP_A_0300.tpf"
"Tools/Yabber 1.3.1/Yabber.exe" "$SEKIRO_GAME_DIR/parts/wp_a_0300-partsbnd-dcx/parts/Weapon/WP_A_0300/WP_A_0300.anibnd"

# 2. 导出 JSON
"Tools/FlverToFbx/FlverToFbx/bin/Release/net9.0-windows/FlverToFbx.exe" \
  "$SEKIRO_GAME_DIR/parts/wp_a_0300-partsbnd-dcx/parts/Weapon/WP_A_0300/WP_A_0300.flver" \
  "$SEKIRO_GAME_DIR/parts/wp_a_0300-partsbnd-dcx/parts/Weapon/WP_A_0300/WP_A_0300_1.flver" \
  "$SEKIRO_GAME_DIR/parts/wp_a_0300-partsbnd-dcx/parts/Weapon/WP_A_0300/WP_A_0300_2.flver" \
  -o "Extracted/WP_A_0300_model.json" \
  --skeleton-hkx "$SEKIRO_GAME_DIR/parts/wp_a_0300-partsbnd-dcx/parts/Weapon/WP_A_0300/WP_A_0300-anibnd/parts/Weapon/WP_A_0300/hkx_compendium/skeleton.hkx"

# 3. 修复骨骼顺序 + SkeletonName
python3 -c "
import json
d=json.load(open('Extracted/WP_A_0300_model.json'))
bones = d['Bones']
sorted_bones = [b for b in bones if b.get('ParentName','') is None or b['ParentName']=='None']
for b in bones:
    if b not in sorted_bones: sorted_bones.append(b)
d['Bones'] = sorted_bones
d['SkeletonName'] = 'WP_A_0300_Kusabimaru'
json.dump(d, open('Extracted/WP_A_0300_model.json','w'), indent=2)
"
```
