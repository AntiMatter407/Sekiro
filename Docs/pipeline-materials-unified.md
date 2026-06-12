# 统一材质导入管线

从 DDS 纹理到 UE5 材质资产的完整流程。支持武器（新建 Material）和角色（MaterialInstance）两种模式。

## 概述

```
Sekiro TPF (纹理包)
  → Yabber 解包 → .dds 文件 (DXT1/BC4/BC5/BC7)
  → texconv → .png (RGBA8)
  → bridge asset import_file → UE5 Texture2D
  → bridge python --file setup_materials.py --args <config.json>
    → Material 或 MaterialInstance + 纹理绑定 + 赋到网格
```

## 快速开始

### 新武器导入

```bash
# 1. DDS → PNG
python Script/convert_dds_texconv.py <DDS目录> -o <PNG输出目录> -y

# 2. PNG → Texture2D（逐个或批量）
MSYS2_ARG_CONV_EXCL='*' python .claude/skills/aibridge/bridge.py asset import_file \
  "/Game/Weapons/MyWeapon/Textures/my_tex_a" \
  "F:/ProjectAI/Sekiro/Content/Weapons/MyWeapon/Textures/my_tex_a.png"

# 3. 创建材质 + 赋到网格（一次搞定）
MSYS2_ARG_CONV_EXCL='*' python .claude/skills/aibridge/bridge.py python \
  --file "F:/ProjectAI/Sekiro/Script/setup_materials.py" \
  --args "F:/ProjectAI/Sekiro/Config/weapon_kusabimaru_materials.json"
```

### 角色材质实例更新

```bash
MSYS2_ARG_CONV_EXCL='*' python .claude/skills/aibridge/bridge.py python \
  --file "F:/ProjectAI/Sekiro/Script/setup_materials.py" \
  --args "F:/ProjectAI/Sekiro/Config/character_sekiro_materials.json"
```

## JSON 配置格式

### Material 模式（武器）

用于创建独立 UMaterial，自动添加 TextureSampleParameter2D 节点并连接。

```json
{
  "pipeline": {"type": "material", "description": "描述"},
  "target": {
    "mesh": "/Game/Weapons/.../SK_Weapon",
    "material_dir": "/Game/Weapons/.../Materials",
    "texture_dir": "/Game/Weapons/.../Textures"
  },
  "materials": [
    {
      "name": "M_Blade",
      "slot": 2,
      "textures": [
        {
          "texture": "tex_albedo",
          "param": "BaseColor",
          "bindings": [{"property": "BaseColor", "channel": "RGB"}]
        }
      ]
    }
  ]
}
```

**bindings.property** 支持的值：
`BaseColor`, `Metallic`, `Specular`, `Roughness`, `EmissiveColor`, `Opacity`, `OpacityMask`, `Normal`, `AmbientOcclusion`

**bindings.channel** 支持的值：
`R`, `G`, `B`, `A`, `RGB`

### Instance 模式（角色）

用于创建/更新 UMaterialInstanceConstant，覆盖父材质的纹理参数。

```json
{
  "pipeline": {
    "type": "instance",
    "description": "描述",
    "skip_mesh_assign": true
  },
  "target": {
    "mesh": "/Game/Characters/.../SK_Character",
    "material_dir": "/Game/Characters/.../Materials",
    "texture_dir": "/Game/Characters/.../Textures"
  },
  "materials": [
    {
      "name": "MI_Body",
      "parent": "M_CharacterMaster",
      "slot": 0,
      "overrides": [
        {"parameter": "_a", "texture": "body_albedo"},
        {"parameter": "_n", "texture": "body_normal"}
      ],
      "blend_mode": "Masked",
      "two_sided": true
    }
  ]
}
```

**字段说明：**
| 字段 | 必填 | 说明 |
|------|------|------|
| `name` | 是 | 材质/实例资产名 |
| `slot` | material模式必填 | 网格材质槽位索引 |
| `parent` | instance模式必填 | 父 Material 资产名 |
| `overrides[].parameter` | 是 | 父材质中 TextureParameter2D 的 ParameterName |
| `overrides[].texture` | 是 | 覆盖纹理资产名 |
| `blend_mode` | 否 | Opaque / Masked / Translucent / Additive 等 |
| `two_sided` | 否 | 双面渲染 |
| `skip_mesh_assign` | 否 | 跳过网格材质赋值（角色已有分配时设为 true） |

## 材质槽位参考

### 楔丸 (Kusabimaru)

| 槽位 | 材质 | Albedo | Normal | ORM |
|------|------|--------|--------|-----|
| [0] Decal | M_Decal | metalblend_a | metalblend_n | — |
| [1] Tsuba | M_Tsuba | WP_A_0300_2_a | WP_A_0300_2_n | — |
| [2] Blade | M_Blade | WP_A_0300_a | WP_A_0300_n | WP_A_0300_m |
| [3] Sheath | M_Sheath | WP_A_0300_2_a | WP_A_0300_2_n | — |

### Sekiro 角色

角色为单 SkeletalMesh，约 35-40 材质槽位，按身体部位分组：

| 部位 | 前缀 | 材质实例示例 | 槽位范围 |
|------|------|-------------|----------|
| 躯干 | BD | MI_BD_M_9000_body, tops1l, muffler, Court | 0-14 |
| 义手 | AM | MI_AM_M_9000_tops, body, artificialarm | 15-24 |
| 腿部 | LG | MI_LG_M_9000_bottoms1, bottoms2, fray1 | 25-31 |
| 面部 | FC | MI_FC_M_0100_ck, FC_M_0100_Eye, hair02 | 32-39 |
| 头部 | HD | MI_HD_M_9510/9520 | 40-41 |

纹理命名规范：`{前缀}_{性别}_{网格ID}_{描述}_{通道}`
通道后缀：`_a` (Albedo), `_n` (Normal), `_m` (ORM packed), `_r` (Roughness)

## 关键文件

| 文件 | 用途 |
|------|------|
| `Script/setup_materials.py` | 统一材质创建脚本（material + instance 模式） |
| `Script/convert_dds_texconv.py` | DDS→PNG 批量转换 |
| `Config/weapon_kusabimaru_materials.json` | 楔丸材质配置模板 |
| `Config/character_sekiro_materials.json` | 角色材质配置模板 |
| `Tools/texconv.exe` | Microsoft DirectXTex 纹理转换工具 |
| `Docs/pipeline-weapon-textures.md` | 纹理导入详细步骤（DDS→PNG→Texture2D） |

## 复用指南

导入新资产只需 3 步：

1. **转换纹理**：`python Script/convert_dds_texconv.py <DDS目录> -o <PNG目录> -y`
2. **导入纹理**：逐个 `bridge asset import_file`（或 UE5 控制台运行 `import_textures.py`）
3. **创建 JSON 配置**：复制 `Config/weapon_kusabimaru_materials.json` 为模板，修改路径和纹理名
4. **运行**：`bridge python --file setup_materials.py --args <新配置.json>`

> Bridge 的 `asset import_file` 避免了 Python `AssetImportTask` 的 GameThread 死锁问题。
> 详见 `Docs/pipeline-weapon-textures.md` 中「Bridge 死锁说明」章节。
