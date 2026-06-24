---
name: sekiro-asset-import
description: "Sekiro 资产导入管线：解包 → FLVER → JSON → UE Commandlet"
argument-hint: "<AssetName> [--original <游戏原名>]"
user-invocable: true
allowed-tools: Bash, Read, Write, Edit, Glob, Grep, PowerShell, Agent
---

# Sekiro 资产导入

> **完整文档**: `Docs/sekiro-asset-import.md`

## 核心流程

1. **解包** — `Yabber.exe` 解 .dcx/.tpf → 得到 .flver/.hkx/.dds
2. **JSON** — `python -m sekiro_asset_manager model import <AssetName>`
3. **UE 导入** — `UnrealEditor-Cmd.exe -run=SekiroImport -Model=...`
4. **蓝图** — `bridge.py blueprint create`

## 工具位置

`Script/sekiro_asset_manager/ext_tools/`（`pipeline_config.py` 自动定位）

## 路径约定

- **禁止硬编码路径** — 全部从 `pipeline_config.py` 读取
- 解包 → `Extracted/`（不入 git）
- 输出 → `Output/<AssetName>/`（不入 git）
- UE → `/Game/Characters/<AssetName>/`

详见 `Docs/sekiro-asset-import.md`