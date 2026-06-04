# 代理协调与委派地图

## 组织层级

```
                           [人类开发者]
                                 |
                 +---------------+---------------+
                 |               |               |
       creative-director  technical-director  producer
                 |               |               |
        +--------+--------+     |        (统筹全局)
        |        |        |     |
  game-designer art-dir  narr-dir  lead-programmer  qa-lead  audio-dir
        |        |        |         |                |        |
     +--+--+     |     +--+--+  +--+--+--+--+--+   |        |
     |  |  |     |     |     |  |  |  |  |  |  |   |        |
    sys lvl eco  ta   wrt  wrld gp ep  ai net tl ui qa-t    snd
                                 |
                             +---+---+
                             |       |
                          perf-a   devops   analytics

  额外负责人（向 producer/directors 汇报）:
    release-manager         -- 发布管线、版本管理、部署
    localization-lead       -- 国际化（i18n）、字符串表、翻译管线
    prototyper              -- 快速可丢弃原型、概念验证
    security-engineer       -- 反作弊、漏洞利用、数据隐私、网络安全
    accessibility-specialist -- WCAG、色盲模式、按键重映射、文本缩放
    live-ops-designer       -- 赛季、活动、战斗通行证、留存、实时经济
    community-manager       -- 补丁说明、玩家反馈、危机沟通

  引擎专家（Unreal Engine 5）:
    unreal-specialist  -- UE5 负责人：Blueprint/C++、GAS 总览、UE 子系统
      ue-gas-specialist         -- GAS：技能、效果、属性、标签、预测
      ue-blueprint-specialist   -- Blueprint：BP/C++ 边界、图表规范、优化
      ue-replication-specialist -- 联网：复制、RPC、预测、带宽
      ue-umg-specialist         -- UI：UMG、CommonUI、控件层级、数据绑定
```

### 图例
```
sys  = systems-designer       gp  = gameplay-programmer
lvl  = level-designer         ep  = engine-programmer
eco  = economy-designer       ai  = ai-programmer
ta   = technical-artist       net = network-programmer
wrt  = writer                 tl  = tools-programmer
wrld = world-builder          ui  = ui-programmer
snd  = sound-designer         qa-t = qa-tester
narr-dir = narrative-director perf-a = performance-analyst
art-dir = art-director
```

## 委派规则

### 谁可以向谁委派

| 来源 | 可委派给 |
|------|----------------|
| creative-director | game-designer、art-director、audio-director、narrative-director |
| technical-director | lead-programmer、devops-engineer、performance-analyst、technical-artist（技术决策） |
| producer | 任意代理（仅限其领域内的任务分配） |
| game-designer | systems-designer、level-designer、economy-designer |
| lead-programmer | gameplay-programmer、engine-programmer、ai-programmer、network-programmer、tools-programmer、ui-programmer |
| art-director | technical-artist、ux-designer |
| audio-director | sound-designer |
| narrative-director | writer、world-builder |
| qa-lead | qa-tester |
| release-manager | devops-engineer（发布构建）、qa-lead（发布测试） |
| localization-lead | writer（字符串审查）、ui-programmer（文本适配） |
| prototyper | （独立工作，向 producer 和相关负责人汇报发现） |
| security-engineer | network-programmer（安全审查）、lead-programmer（安全模式） |
| accessibility-specialist | ux-designer（无障碍模式）、ui-programmer（实现）、qa-tester（无障碍测试） |
| [engine]-specialist | 引擎子专家（委派子系统特定工作） |
| [engine] 子专家 | （为所有程序员提供引擎子系统模式和优化建议） |
| live-ops-designer | economy-designer（实时经济）、community-manager（活动沟通）、analytics-engineer（参与度指标） |
| community-manager | （与 producer 协调审批，与 release-manager 协调补丁说明发布时机） |

### 上报路径

| 情况 | 上报至 |
|-----------|------------|
| 两位设计师对机制有分歧 | game-designer |
| 游戏设计与叙事冲突 | creative-director |
| 游戏设计与技术可行性冲突 | producer（协调），然后 creative-director + technical-director |
| 美术与音频风格冲突 | creative-director |
| 代码架构分歧 | technical-director |
| 跨系统代码冲突 | lead-programmer，然后 technical-director |
| 部门间排期冲突 | producer |
| 范围超出产能 | producer，然后 creative-director 裁剪范围 |
| 质量关卡分歧 | qa-lead，然后 technical-director |
| 性能预算违规 | performance-analyst 标记，technical-director 决策 |

## 常见工作流模式

### 模式 1：新功能（完整流程）

```
1. creative-director  -- 审批功能概念是否符合愿景
2. game-designer      -- 创建包含完整规格的设计文档
3. producer           -- 排定工期，识别依赖
4. lead-programmer    -- 设计代码架构，创建接口草图
5. [专业程序员]       -- 实现功能
6. technical-artist   -- 实现视觉效果（如需要）
7. writer             -- 创建文本内容（如需要）
8. sound-designer     -- 创建音频事件列表（如需要）
9. qa-tester          -- 编写测试用例
10. qa-lead           -- 审查并批准测试覆盖率
11. lead-programmer   -- 代码审查
12. qa-tester         -- 执行测试
13. producer          -- 标记任务完成
```

### 模式 2：Bug 修复

```
1. qa-tester          -- 通过 /bug-report 提交 Bug 报告
2. qa-lead            -- 分级严重性和优先级
3. producer           -- 分配至 Sprint（如非 S1）
4. lead-programmer    -- 定位根因，分配给程序员
5. [专业程序员]       -- 修复 Bug
6. lead-programmer    -- 代码审查
7. qa-tester          -- 验证修复并执行回归测试
8. qa-lead            -- 关闭 Bug
```

### 模式 3：平衡性调整

```
1. analytics-engineer -- 从数据（或玩家反馈）中识别不平衡
2. game-designer      -- 对照设计意图评估问题
3. economy-designer   -- 建模调整方案
4. game-designer      -- 审批新数值
5. [数据文件更新]     -- 修改配置数值
6. qa-tester          -- 对受影响系统执行回归测试
7. analytics-engineer -- 监控变更后指标
```

### 模式 4：新区域/关卡

```
1. narrative-director -- 定义区域的叙事目的和节拍
2. world-builder      -- 创建世界观和环境背景
3. level-designer     -- 设计布局、遭遇战、节奏
4. game-designer      -- 审查遭遇战的机制设计
5. art-director       -- 定义区域的视觉方向
6. audio-director     -- 定义区域的音频方向
7. [相关程序员和美术师实现]
8. writer             -- 创建区域专用文本内容
9. qa-tester          -- 测试完整区域
```

### 模式 5：Sprint 周期

```
1. producer           -- 通过 /sprint-plan new 规划 Sprint
2. [所有代理]         -- 执行分配的任务
3. producer           -- 通过 /sprint-plan status 每日汇报
4. qa-lead            -- Sprint 期间持续测试
5. lead-programmer    -- Sprint 期间持续代码审查
6. producer           -- 通过 post-sprint hook 执行 Sprint 回顾
7. producer           -- 结合经验教训规划下一个 Sprint
```

### 模式 6：里程碑检查点

```
1. producer           -- 运行 /milestone-review
2. creative-director  -- 审查创意进度
3. technical-director -- 审查技术健康状况
4. qa-lead            -- 审查质量指标
5. producer           -- 主持推进/暂停讨论
6. [所有总监]         -- 在需要时就范围调整达成一致
7. producer           -- 记录决策并更新计划
```

### 模式 7：发布管线

```text
1. producer             -- 宣布发布候选版本，确认里程碑标准已达成
2. release-manager      -- 切出发布分支，生成 /release-checklist
3. qa-lead              -- 执行完整回归测试，签署质量确认
4. localization-lead    -- 验证所有字符串已翻译，文本适配通过
5. performance-analyst  -- 确认性能基准达标
6. devops-engineer      -- 构建发布产物，运行部署管线
7. release-manager      -- 生成 /changelog，打标签，创建发布说明
8. technical-director   -- 对主要发布进行最终签署
9. release-manager      -- 部署并监控 48 小时
10. producer            -- 标记发布完成
```

### 模式 8：快速原型

```text
1. game-designer        -- 定义假设和成功标准
2. prototyper           -- 通过 /prototype 搭建原型
3. prototyper           -- 构建最小实现（以小时计，非以天计）
4. game-designer        -- 对照标准评估原型
5. prototyper           -- 记录发现报告
6. creative-director    -- 做出是否进入制作阶段的推进/暂停决策
7. producer             -- 如获批准则排定制作工期
```

### 模式 9：实时活动 / 赛季发布

```text
1. live-ops-designer     -- 设计活动/赛季内容、奖励、排期
2. game-designer         -- 验证活动的游戏机制
3. economy-designer      -- 平衡活动经济和奖励数值
4. narrative-director    -- 提供赛季叙事主题
5. writer                -- 创建活动描述和世界观文本
6. producer              -- 排定实现工作
7. [相关程序员实现]
8. qa-lead               -- 端到端测试活动流程
9. community-manager     -- 起草活动公告和补丁说明
10. release-manager      -- 部署活动内容
11. analytics-engineer   -- 监控活动参与度和指标
12. live-ops-designer    -- 活动后分析和经验总结
```

## 跨域沟通协议

### 设计变更通知

当设计文档变更时，game-designer 必须通知：
- lead-programmer（实现影响）
- qa-lead（测试计划需更新）
- producer（排期影响评估）
- 相关专家代理（取决于变更内容）

### 架构变更通知

当创建或修改架构决策记录（ADR）时，technical-director 必须通知：
- lead-programmer（需要的代码变更）
- 所有受影响的专业程序员
- qa-lead（测试策略可能变更）
- producer（排期影响）

### 资产标准变更通知

当美术圣经或资产标准变更时，art-director 必须通知：
- technical-artist（管线变更）
- 所有使用受影响资产的内容创作者
- devops-engineer（如构建管线受影响）

## 应避免的反模式

1. **绕过层级**：专家代理未经咨询不得做出属于其负责人的决策。
2. **跨域实现**：代理未经相关所有者明确委派，不得修改其指定区域外的文件。
3. **暗箱决策**：所有决策必须记录在案。无书面记录的口头约定会导致矛盾。
4. **单体任务**：分配给代理的每个任务应在 1-3 天内可完成。如规模更大，必须先进行拆分。
5. **基于假设的实现**：如规格存在歧义，实现者必须向规格制定者提问，而非自行猜测。错误的猜测比一个问题代价更高。
