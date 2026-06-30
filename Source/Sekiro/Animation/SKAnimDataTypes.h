#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "SKAnimDataTypes.generated.h"

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
