# 目录结构 (Directory Structure)

```text
/
├── CLAUDE.md                    # 主配置文件 (Master configuration)
├── .claude/                     # 代理定义、技能、钩子、规则、文档
├── Source/                      # C++ 游戏源代码
├── Content/                     # UE 资产（美术、音频、特效、着色器、数据）
├── Docs/                        # 所有项目文档（GDD、架构、技术参考、复盘报告）
│   ├── gdd/                     # 游戏设计文档（GDD、叙事、关卡、平衡性）
│   └── engine-reference/        # 精选的引擎 API 快照（版本锁定，仅 UE）
├── Saved/                       # UE 运行时产出 + 日志、录像、截屏
├── Script/                      # 项目自有 Python 脚本（Blender/UE5/管线/诊断）
│   └── temp/                     # 临时脚本和诊断输出（一次性使用，不提交到主线）
├── Tools/                       # 第三方工具 + C# 工具链（Yabber、Noesis、FlverToFbx C#等）
├── Tests/                       # 测试套件（单元测试、集成测试、性能测试、试玩测试）
└── .claude/                     # 代理定义、技能、钩子、规则、文档
    └── active.md                # 当前工作状态（唯一的进度文件）
```
