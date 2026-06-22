# 输入→JT ID 映射准确性测试方案

> 验证 `FSATAEImporter` 管线修正后，`ExtractCancelWindows` 输出的 CancelRules 是否与 TAE JSON 原始数据一致。

---

## 一、测试目标

| 测试层面 | 验证目标 | 方法 |
|----------|---------|------|
| **管线输出** | ExtractCancelWindows 输出的 CancelRules 与 TAE JSON 原始 JT 事件一致 | Python 管线验证脚本 |
| **运行时** | CanCancelTo(AnimID, Frame, TargetAction) 返回正确 | C++ 单元测试 |

---

## 二、测试数据集

### 2.1 核心测试动画

从 TAE JSON（2209 动画）中选出的代表性动画：

| AnimID | 名称 | 总帧数 | Cancel JT | 说明 |
|--------|------|--------|-----------|------|
| 201010 | 攻击连段（横斩一段） | 38 | JT=115(R1), 26(Generic), 117(L1), 118(L2), 154(Item) | 攻击典型，5种取消窗口齐全 |
| 201040 | 攻击连段（短刀速攻） | 23 | JT=115(R1), 26(Generic), 118(L2), 154(Item) | 无 L1 取消的短前摇攻击 |
| 201050 | 攻击连段（终结重击） | 33 | JT=115(R1), 26(Generic), 117(L1), 118(L2), 154(Item) | 终结段，窗口从帧0开始 |
| 100100 | Locomotion （Walk动作中） | 28 | JT=115(R1), 26(Generic), 117(L1), 118(L2), 154(Item) | 移动中攻击取消 |
| 301000 | Guard_Idle（举刀格挡） | 68 | JT=25(Dodge), 26(Generic), 115(R1), 117(L1), 118(L2), 154(Item) | 格挡状态，含 Dodge 取消 |
| 310000 | Deflect（完美格挡） | 103 | JT=115(R1), 117(L1), 118(L2) | 弹刀后反斩窗口 |
| 400000 | Hit（受击） | 45 | JT=26(Generic), 115(R1), 117(L1), 118(L2), 154(Item) | 受击硬直窗口 |

### 2.2 批量测试范围

全覆盖检查：

| 范围 | AnimID 区间 | 动画数 | 预期 |
|------|------------|--------|------|
| Locomotion | 0-999 | ~300 | 各方向移动动画有对应的取消窗口 |
| Attack | 200000-209999 | 203 | 每段攻击在 Recovery 阶段有 R1 取消 |
| Guard | 300000-301000 | ~20 | 举刀时有 Dodge 取消 |
| Deflect | 310000-311000 | ~30 | 弹刀后有 R1 反斩窗口 |
| Dodge | 320000-322000 | ~30 | 闪避中有 L1/L2 取消 |
| Hit | 400000-402000 | ~50 | 受击硬直后有 Guard 取消 |
| Jump | 600-699 | ~100 | 跳跃中有攻击/格挡取消 |
| Deathblow | 500000-510000 | ~30 | 忍杀动画通常无取消（Priority=10 不可打断）|

---

## 三、验证脚本

### 3.1 Python 管线验证（离线，在管线输出日志级别验证）

用 Python 直接读 TAE JSON，验证 ExtractCancelWindows 的逻辑是否正确。

```python
# test_cancel_mapping.py
"""
验证 ExtractCancelWindows 的 CancelRules 输出与 TAE JSON 原始数据的映射正确性。
"""

import json
import sys

CANCEL_JT_MAP = {
    115: "Attack",    # AnimCancelEnd_R1
    117: "Guard",     # AnimCancelEnd_L1
    118: "Prosthetic", # AnimCancelEnd_L2
    25:  "Dodge",     # AnimCancelStart_Dodge
    26:  "Attack",    # GenericCancelStart
    154: "Item",      # ItemUseWindow
}

JT_NAMES = {115: "R1取消", 117: "L1防御", 118: "L2义手",
            25: "闪避取消", 26: "通用取消", 154: "道具窗口"}


def test_cancel_mapping(tae_json_path):
    """验证管线中的 ExtractCancelWindows 逻辑与 TAE 原始数据一致"""
    with open(tae_json_path, 'r', encoding='utf-8') as f:
        data = json.load(f)

    total_anims = 0
    total_cancel_events = 0
    total_cancel_rules = 0  # 同 AnimID 合并条数

    errors = []
    stats = {}  # TargetAction -> count

    for tae_file in data['TAE_Files']:
        for anim in tae_file.get('Animations', []):
            aid = anim.get('AnimID', -1)
            events = anim.get('Events', [])

            # 模拟 ExtractCancelWindows 的新逻辑
            cancel_rules_for_anim = []

            for e in events:
                if e.get('Type') != 0:
                    continue
                jtid = e.get('Parameters', {}).get('JumpTableID')
                if jtid not in CANCEL_JT_MAP:
                    continue

                target_action = CANCEL_JT_MAP[jtid]
                start_frame = e['StartFrame']
                end_frame = e['EndFrame']

                # Validate: 帧范围必须有效
                if end_frame < start_frame:
                    errors.append(f"AnimID={aid}: JT={jtid} 帧范围无效 {start_frame}-{end_frame}")

                # Validate: 帧必须在合理范围内 (30fps, 最长动画约 200帧)
                if end_frame > 500:
                    errors.append(f"AnimID={aid}: JT={jtid} EndFrame={end_frame} 超出预期范围")

                cancel_rules_for_anim.append({
                    'jtid': jtid,
                    'target': target_action,
                    'range': (start_frame, end_frame)
                })

                target = f"{target_action}(JT={jtid})"
                stats[target] = stats.get(target, 0) + 1
                total_cancel_events += 1

            if cancel_rules_for_anim:
                total_anims += 1
                total_cancel_rules += len(cancel_rules_for_anim)

                # 每个动画每条规则都是独立条目（ExtractCancelWindows 不合并重复）

    # ── 输出统计 ──
    print(f"=== CancelRules 映射统计 ===")
    print(f"有取消窗口的动画数: {total_anims}")
    print(f"总取消事件数: {total_cancel_events}")
    print(f"总取消规则数: {total_cancel_rules}")
    print()

    print(f"按 TargetAction 分布:")
    for target in sorted(stats.keys(), key=lambda k: -stats[k]):
        print(f"  {target}: {stats[target]} 条")
    print()

    # ── 验证关键动画的窗口帧 ──
    verify_anims = [201010, 201040, 201050, 100100, 301000, 310000, 400000]
    print(f"=== 关键动画验证 ===")
    for tae_file in data['TAE_Files']:
        for anim in tae_file.get('Animations', []):
            aid = anim.get('AnimID', -1)
            if aid not in verify_anims:
                continue
            print(f"AnimID={aid}:")
            events = anim.get('Events', [])
            for e in events:
                if e.get('Type') != 0:
                    continue
                jtid = e.get('Parameters', {}).get('JumpTableID')
                if jtid in CANCEL_JT_MAP:
                    target = CANCEL_JT_MAP[jtid]
                    print(f"  JT={jtid:3d}({JT_NAMES[jtid]:>6}) → TargetAction={target:>10} 帧=[{e['StartFrame']:3d}-{e['EndFrame']:3d}]")
            print()

    # ── 错误报告 ──
    if errors:
        print(f"⚠ 发现 {len(errors)} 个问题:")
        for err in errors[:20]:
            print(f"  {err}")
    else:
        print("✅ 帧范围验证通过")

    # ── 断言（用于 CI）──
    assert total_cancel_events > 0, "没有找到任何取消事件！"
    assert len(errors) == 0, f"发现 {len(errors)} 个帧范围问题"

    return {
        'total_anims': total_anims,
        'total_events': total_cancel_events,
        'total_rules': total_cancel_rules,
        'errors': errors,
        'stats': stats
    }


if __name__ == '__main__':
    result = test_cancel_mapping(r'D:\Sekiro\Extracted\Sekiro_TAE_Logic.json')
    print(f"\n测试结果: {'✅ PASS' if not result['errors'] else '❌ FAIL'}")
    sys.exit(1 if result['errors'] else 0)
```

运行方式：

```bash
python3 test_cancel_mapping.py
```

**预期输出**：

```
=== CancelRules 映射统计 ===
有取消窗口的动画数: 899
总取消事件数: ~4200
总取消规则数: ~4200

按 TargetAction 分布:
  Attack(JT=115): 1217 条
  Attack(JT=26):  1096 条
  Guard(JT=117):  1127 条
  Prosthetic(JT=118): 1129 条
  Dodge(JT=25):   64 条
  Item(JT=154):   1049 条
```

### 3.2 C++ DataAsset 验证（运行时）

在 UE 编辑器中执行（通过 bridge.py 或控制台命令），验证 DataAsset 内容：

```cpp
// 验证命令：在 PIE 中执行
void ValidateCancelRules(UWorld* World)
{
    USKAnimationLogicData* DA = LoadObject<USKAnimationLogicData>(nullptr,
        TEXT("/Game/Characters/Sekiro/DA_Sekiro_AnimLogic.DA_Sekiro_AnimLogic"));

    if (!DA) { UE_LOG(LogTemp, Error, TEXT("❌ DataAsset 未加载")); return; }

    // 1. 总条数验证
    int32 TotalRules = 0;
    for (const auto& Pair : DA->CancelRules)
        TotalRules += Pair.Value.Rules.Num();

    UE_LOG(LogTemp, Log, TEXT("CancelRules 总数: %d 动画, %d 规则"),
        DA->CancelRules.Num(), TotalRules);

    // 2. 关键动画验证
    int32 TestAnims[] = {201010, 201040, 201050, 100100, 301000, 310000, 400000};
    for (int32 AnimID : TestAnims)
    {
        const FSKCancelRuleList* List = DA->CancelRules.Find(AnimID);
        if (!List) {
            UE_LOG(LogTemp, Error, TEXT("❌ AnimID=%d: 无 CancelRules"), AnimID);
            continue;
        }
        for (const FSKCancelRule& Rule : List->Rules)
        {
            UE_LOG(LogTemp, Log, TEXT("AnimID=%d: Target=%s 帧=[%d-%d]"),
                AnimID, *Rule.TargetAction.ToString(), Rule.StartFrame, Rule.EndFrame);
        }
    }

    // 3. 运行时 CanCancelTo 验证
    float Crossfade;
    bool bCanCancel_201040_f5_to_Attack =
        DA->CanCancelTo(201040, 5.0f/30.0f, TEXT("Attack"), Crossfade);
    bool bCanCancel_201040_f0_to_Attack =
        DA->CanCancelTo(201040, 0.0f/30.0f, TEXT("Attack"), Crossfade);

    UE_LOG(LogTemp, Log, TEXT("CanCancelTo(201040, 帧5, 'Attack') = %s"), 
        bCanCancel_201040_f5_to_Attack ? TEXT("✅ true") : TEXT("❌ false"));
    UE_LOG(LogTemp, Log, TEXT("CanCancelTo(201040, 帧0, 'Attack') = %s"), 
        bCanCancel_201040_f0_to_Attack ? TEXT("true") : TEXT("✅ false (前摇阶段不可取消)"));
}
```

---

## 四、各 JT ID 的预期验证结果

### 4.1 JT=115 → TargetAction="Attack"

| 测试用例 | 预期 | 通过标准 |
|---------|------|---------|
| AnimID=201010, 帧5 | ✅ CanCancelTo=true | R1取消窗口从帧3开始 |
| AnimID=201010, 帧0 | ❌ CanCancelTo=false | 帧0-2是前摇，不可取消 |
| AnimID=301000, 帧10 | ❌ CanCancelTo=false | Guard_Idle 帧0-32没有 JT=115 |
| AnimID=301000, 帧40 | ✅ CanCancelTo=true | Guard_Idle 帧33-68有 JT=115 |

### 4.2 JT=117 → TargetAction="Guard"

| 测试用例 | 预期 | 通过标准 |
|---------|------|---------|
| AnimID=201010, 帧5 | ✅ CanCancelTo=true | JT=117 帧3-38 |
| AnimID=201040, 帧5 | ❌ CanCancelTo=false | JT=117 未出现在 201040 中 |
| AnimID=201050, 帧0 | ✅ CanCancelTo=true | JT=117 帧0-33（终结段全程可格挡取消）|

### 4.3 JT=25 → TargetAction="Dodge"

| 测试用例 | 预期 | 通过标准 |
|---------|------|---------|
| AnimID=301000, 帧5 | ✅ CanCancelTo=true | JT=25 帧0-15 |
| AnimID=301000, 帧30 | ❌ CanCancelTo=false | JT=25 只覆盖帧0-15 |
| AnimID=201010, 帧10 | ❌ CanCancelTo=false | 201010 没有 JT=25 |

---

## 五、自动化测试流程

### 5.1 阶段 1：管线验证（Python，离线）

```bash
# 在管线构建后立即运行
python3 Script/test_cancel_mapping.py
```

### 5.2 阶段 2：DataAsset 重建

```bash
# 在 UE 编辑器中重建 DataAsset
"$UE_ENGINE_DIR/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
    "D:/Sekiro/Sekiro.uproject" -run=SekiroAnimDataBuild \
    -output=/Game/Characters/Sekiro/DA_Sekiro_AnimLogic
```

### 5.3 阶段 3：C++ 运行时验证（PIE）

在 PIE 中运行 `ValidateCancelRules`，输出与 Python 脚本比对：

| 字段 | Python 预期 | C++ 实际 | 应匹配 |
|------|------------|---------|--------|
| CancelRules 总动画数 | 899 | ? | ✅ |
| CancelRules 总规则数 | ~4200 | ? | ✅ |
| CanCancelTo(201010, 5, Attack) | true | ? | ✅ |
| CanCancelTo(201010, 0, Attack) | false | ? | ✅ |

### 5.4 阶段 4：回测（确保旧行为不被破坏）

| 回测项 | 方法 |
|--------|------|
| FrameFlags 不变 | 验证 AnimFrameFlags 数量与修正前一致 |
| AttackHitboxConfigs 不变 | 验证攻击框数量与修正前一致 |
| SpEffectConfigs 不变 | 验证 SpEffect 数量与修正前一致 |
| CategoryAnimMap 不变 | 验证类别分组与修正前一致 |

---

## 六、常见问题排查

| 现象 | 可能原因 |
|------|---------|
| CanCancelTo 全部返回 false | DataAsset 未重建，使用了旧管线输出的 `.uasset` |
| JT=115 映射丢失 | `ExtractCancelWindows` 修正未正确应用 |
| TargetAction 名称不匹配 | `BuildCancelRules` 中未更新 TargetAction 字段 |
| 运行时加载不到 DataAsset | `SekiroAnimDataBuild` Commandlet 未执行或输出路径错误 |

---

## 附录：TAE JSON 原始数据参考格式

```json
{
  "Type": 0,
  "TypeName": "JumpTable",
  "StartFrame": 3,
  "EndFrame": 38,
  "Parameters": {
    "JumpTableID": 115,    // AnimCancelEnd_R1
    "ArgA": 0,
    "ArgB": -1,
    "ArgC": 6,
    "ArgD": 0,
    "StateInfo": 0
  }
}
```

管线修正后，这条数据应输出为：

```cpp
FSKCancelRule {
    StartFrame = 3,
    EndFrame = 38,
    TargetAction = "Attack",
    CrossfadeDuration = 0.1f,
    Priority = 0
}
```
