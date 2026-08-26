#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/SKAttributeTypes.h"

/** 原生资源策略；ASC 仅持有与弱 UObject 拥有者配对的接口，不保存生命周期状态。 */
class SEKIRO_API ISKResourcePolicy
{
public:
    /** 游戏线程查询策略是否正处于同步通知/转换；true 时公开资源请求返回 Reentrant。 */
    virtual bool IsResourcePolicyBusy() const = 0;

    /** 游戏线程校验普通资源操作；Operation 不包括初始化/内部恢复，返回允许或拒绝码，不写资源。 */
    virtual ESKNumericResultCode CheckResourceOperation(ESKNumericOperation Operation) const = 0;

    /** 游戏线程返回普通操作的拒绝原因名；允许时返回 None，不修改状态。 */
    virtual FName GetResourceRejectionReason(ESKNumericOperation Operation) const = 0;

    /** 游戏线程校验策略拥有者的内部恢复窗口及非零流程编号；不授予脚本旁路权限。 */
    virtual bool CanRestoreOwnedResources(int64 TransitionSerial) const = 0;

    /** 游戏线程在公开通知前处理完整实际快照；Result 可空表示统计属性变动，bInitial 表示首次就绪。 */
    virtual void ReconcileResourceState(const FSKNumericResult* Result, bool bInitial) = 0;

    /** 游戏线程发布已排队语义事件；ASC 保持重入门禁，监听者不能同步提交另一笔资源修改。 */
    virtual void FlushResourceEvents() = 0;
};
