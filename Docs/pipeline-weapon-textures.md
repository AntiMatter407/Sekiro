# 武器纹理导入管线

楔丸（Kusabimaru）从 Sekiro 解包文件到 UE5 材质资产的完整流程。

## 概述

```
Sekiro TPF (纹理包)
  → Yabber 解包 → .dds 文件 (DXT1/BC4/BC5/BC7)
  → texconv → .png (RGBA8)
  → bridge asset import_file → UE5 Texture2D
  → bridge python --file setup_materials.py --args config.json → Material + 赋到网格
```

## 步骤

### 1. DDS → PNG 转换

工具：`Tools/texconv.exe`（Microsoft DirectXTex）

```bash
# 单个文件
Tools/texconv.exe -ft png -y -o <输出目录> <输入.dds>

# 批量（使用封装脚本）
python Script/convert_dds_texconv.py <DDS目录> -o <PNG输出目录> -y
```

纹理格式映射：
| DDS 格式 | DXGI | 典型用途 |
|----------|------|----------|
| DXT1 (BC1) | 71 | 无 Alpha Albedo / 老法线 |
| ATI1 (BC4) | 80 | 单通道（Metallic/Roughness） |
| BC5_UNORM | 72 | 双通道法线 |
| BC7_UNORM | 98 | 高质量 Albedo/Normal（UE5 首选） |

### 2. PNG → UE5 Texture2D

**通过 bridge 导入**（推荐，不阻塞 GameThread）：

```bash
# bridge.py asset import_file <资产路径> <源文件绝对路径>
MSYS2_ARG_CONV_EXCL='*' python .claude/skills/aibridge/bridge.py asset import_file \
  "/Game/Weapons/Kusabimaru/Textures/WP_A_0300_a" \
  "F:/ProjectAI/Sekiro/Content/Weapons/Kusabimaru/Textures/WP_A_0300_a.png"
```

**备选：UE5 编辑器 Python 控制台**：
```python
exec(open(r"F:/ProjectAI/Sekiro/Script/weapon_textures_ue5.py", encoding="utf-8").read())
```

> `import_file` 是新增的 C++ 层桥接工具（`USKAssetTool::HandleImportFile`）。
> Python 的 `AssetImportTask.import_asset_tasks()` 在 bridge 中调用会导致 GameThread 死锁。

### 3. 创建材质 + 赋到网格

```bash
MSYS2_ARG_CONV_EXCL='*' python .claude/skills/aibridge/bridge.py python \
  --file "F:/ProjectAI/Sekiro/Script/setup_materials.py" \
  --args "F:/ProjectAI/Sekiro/Config/weapon_kusabimaru_materials.json"
```

该脚本会：
1. 创建 4 个 `Material` 资产（M_Blade / M_Tsuba / M_Decal / M_Sheath）
2. 用 `MaterialEditingLibrary` 添加 TextureSampleParameter2D 节点
3. 连接纹理参数到材质属性（BaseColor / Normal / AO / Roughness / Metallic）
4. 将材质赋到 `WP_A_0300_Kusabimaru` 的 4 个材质槽位

> **通用版**：`setup_materials.py` 支持 JSON 配置驱动，也可处理角色 MaterialInstance。
> 详见 `Docs/pipeline-materials-unified.md`。

## 材质槽位映射

| 槽位 | 材质 | Albedo | Normal | ORM |
|------|------|--------|--------|-----|
| [0] Decal | M_Decal | metalblend_a | metalblend_n | — |
| [1] Tsuba | M_Tsuba | WP_A_0300_2_a | WP_A_0300_2_n | — |
| [2] Blade | M_Blade | WP_A_0300_a | WP_A_0300_n | WP_A_0300_m (R=AO, G=Roughness, B=Metallic) |
| [3] Sheath | M_Sheath | WP_A_0300_2_a | WP_A_0300_2_n | — |

## 关键文件

| 文件 | 用途 |
|------|------|
| `Script/convert_dds_texconv.py` | DDS→PNG 转换封装脚本 |
| `Script/setup_materials.py` | 统一材质创建脚本（替代 setup_weapon_materials.py） |
| `Script/weapon_textures_ue5.py` | UE5 内批量导入 PNG 纹理 |
| `Config/weapon_kusabimaru_materials.json` | 楔丸材质配置 |
| `Config/character_sekiro_materials.json` | 角色材质配置 |
| `Docs/pipeline-materials-unified.md` | 统一材质管线完整文档 |
| `Tools/texconv.exe` | Microsoft DirectXTex 纹理转换工具 |
| `Plugins/SekiroAIBridge/.../USKAssetTool.cpp` | C++ `HandleImportFile` 实现 |

## Bridge 死锁说明

UE5 Python 的 `AssetImportTask` 在 bridge 调用时会死锁，原因：
- Bridge 将所有工具调用派发到 **GameThread**（`AsyncTask(ENamedThreads::GameThread, ...)`）
- Python `exec()` 在 GameThread 上同步执行
- `import_asset_tasks()` 内部等待 Slate 事件循环泵送
- GameThread 被 Python 阻塞 → 无法泵送事件 → 死锁

**解决方案**：`asset import_file` 在 C++ 层直接调用 `IAssetTools::ImportAssetTasks`，避开 Python→GameThread 的阻塞链路。材质创建（`MaterialEditingLibrary`）不走 `AssetImportTask`，因此可以通过 bridge Python `--file` 执行。
