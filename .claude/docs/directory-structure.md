# 项目目录结构

```
/
├── Source/Sekiro/               # C++ 游戏源代码（SK 前缀）
│   ├── Animation/               # 动画系统
│   ├── Character/               # 角色系统
│   ├── Core/                    # 核心框架
│   ├── Movement/                # 移动系统
│   └── Weapon/                  # 武器系统
├── Content/                     # UE 资产
│   ├── Characters/              # 角色资产
│   ├── Weapons/                 # 武器资产
│   ├── Gameplay/                # 游戏性资产
│   ├── Input/                   # 输入配置
│   └── Script/                  # Blueprint/Lua 脚本资产
├── Plugins/                     # UE 插件（Sekiro 前缀）
│   ├── SekiroImport/            # JSON→UE 资产导入管线
│   ├── SekiroAIBridge/          # AI TCP 通信桥接
│   ├── UnLua/                   # Lua 脚本集成
│   └── UnLuaExtensions/         # UnLua 扩展
├── Script/                      # Python 脚本（Blender 导出、UE5 导入、管线编排、AIBridge）
│   ├── aibridge/bridge.py       # AIBridge 核心（与 .codex 共享同一份）
│   ├── parse_behavior_param.py  # BehaviorParam 管线脚本
│   ├── pie_test.py              # PIE 测试脚本
│   ├── list_assets.py           # 资产列表工具
│   └── temp/                    # 临时脚本（不提交）
├── Docs/                        # 项目文档
│   ├── breakdown/               # 需求拆分文档（/breakdown 产出）
│   ├── tech-designs/            # 技术方案文档（/tech-design 产出）
│   ├── gdd/                     # 游戏设计文档
│   ├── engine-reference/        # UE5.2 引擎参考（API 快照、技能库）
│   └── examples/                # 工作流示例
├── .claude/                     # Claude Code 配置
│   ├── CLAUDE.md                # 项目主入口指令
│   ├── settings.json            # 项目级权限
│   ├── settings.local.json      # 本地权限（不提交）
│   ├── agents/                  # 子代理定义
│   │   ├── plugin-programmer.md
│   │   ├── gameplay-programmer.md
│   │   ├── script-agent.md
│   │   └── review-agent.md
│   ├── skills/                  # 技能定义（/ 命令）
│   │   ├── breakdown/SKILL.md
│   │   ├── tech-design/SKILL.md
│   │   └── review/SKILL.md
│   ├── rules/                   # 路径特定规则
│   │   └── code-style.md
│   ├── docs/                    # 项目指引（供 CLAUDE.md @ 引用）
│   │   └── directory-structure.md
│   └── archive/                 # 旧配置存档
├── Tools/                       # 第三方工具（Yabber、DSAnimStudio 等）
├── Config/                      # UE 项目配置
├── Extracted/                   # Sekiro 原始数据导出（不提交）
├── Binaries/                    # 编译输出（不提交）
├── Intermediate/                # UE 构建中间产物（不提交）
└── Saved/                       # UE 运行时数据（不提交）
```
