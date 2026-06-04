# 编码标准

- 所有游戏代码必须在公共 API 上包含文档注释（doc comments）
- 每个系统必须在 `Docs/architecture/` 中有对应的架构决策记录（Architecture Decision Record, ADR）
- 游性数值必须是数据驱动的（外部配置），禁止硬编码（hardcode）
- 所有公共方法必须可单元测试（依赖注入优于单例模式）
- 提交必须引用相关设计文档或任务 ID
- **验证驱动开发（Verification-Driven Development）**：添加游戏系统时先编写测试。UI 变更通过截图验证。将预期输出与实际输出对比后再标记工作完成。每项实现都必须有证明其有效的方式。

# 设计文档标准

- 所有设计文档使用 Markdown 格式
- 每个机制在 `Docs/gdd/` 中有专属文档
- 文档必须包含以下 8 个必填章节：
  1. **概述（Overview）** — 一段式总结
  2. **玩家幻想（Player Fantasy）** — 期望的感受与体验
  3. **详细规则（Detailed Rules）** — 无歧义的机制描述
  4. **公式（Formulas）** — 所有数学定义及变量说明
  5. **边界情况（Edge Cases）** — 已处理的异常情况
  6. **依赖关系（Dependencies）** — 其他系统列表
  7. **调优旋钮（Tuning Knobs）** — 已识别的可配置数值
  8. **验收标准（Acceptance Criteria）** — 可测试的成功条件
- 平衡数值必须链接到其源公式或设计依据
