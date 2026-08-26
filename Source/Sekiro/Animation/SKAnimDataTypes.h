#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "SKAnimDataTypes.generated.h"

// ── 通用移动方向枚举 ─────────────────────────────────────────

UENUM(BlueprintType)
enum class ESKLocomotionDirection : uint8
{
	Fwd,                                                     // 前
	Fwd_L,                                                   // 前左
	L,                                                       // 左
	Bwd_L,                                                   // 后左
	Bwd,                                                     // 后
	Bwd_R,                                                   // 后右
	R,                                                       // 右
	Fwd_R                                                    // 前右
};

// ── ALS 风格动画蓝图状态枚举 ─────────────────────────────────

UENUM(BlueprintType)
enum class ESKAnimMovementState : uint8
{
	Grounded,                                                // 地面
	InAir,                                                   // 空中
	Crouching,                                               // 蹲姿
	Special                                                  // 特殊状态（悬挂/游泳/贴墙等）
};

UENUM(BlueprintType)
enum class ESKAnimMovementAction : uint8
{
    None,                                                    // 无互斥移动动作
    Step,                                                    // 地面垫步
    Dodge                                                    // 空中或后续扩展的完整闪避动作
};

UENUM(BlueprintType)
enum class ESKAnimRotationMode : uint8
{
	VelocityDirection,                                       // 非锁定：朝运动方向
	LookingDirection,                                        // 锁定：朝观察/锁定目标方向
	SprintAlign                                              // 冲刺：朝运动方向且相机慢速对齐
};

UENUM(BlueprintType)
enum class ESKAnimGait : uint8
{
	Idle,                                                    // 静止
	Walk,                                                    // 步行
	Run,                                                     // 奔跑
	Sprint                                                   // 冲刺
};

UENUM(BlueprintType)
enum class ESKAnimStance : uint8
{
	Standing,                                                // 站立
	Crouching                                                // 蹲姿
};

UENUM(BlueprintType)
enum class ESKAnimGroundedEntryState : uint8
{
	Idle,                                                    // 待机
	Start,                                                   // 起步
	Cycle,                                                   // 循环移动
	Stop,                                                    // 停止
	DodgeStep,                                               // 闪避步
	Pivot,                                                   // 急转
	Turn,                                                    // 移动中转向
	TurnInPlace                                              // 原地转身
};

UENUM(BlueprintType)
enum class ESKAnimTurnDirection : uint8
{
	None,                                                    // 不转向
	Left,                                                    // 播放左转动画
	Right                                                    // 播放右转动画
};

UENUM(BlueprintType)
enum class ESKAnimOverlayState : uint8
{
    Default,                                                 // 普通移动基础姿态
    Sword,                                                   // 持刀移动姿态
    Guard,                                                   // 持续防御姿态
    Combat                                                   // 攻击、弹反等全身战斗动作
};

// ============================================================
// DataTable 行类型 — 用于 UE 编辑器导入 StateAnimMap / StateTransitions
// 数据来源：c0000.hkx Behavior Graph 提取
// ============================================================

// ── 状态 → AnimID 映射行 ──
USTRUCT(BlueprintType)
struct FSKStateAnimRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "State")
	FName StateName;                            // 状态名（Idle, Sprint, Attack...）

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "State")
	FString DisplayName;                        // 显示名

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "State")
	FString Category;                           // 分类（locomotion/combat/air/damage...）

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation")
	FString PrimaryAnimID;                      // 主要动画 ID（a000_001151）

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation")
	FString AllAnimIDs;                         // 所有动画 ID，逗号分隔
};

// ── 状态转换行 ──
USTRUCT(BlueprintType)
struct FSKStateTransitionRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Transition")
	FName EventName;                            // 触发事件名（W_Sprint, GroundJump_to_FreeFall）

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Transition")
	FName FromState;                            // 源状态

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Transition")
	FName ToState;                              // 目标状态

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Transition")
	float BlendDuration = 0.2f;                 // 混合时长（秒）
};
