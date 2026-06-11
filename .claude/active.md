# active — Sekiro → UE5.2 迁移进度

> **active.md 是本项目唯一的进度文件。**

## 当前阶段
SekiroImport C++ 插件全部完成。下一步：UE5.2 项目创建 & UnLua 集成。

## 方案状态

| 方案 | 状态 | 说明 |
|------|------|------|
| 方案A: Blender FLVER→FBX | ❌ 已取消 (2026-06-08) | 已被方案B取代 |
| 方案B: SekiroImport C++ | ✅ 完成 (2026-06-11) | JSON→UE资产直接导入，5阶段全部完成 |

## SekiroImport 实施总结

| Phase | 内容 | 状态 |
|-------|------|------|
| Phase 0 | Python可调用验证框架 | ✅ 完成 |
| Phase 1 | 骨架构建对齐 | ✅ 完成 (147/147骨骼<0.05cm) |
| Phase 2 | 网格构建对齐 | ✅ 完成 (顶点对齐<0.001mm) |
| Phase 3 | 动画构建对齐 | ✅ 完成 (3-Pass FK+OrientQ+DeriveLocal) |
| Phase 4 | 材质构建对齐 | ✅ 完成 (MTD→BlendMode推导+父材质+46MIC) |
| Phase 5 | 管线编排 | ✅ 完成 (2026-06-11 代码优化) |

## 待办
- [ ] UE5.2 项目创建 & UnLua 集成
- [ ] 核心战斗系统设计 (拼刀/架势条/忍杀)
- [ ] 角色移动系统 (钩绳/潜行/游泳)

## 已完成
- 2026-05-30: 项目启动，技术栈确定（UE5.2 + C++ + UnLua）
- 2026-06-03: SekiroImport Phase 0+1+2 完成
- 2026-06-08: 方案A取消；Phase 3+4 完成
- 2026-06-11: Phase 5 完成 + 管线代码优化 + .claude/ 精简

## 阻塞项
- 无
