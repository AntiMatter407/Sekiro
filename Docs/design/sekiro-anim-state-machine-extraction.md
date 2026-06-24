# 阶段1：BehaviorVariationID ↔ AnimID 映射 — 技术方案

| 进度文档 | 状态 | 创建 | 更新 |
|-----------|------|------|------|
| [需求](../plan/sekiro-anim-input-replica.md) | 🟢 已验证 | 2026-06-24 | 2026-06-25 |

## 目标

建立 `behaviorVariationID → AnimID` 的映射关系，基于真实 BehaviorParam_PC 数据做权威 AnimID 分类。

## 已验证的编码规则

### 数据来源

`BehaviorParam_PC.json`（611 条记录，49 个唯一 variation_id）
→ `Output/BehaviorParam_PC.json`

提取工具：`Script/sekiro_asset_manager/ext_tools/ParamReader/`（C# + SoulsFormats.dll 反射访问 Row.DataOffset）

### BehaviorParam Key 编码（已验证）

```
Row.ID = 100000000 + variation_id * 1000 + behaviorJudgeID
```

572/572 非零条目完全符合（39 条 var_id=0 的系统条目除外）。

### AnimID ↔ variation_id 映射（已验证）

```
AnimID = variation_id * 100 + sub_id     (sub_id ∈ [0, 99])
variation_id = AnimID / 100              (整数除法)
```

| variation_id | AnimID 范围 | CategoryID | 行为大类 |
|-------------|------------|-----------|---------|
| 0 | 0-99 | 0 | System/Common |
| 5000-5030 | 500000-503099 | 50 | Deathblow |
| 7000-7902 | 700000-790299 | 70-79 | Prosthetic |
| 9500 | 950000-950099 | 95 | (特殊) |
| 50100-50400 | 5010000-5040099 | 501-504 | Deathblow(高位) |

### BehaviorParam 行数据结构（每行 30 字节）

| 偏移 | 字段 | 类型 | 说明 |
|------|------|------|------|
| 0x00 | variation_id | int32 | = AnimID / 100 |
| 0x04 | behaviorJudgeID | int32 | 对应 TAE Type=1/2/5 事件 |
| 0x08 | ez_state_behavior_type | byte | EzState 行为类型（旧版） |
| 0x09 | ref_type | byte | 0=AtkParam, 1=Bullet, 2=SpEffect |
| 0x0C | ref_id | int32 | AtkParam_PC / Bullet / SpEffectParam ID |
| 0x10 | sfx_variation_id | int32 | SFX 音效 variation ID |
| 0x14 | stamina | int32 | 精力消耗 |
| 0x18 | mp | int32 | 纸人消耗 |
| 0x1C | category | byte | 行为大类编号 |
| 0x1D | hero_point | byte | 英雄点 |

### 数据统计

| 指标 | 数值 |
|------|------|
| 总条目 | 611 |
| 唯一 variation_id | 49 |
| 唯一 behaviorJudgeID | 202 |
| RefType=Attack | 325 |
| RefType=Bullet | 277 |
| RefType=SpEffect | 9 |
| Category 0 | 77 条 |
| Category 1 | 229 条 |
| Category 2 | 186 条 |
| Category 5 | 39 条（var_id=0 系统条目） |
| Category 9 | 80 条 |

### 关键修正

原设计文档假设 `behaviorVariationID = CategoryID * 10`（即 AnimID/10000*10），
但实际数据证明 `variation_id = AnimID / 100`（除以100而非10000），粒度更细。

例如 AnimID=503050:
- 原假设: CategoryID=50 → VarID ∈ [500, 509]
- 实际: VarID = 503050/100 = 5030 ✅

---

## 方案设计

### 核心思路（简化）

由于 `variation_id = AnimID / 100` 是确定性的，**不需要交叉验证推断**。
直接用 AnimID 计算出 variation_id，然后在 BehaviorParam 表中查对应的行为配置。

```python
def get_behavior_config(anim_id: int) -> list[dict]:
    """根据 AnimID 查询 BehaviorParam 配置"""
    var_id = anim_id // 100
    return behavior_param_table.get(var_id, [])  # 返回该 variation 的所有 behavior 行
```

### BehaviorParam 查表流程

```
输入: AnimID = 300000
  ↓
var_id = 300000 // 100 = 3000
  ↓
查询 BehaviorParam[3000]:
  如果没有 → 回退到 AnimID 数字范围分类
  如果有 → 获取:
    - behaviorJudgeID 列表（对应 TAE Type=1/2/5 事件）
    - RefType（AtkParam / Bullet / SpEffect）
    - Category（行为大类）
  ↓
输出: {
    "behaviorVariationID": 3000,
    "anim_range": [300000, 300099],
    "behaviors": [...],
    "primary_action": "Guard"  # 根据 Category + RefType 判定
}
```

### 与 TAE 事件的关联

```
AnimID → TAE 事件 (Type=1/2/5)
         ↓ BehaviorJudgeID
BehaviorParam[var_id] → 匹配 judge_id → 获取 RefType + RefID
         ↓
    RefType=0 (AtkParam) → AtkParam_PC[ref_id] → 攻击参数
    RefType=1 (Bullet)   → Bullet[ref_id]      → 弹道参数
    RefType=2 (SpEffect) → SpEffectParam[ref_id] → 特效参数
```

---

## 涉及文件

| 文件 | 操作 | 说明 |
|------|------|------|
| `Output/BehaviorParam_PC.json` | ✅ 已生成 | 611 条 BehaviorParam 数据 |
| `Script/sekiro_asset_manager/ext_tools/ParamReader/` | ✅ 已完成 | C# 提取工具 |
| `Script/sekiro_asset_manager/behavior_variation_map.py` | 待建 | BehaviorParam 查表 + AnimID 分类 |
| `Source/Sekiro/Animation/SKAnimationController.cpp` | 待修改 | 替换 InferCategoryFromAnimID 为 BehaviorParam 查表 |

## 数据流

```
BehaviorParam_PC.json (611条, var_id→behaviors)
         ↓
behavior_variation_map.py
         ↓
category_map.json (AnimID → Action+Config)
         ↓
SKAnimationController::ResolveAnimID()
  取代 InferCategoryFromAnimID 的数字范围硬编码
```

---

## 变更记录

| 日期 | 变更 |
|------|------|
| 2026-06-24 | 创建：三步骤方案（编码规则 + 交叉验证 + 输出映射） |
| 2026-06-25 | 阶段2完成：BehaviorParam_PC 611条提取，编码公式验证通过；修正设计：variation_id = AnimID/100（非 CategoryID*10） |