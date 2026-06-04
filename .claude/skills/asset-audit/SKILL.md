```markdown
---
name: asset-audit
description: "审核游戏资产是否符合命名约定、文件大小预算、格式标准和管线要求。识别孤立项（Orphaned Assets）、缺失引用和标准违规。"
argument-hint: "[category|all]"
user-invocable: true
allowed-tools: Read, Glob, Grep
---

当此技能被调用时：

1. **从相关设计文档和 CLAUDE.md 命名约定中读取美术圣经（Art Bible）或资产标准**。

2. **使用 Glob 扫描目标资产目录**：
   - `assets/art/**/*` — 美术资产
   - `assets/audio/**/*` — 音频资产
   - `assets/vfx/**/*` — 视觉特效（VFX）资产
   - `assets/shaders/**/*` — 着色器（Shader）
   - `assets/data/**/*` — 数据文件

3. **检查命名约定（Naming Conventions）**：
   - 美术：`[category]_[name]_[variant]_[size].[ext]`
   - 音频：`[category]_[context]_[name]_[variant].[ext]`
   - 所有文件必须为小写，使用下划线分隔

4. **检查文件标准**：
   - 纹理（Texture）：2 的幂次尺寸（Power-of-two），格式正确（UI 使用 PNG，3D 使用压缩格式），在大小预算内
   - 音频：采样率正确，格式正确（音效使用 OGG，音乐使用 OGG/MP3），在时长限制内
   - 数据：有效的 JSON/YAML，符合 Schema（模式定义）

5. **通过在代码中搜索对每个资产文件的引用，检查孤立项（Orphaned Assets）**。

6. **通过在代码中搜索资产引用并验证文件是否存在，检查缺失资产（Missing Assets）**。

7. **输出审核报告**：

```markdown
# Asset Audit Report -- [Category] -- [Date]

## Summary
- **Total assets scanned**: [N]
- **Naming violations**: [N]
- **Size violations**: [N]
- **Format violations**: [N]
- **Orphaned assets**: [N]
- **Missing assets**: [N]
- **Overall health**: [CLEAN / MINOR ISSUES / NEEDS ATTENTION]

## Naming Violations
| File | Expected Pattern | Issue |
|------|-----------------|-------|

## Size Violations
| File | Budget | Actual | Overage |
|------|--------|--------|---------|

## Format Violations
| File | Expected Format | Actual Format |
|------|----------------|---------------|

## Orphaned Assets (no code references found)
| File | Last Modified | Size | Recommendation |
|------|-------------|------|---------------|

## Missing Assets (referenced but not found)
| Reference Location | Expected Path |
|-------------------|---------------|

## Recommendations
[Prioritized list of fixes]
```
```
