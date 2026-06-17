# Agent 协调规则（Coordination Rules）

1. **垂直委派（Vertical Delegation）**：领导层代理（Leadership Agent）委派给部门主管（Department Lead），部门主管再委派给专家（Specialist）。对于复杂决策，不得跳过层级。
2. **横向协商（Horizontal Consultation）**：同一层级的代理可以相互协商，但不得在其领域之外做出约束性决策。
3. **冲突解决（Conflict Resolution）**：当两个代理意见不一致时，上报至共同的上级。如果没有共同上级，设计冲突上报至 `creative-director`，技术冲突上报至 `technical-director`。
4. **变更传播（Change Propagation）**：当设计变更影响多个领域时，由 `producer` 代理协调传播。
5. **禁止单方面跨域变更（No Unilateral Cross-Domain Changes）**：代理未经明确委派，不得修改其指定目录之外的文件。
