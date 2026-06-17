# 路径特定规则 (Path-Specific Rules)

`.claude/rules/` 中的规则在编辑匹配路径的文件时自动生效：

| 规则文件 | 路径模式 | 强制要求 |
| ---- | ---- | ---- |
| `gameplay-code.md` | `src/gameplay/**` | 数据驱动值、增量时间 (delta time)、无 UI 引用 |
| `engine-code.md` | `src/core/**` | 热路径零分配、线程安全、API 稳定性 |
| `ai-code.md` | `src/ai/**` | 性能预算、可调试性、数据驱动参数 |
| `network-code.md` | `src/networking/**` | 服务端权威 (server-authoritative)、版本化消息、安全性 |
| `ui-code.md` | `src/ui/**` | 不持有游戏状态所有权、本地化就绪、无障碍访问 (accessibility) |
| `design-docs.md` | `Docs/gdd/**` | 必须包含 8 个章节、公式格式、边界情况 (edge cases) |
| `narrative.md` | `Docs/narrative/**` | 世界观一致性、角色语调、正典层级 (canon levels) |
| `data-files.md` | `assets/data/**` | JSON 有效性、命名约定、Schema 规则 |
| `test-standards.md` | `tests/**` | 测试命名、覆盖率要求、Fixture 模式 |
| `prototype-code.md` | `prototypes/**` | 宽松标准、需要 README、假设已记录 |
| `shader-code.md` | `assets/shaders/**` | 命名约定、性能目标、跨平台规则 |
