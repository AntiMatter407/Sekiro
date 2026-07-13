#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequenceBase.h"
#include "SekiroLuaAnimInstanceProxy.h"
#include "SekiroLuaAnimTypes.h"
#include "UnLuaInterface.h"
#include "SekiroLuaAnimInstance.generated.h"

UCLASS(Blueprintable, BlueprintType)
class SEKIROANIMBLUEPRINTEXT_API USekiroLuaAnimInstance : public UAnimInstance, public IUnLuaInterface
{
    GENERATED_BODY()

public:
    /** 作用：构造 Lua 动画实例并保留默认配置。@param 无。@return 无。 */
    USekiroLuaAnimInstance();

    /** 作用：初始化动画实例、清空运行时快照并配置 Lua 模块。@param 无。@return void，无返回值。 */
    virtual void NativeInitializeAnimation() override;
    /** 作用：执行引擎动画更新，并在启用自动更新时驱动 Lua 动画。@param DeltaSeconds float，本帧时间，单位为秒。@return void，无返回值。 */
    virtual void NativeUpdateAnimation(float DeltaSeconds) override;
    /** 作用：向 UE 动画调试画布输出各 Lua 动画层状态。@param DisplayDebugManager FDisplayDebugManager&，调试文本绘制器。@param Indent float&，当前绘制缩进并由绘制器更新。@return void，无返回值。 */
    virtual void DisplayDebugInstance(FDisplayDebugManager& DisplayDebugManager, float& Indent) override;
    /** 作用：创建持有 Lua 不可变快照的自定义动画代理。@param 无。@return FAnimInstanceProxy*，新代理地址。 */
    virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
    /** 作用：销毁自定义动画代理。@param InProxy FAnimInstanceProxy*，待销毁代理。@return void，无返回值。 */
    virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override;

    /** 作用：更新全部已注册 Lua 动画层。@param DeltaSeconds float，本帧时间，单位为秒。@return bool，至少一个动画层成功应用决策时为 true。 */
    UFUNCTION(BlueprintCallable, Category = "Sekiro|Lua Animation")
    bool UpdateLuaDrivenAnimation(float DeltaSeconds);

    /** 作用：求值并更新时间指定 Lua 动画层。@param LayerName FName，目标动画层名称，None 表示默认层。@param DeltaSeconds float，本帧时间，单位为秒。@return bool，成功应用 Lua 动画决策时为 true。 */
    UFUNCTION(BlueprintCallable, Category = "Sekiro|Lua Animation")
    bool UpdateLuaDrivenAnimationLayer(FName LayerName, float DeltaSeconds);

    /** 作用：覆盖当前实例使用的 Lua 模块名称并使配置失效。@param ModuleName const FString&，UnLua 模块名称。@return void，无返回值。 */
    UFUNCTION(BlueprintCallable, Category = "Sekiro|Lua Animation")
    void SetLuaAnimModuleName(const FString& ModuleName);

    /** 作用：获取最终解析出的 Lua 动画模块名称。@param 无。@return FString，当前有效模块名称。 */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Sekiro|Lua Animation")
    FString GetLuaAnimModuleName() const;

    /** 作用：实现 UnLua 接口并返回待绑定模块名称。@param 无。@return FString，UnLua 模块名称。 */
    virtual FString GetModuleName_Implementation() const override;

    /** 作用：设置 LayerName 为 None 时使用的默认动画层。@param LayerName FName，新的默认层名称。@return void，无返回值。 */
    UFUNCTION(BlueprintCallable, Category = "Sekiro|Lua Animation")
    void SetDefaultLuaAnimLayerName(FName LayerName);

    /** 作用：注册一个需要逐帧求值的 Lua 动画层。@param LayerName FName，动画层名称。@return void，无返回值。 */
    UFUNCTION(BlueprintCallable, Category = "Sekiro|Lua Animation")
    void RegisterLuaAnimLayer(FName LayerName);

    /** 作用：清空注册层、待处理决策和已发布快照。@param 无。@return void，无返回值。 */
    UFUNCTION(BlueprintCallable, Category = "Sekiro|Lua Animation")
    void ClearLuaAnimLayers();

    /** 作用：获取已注册 Lua 动画层名称副本。@param 无。@return TArray<FName>，动画层名称数组。 */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Sekiro|Lua Animation")
    TArray<FName> GetLuaAnimLayerNames() const;

    /** 作用：按软路径加载并缓存动画资产。@param AssetPath const FString&，动画资产对象路径。@return UAnimationAsset*，加载成功的资产，失败返回 nullptr。 */
    UFUNCTION(BlueprintCallable, Category = "Sekiro|Lua Animation")
    UAnimationAsset* LoadLuaAnimationAsset(const FString& AssetPath);

    /** 作用：按动画层和节点名称创建或取得持久 SequencePlayer。@param LayerName FName，目标动画层，None 表示默认层。@param NodeName FName，Lua 图内稳定且非空的节点名称。@return FSekiroLuaPoseLink，成功时返回当前图代次的节点句柄，失败返回无效句柄。 */
    UFUNCTION(BlueprintCallable, Category = "Sekiro|Lua Animation|Pose Graph")
    FSekiroLuaPoseLink CreateLuaSequencePlayer(FName LayerName, FName NodeName);

    /** 作用：按动画层和节点名称创建或取得持久 StateResult。@param LayerName FName，目标动画层，None 表示默认层。@param NodeName FName，Lua 图内稳定且非空的节点名称。@param StateName FName，节点所属状态名称。@return FSekiroLuaPoseLink，成功时返回当前图代次的节点句柄，失败返回无效句柄。 */
    UFUNCTION(BlueprintCallable, Category = "Sekiro|Lua Animation|Pose Graph")
    FSekiroLuaPoseLink CreateLuaStateResult(FName LayerName, FName NodeName, FName StateName);

    /** 作用：把 StateResult 的 Result 输入连接到同层 SequencePlayer。@param StateResultPoseLink FSekiroLuaPoseLink，CreateLuaStateResult 返回的节点句柄。@param InputPoseLink FSekiroLuaPoseLink，目标 SequencePlayer 句柄。@return bool，两个节点有效、同层且输入类型受支持时为 true。 */
    UFUNCTION(BlueprintCallable, Category = "Sekiro|Lua Animation|Pose Graph")
    bool SetLuaStateResultInput(FSekiroLuaPoseLink StateResultPoseLink, FSekiroLuaPoseLink InputPoseLink);

    /** 作用：检查 PoseLink 是否属于当前实例、当前图代次且仍对应已注册 SequencePlayer 或 StateResult。@param PoseLink FSekiroLuaPoseLink，待检查节点句柄。@return bool，句柄当前可用时为 true。 */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Sekiro|Lua Animation|Pose Graph")
    bool IsLuaPoseLinkValid(FSekiroLuaPoseLink PoseLink) const;

    /** 作用：查询 PoseLink 是否仍在原生当前输出或活动过渡链内。@param PoseLink FSekiroLuaPoseLink，待查询节点句柄。@return bool，Proxy 上一帧回传仍有权重时为 true。 */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Sekiro|Lua Animation|Pose Graph")
    bool IsLuaPoseLinkActive(FSekiroLuaPoseLink PoseLink) const;

    /** 作用：设置持久 SequencePlayer 的动画、调试别名和可选起播位置。@param PoseLink FSekiroLuaPoseLink，CreateLuaSequencePlayer 返回的节点句柄。@param Sequence UAnimSequenceBase*，已加载动画序列，不允许为空。@param AnimationName FName，Lua 动画别名。@param bResetTime bool，是否重置播放器时间。@param StartPosition float，重置时使用的归一化起播位置，范围为 [0,1]。@return bool，节点和动画有效且设置成功时为 true。 */
    UFUNCTION(BlueprintCallable, Category = "Sekiro|Lua Animation|Pose Graph")
    bool SetLuaSequencePlayerAsset(FSekiroLuaPoseLink PoseLink, UAnimSequenceBase* Sequence, FName AnimationName, bool bResetTime, float StartPosition);

    /** 作用：在游戏线程加载路径对应的动画序列并设置到持久 SequencePlayer。@param PoseLink FSekiroLuaPoseLink，CreateLuaSequencePlayer 返回的节点句柄。@param AnimationPath const FString&，动画序列对象路径。@param AnimationName FName，Lua 动画别名。@param bResetTime bool，是否重置播放器时间。@param StartPosition float，重置时使用的归一化起播位置，范围为 [0,1]。@return bool，路径解析为动画序列且设置成功时为 true。 */
    UFUNCTION(BlueprintCallable, Category = "Sekiro|Lua Animation|Pose Graph")
    bool SetLuaSequencePlayerAssetByPath(FSekiroLuaPoseLink PoseLink, const FString& AnimationPath, FName AnimationName, bool bResetTime, float StartPosition);

    /** 作用：更新持久 SequencePlayer 的播放倍率和循环属性。@param PoseLink FSekiroLuaPoseLink，目标节点句柄。@param PlayRate float，播放倍率，绝对值过小时按安全最小值处理。@param bLoop bool，是否循环播放。@return bool，节点有效且参数设置成功时为 true。 */
    UFUNCTION(BlueprintCallable, Category = "Sekiro|Lua Animation|Pose Graph")
    bool SetLuaSequencePlayerParameters(FSekiroLuaPoseLink PoseLink, float PlayRate, bool bLoop);

    /** 作用：将一个 PoseLink 设为动画层根输出，并在输出节点变化时建立普通前后 Pose 过渡。@param LayerName FName，目标动画层，必须与 PoseLink 所属层一致。@param PoseLink FSekiroLuaPoseLink，新的根输出节点。@param TransitionTime float，前后 Pose 线性交叉混合时间，单位为秒。@return bool，层和节点有效且快照发布成功时为 true。 */
    UFUNCTION(BlueprintCallable, Category = "Sekiro|Lua Animation|Pose Graph")
    bool PublishLuaOutputPose(FName LayerName, FSekiroLuaPoseLink PoseLink, float TransitionTime);

    /** 作用：将完整动画决策排入当前 Lua 求值周期。@param LayerName FName，目标层名称。@param Decision const FSekiroLuaAnimDecision&，姿势及过渡决策。@return bool，决策有效并成功排队时为 true。 */
    UFUNCTION(BlueprintCallable, Category = "Sekiro|Lua Animation")
    bool SubmitLuaAnimPose(FName LayerName, const FSekiroLuaAnimDecision& Decision);

    /** 作用：使用已加载资产提交通用动画姿势。@param LayerName FName，目标层。@param StateName FName，状态名。@param AnimationAsset UAnimationAsset*，动画资产。@param BlendInputX float，混合输入 X。@param BlendInputY float，混合输入 Y。@param BlendInputZ float，混合输入 Z。@param BlendTime float，线性混合秒数。@param PlayRate float，播放倍率。@param bLoop bool，是否循环。@param bResetTime bool，是否重置时间。@return bool，成功排队时为 true。 */
    UFUNCTION(BlueprintCallable, Category = "Sekiro|Lua Animation")
    bool SetLuaAnimPose(FName LayerName, FName StateName, UAnimationAsset* AnimationAsset, float BlendInputX, float BlendInputY, float BlendInputZ, float BlendTime, float PlayRate, bool bLoop, bool bResetTime);

    /** 作用：使用资产路径提交通用动画姿势。@param LayerName FName，目标层。@param StateName FName，状态名。@param AnimationPath const FString&，动画对象路径。@param BlendInputX float，混合输入 X。@param BlendInputY float，混合输入 Y。@param BlendInputZ float，混合输入 Z。@param BlendTime float，线性混合秒数。@param PlayRate float，播放倍率。@param bLoop bool，是否循环。@param bResetTime bool，是否重置时间。@return bool，成功排队时为 true。 */
    UFUNCTION(BlueprintCallable, Category = "Sekiro|Lua Animation")
    bool SetLuaAnimPoseByPath(FName LayerName, FName StateName, const FString& AnimationPath, float BlendInputX, float BlendInputY, float BlendInputZ, float BlendTime, float PlayRate, bool bLoop, bool bResetTime);

    /** 作用：使用路径和 Lua 别名提交通用动画姿势。@param LayerName FName，目标层。@param StateName FName，状态名。@param AnimationName FName，Lua 动画别名。@param AnimationPath const FString&，动画对象路径。@param BlendInputX float，混合输入 X。@param BlendInputY float，混合输入 Y。@param BlendInputZ float，混合输入 Z。@param BlendTime float，线性混合秒数。@param PlayRate float，播放倍率。@param bLoop bool，是否循环。@param bResetTime bool，是否重置时间。@return bool，成功排队时为 true。 */
    UFUNCTION(BlueprintCallable, Category = "Sekiro|Lua Animation")
    bool SetLuaAnimPoseByPathWithName(FName LayerName, FName StateName, FName AnimationName, const FString& AnimationPath, float BlendInputX, float BlendInputY, float BlendInputZ, float BlendTime, float PlayRate, bool bLoop, bool bResetTime);

    /** 作用：使用路径、别名和归一化起播位置提交姿势。@param LayerName FName，目标层。@param StateName FName，状态名。@param AnimationName FName，Lua 动画别名。@param AnimationPath const FString&，动画对象路径。@param BlendInputX float，混合输入 X。@param BlendInputY float，混合输入 Y。@param BlendInputZ float，混合输入 Z。@param BlendTime float，线性混合秒数。@param PlayRate float，播放倍率。@param bLoop bool，是否循环。@param bResetTime bool，是否重置时间。@param StartPosition float，归一化起播位置。@return bool，成功排队时为 true。 */
    UFUNCTION(BlueprintCallable, Category = "Sekiro|Lua Animation")
    bool SetLuaAnimPoseByPathWithNameAndStartPosition(FName LayerName, FName StateName, FName AnimationName, const FString& AnimationPath, float BlendInputX, float BlendInputY, float BlendInputZ, float BlendTime, float PlayRate, bool bLoop, bool bResetTime, float StartPosition);

    /** 作用：提交由 UE 惯性化完成过渡的动画姿势。@param LayerName FName，目标层。@param StateName FName，状态名。@param AnimationName FName，Lua 动画别名。@param AnimationPath const FString&，动画对象路径。@param BlendInputX float，混合输入 X。@param BlendInputY float，混合输入 Y。@param BlendInputZ float，混合输入 Z。@param PlayRate float，播放倍率。@param bLoop bool，是否循环。@param bResetTime bool，是否重置时间。@param bUseStartPosition bool，是否采用指定起播位置。@param StartPosition float，归一化起播位置。@param InertialBlendTime float，惯性化秒数。@return bool，成功排队时为 true。 */
    UFUNCTION(BlueprintCallable, Category = "Sekiro|Lua Animation")
    bool SetLuaAnimPoseByPathWithNameAndInertialization(FName LayerName, FName StateName, FName AnimationName, const FString& AnimationPath, float BlendInputX, float BlendInputY, float BlendInputZ, float PlayRate, bool bLoop, bool bResetTime, bool bUseStartPosition, float StartPosition, float InertialBlendTime);

    /** 作用：使用已加载资产提交序列姿势。@param LayerName FName，目标层。@param StateName FName，状态名。@param AnimationAsset UAnimationAsset*，序列资产。@param BlendTime float，混合秒数。@param PlayRate float，播放倍率。@param bLoop bool，是否循环。@param bResetTime bool，是否重置时间。@return bool，成功排队时为 true。 */
    UFUNCTION(BlueprintCallable, Category = "Sekiro|Lua Animation")
    bool SetLuaAnimSequencePose(FName LayerName, FName StateName, UAnimationAsset* AnimationAsset, float BlendTime, float PlayRate, bool bLoop, bool bResetTime);

    /** 作用：使用资产路径提交序列姿势。@param LayerName FName，目标层。@param StateName FName，状态名。@param AnimationPath const FString&，序列对象路径。@param BlendTime float，混合秒数。@param PlayRate float，播放倍率。@param bLoop bool，是否循环。@param bResetTime bool，是否重置时间。@return bool，成功排队时为 true。 */
    UFUNCTION(BlueprintCallable, Category = "Sekiro|Lua Animation")
    bool SetLuaAnimSequencePoseByPath(FName LayerName, FName StateName, const FString& AnimationPath, float BlendTime, float PlayRate, bool bLoop, bool bResetTime);

    /** 作用：使用路径和别名提交序列姿势。@param LayerName FName，目标层。@param StateName FName，状态名。@param AnimationName FName，Lua 动画别名。@param AnimationPath const FString&，序列对象路径。@param BlendTime float，混合秒数。@param PlayRate float，播放倍率。@param bLoop bool，是否循环。@param bResetTime bool，是否重置时间。@return bool，成功排队时为 true。 */
    UFUNCTION(BlueprintCallable, Category = "Sekiro|Lua Animation")
    bool SetLuaAnimSequencePoseByPathWithName(FName LayerName, FName StateName, FName AnimationName, const FString& AnimationPath, float BlendTime, float PlayRate, bool bLoop, bool bResetTime);

    /** 作用：使用路径、别名和起播位置提交序列姿势。@param LayerName FName，目标层。@param StateName FName，状态名。@param AnimationName FName，Lua 动画别名。@param AnimationPath const FString&，序列对象路径。@param BlendTime float，混合秒数。@param PlayRate float，播放倍率。@param bLoop bool，是否循环。@param bResetTime bool，是否重置时间。@param StartPosition float，归一化起播位置。@return bool，成功排队时为 true。 */
    UFUNCTION(BlueprintCallable, Category = "Sekiro|Lua Animation")
    bool SetLuaAnimSequencePoseByPathWithNameAndStartPosition(FName LayerName, FName StateName, FName AnimationName, const FString& AnimationPath, float BlendTime, float PlayRate, bool bLoop, bool bResetTime, float StartPosition);

    /** 作用：使用已加载资产提交 BlendSpace 姿势。@param LayerName FName，目标层。@param StateName FName，状态名。@param BlendSpaceAsset UAnimationAsset*，BlendSpace 资产。@param BlendInputX float，混合输入 X。@param BlendInputY float，混合输入 Y。@param BlendInputZ float，混合输入 Z。@param BlendTime float，混合秒数。@param PlayRate float，播放倍率。@param bLoop bool，是否循环。@param bResetTime bool，是否重置时间。@return bool，成功排队时为 true。 */
    UFUNCTION(BlueprintCallable, Category = "Sekiro|Lua Animation")
    bool SetLuaAnimBlendSpacePose(FName LayerName, FName StateName, UAnimationAsset* BlendSpaceAsset, float BlendInputX, float BlendInputY, float BlendInputZ, float BlendTime, float PlayRate, bool bLoop, bool bResetTime);

    /** 作用：使用资产路径提交 BlendSpace 姿势。@param LayerName FName，目标层。@param StateName FName，状态名。@param BlendSpacePath const FString&，BlendSpace 对象路径。@param BlendInputX float，混合输入 X。@param BlendInputY float，混合输入 Y。@param BlendInputZ float，混合输入 Z。@param BlendTime float，混合秒数。@param PlayRate float，播放倍率。@param bLoop bool，是否循环。@param bResetTime bool，是否重置时间。@return bool，成功排队时为 true。 */
    UFUNCTION(BlueprintCallable, Category = "Sekiro|Lua Animation")
    bool SetLuaAnimBlendSpacePoseByPath(FName LayerName, FName StateName, const FString& BlendSpacePath, float BlendInputX, float BlendInputY, float BlendInputZ, float BlendTime, float PlayRate, bool bLoop, bool bResetTime);

    /** 作用：使用路径和别名提交 BlendSpace 姿势。@param LayerName FName，目标层。@param StateName FName，状态名。@param AnimationName FName，Lua 动画别名。@param BlendSpacePath const FString&，BlendSpace 对象路径。@param BlendInputX float，混合输入 X。@param BlendInputY float，混合输入 Y。@param BlendInputZ float，混合输入 Z。@param BlendTime float，混合秒数。@param PlayRate float，播放倍率。@param bLoop bool，是否循环。@param bResetTime bool，是否重置时间。@return bool，成功排队时为 true。 */
    UFUNCTION(BlueprintCallable, Category = "Sekiro|Lua Animation")
    bool SetLuaAnimBlendSpacePoseByPathWithName(FName LayerName, FName StateName, FName AnimationName, const FString& BlendSpacePath, float BlendInputX, float BlendInputY, float BlendInputZ, float BlendTime, float PlayRate, bool bLoop, bool bResetTime);

    /** 作用：使用路径、别名和起播位置提交 BlendSpace 姿势。@param LayerName FName，目标层。@param StateName FName，状态名。@param AnimationName FName，Lua 动画别名。@param BlendSpacePath const FString&，BlendSpace 对象路径。@param BlendInputX float，混合输入 X。@param BlendInputY float，混合输入 Y。@param BlendInputZ float，混合输入 Z。@param BlendTime float，混合秒数。@param PlayRate float，播放倍率。@param bLoop bool，是否循环。@param bResetTime bool，是否重置时间。@param StartPosition float，归一化起播位置。@return bool，成功排队时为 true。 */
    UFUNCTION(BlueprintCallable, Category = "Sekiro|Lua Animation")
    bool SetLuaAnimBlendSpacePoseByPathWithNameAndStartPosition(FName LayerName, FName StateName, FName AnimationName, const FString& BlendSpacePath, float BlendInputX, float BlendInputY, float BlendInputZ, float BlendTime, float PlayRate, bool bLoop, bool bResetTime, float StartPosition);

    /** 作用：通过 UE 反射读取数值属性。@param PropertyName const FString&，属性名称。@param DefaultValue float，读取失败时返回值。@return float，属性数值或默认值。 */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Sekiro|Lua Animation")
    float GetLuaAnimNumberProperty(const FString& PropertyName, float DefaultValue) const;

    /** 作用：通过 UE 反射读取布尔或数值属性。@param PropertyName const FString&，属性名称。@param bDefaultValue bool，读取失败时返回值。@return bool，属性布尔值或默认值。 */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Sekiro|Lua Animation")
    bool GetLuaAnimBoolProperty(const FString& PropertyName, bool bDefaultValue) const;

    /** 作用：通过 UE 反射读取可文本化属性。@param PropertyName const FString&，属性名称。@param DefaultValue const FString&，读取失败时返回值。@return FString，属性文本或默认值。 */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Sekiro|Lua Animation")
    FString GetLuaAnimPropertyText(const FString& PropertyName, const FString& DefaultValue) const;

    /** 作用：线程安全地复制默认动画快照。@param 无。@return FSekiroLuaAnimSnapshot，默认层快照副本。 */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Sekiro|Lua Animation")
    FSekiroLuaAnimSnapshot GetLuaAnimSnapshot() const;

    /** 作用：线程安全地复制指定层动画快照。@param LayerName FName，目标层名称。@return FSekiroLuaAnimSnapshot，目标层快照副本，不存在时返回空快照。 */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Sekiro|Lua Animation")
    FSekiroLuaAnimSnapshot GetLuaAnimLayerSnapshot(FName LayerName) const;

    /** 作用：发布指定层的方向扭转策略。@param LayerName FName，目标层。@param bEnabled bool，是否启用。@param OrientationAngle float，姿势扭转角度。@param WarpingAlpha float，扭转强度。@param bWarpRootMotionTranslation bool，是否旋转根运动平移。@param RootMotionTranslationAngle float，根运动平移旋转角。@return void，无返回值。 */
    UFUNCTION(BlueprintCallable, Category = "Sekiro|Lua Animation")
    void SetLuaAnimOrientationWarpingPolicyByName(FName LayerName, bool bEnabled, float OrientationAngle, float WarpingAlpha, bool bWarpRootMotionTranslation, float RootMotionTranslationAngle);

    /** 作用：线程安全地读取指定层方向扭转策略。@param LayerName FName，目标层。@return FSekiroLuaOrientationWarpingPolicy，策略副本。 */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Sekiro|Lua Animation")
    FSekiroLuaOrientationWarpingPolicy GetLuaAnimOrientationWarpingPolicySnapshot(FName LayerName) const;

    /** 作用：设置待与姿势决策一同应用的根运动旋转模式。@param LayerName FName，目标层。@param ModeName FName，旋转模式名称。@return bool，模式有效时为 true。 */
    UFUNCTION(BlueprintCallable, Category = "Sekiro|Lua Animation")
    bool SetLuaAnimRootMotionRotationModeByName(FName LayerName, FName ModeName);

    /** 作用：设置待与姿势决策一同应用的完整根运动旋转策略。@param LayerName FName，目标层。@param ModeName FName，旋转模式名称。@param TargetWorldYaw float，目标世界偏航角。@param MaxYawRate float，最大转向速度，度每秒。@param CompletionTimeSeconds float，完成时间，单位秒。@return bool，模式有效且成功暂存时为 true。 */
    UFUNCTION(BlueprintCallable, Category = "Sekiro|Lua Animation")
    bool SetLuaAnimRootMotionRotationPolicyByName(FName LayerName, FName ModeName, float TargetWorldYaw, float MaxYawRate, float CompletionTimeSeconds = 0.0f);

    /** 作用：设置同状态切换资产时是否保持归一化进度。@param LayerName FName，目标层。@param bPreserveNormalizedTime bool，是否保持进度。@return void，无返回值。 */
    UFUNCTION(BlueprintCallable, Category = "Sekiro|Lua Animation")
    void SetLuaAnimPreserveNormalizedTimeOnAssetChange(FName LayerName, bool bPreserveNormalizedTime);

    /** 作用：读取当前根运动旋转模式。@param LayerName FName，目标层。@return ESekiroLuaRootMotionRotationMode，当前模式。 */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Sekiro|Lua Animation")
    ESekiroLuaRootMotionRotationMode GetLuaAnimRootMotionRotationMode(FName LayerName) const;

    /** 作用：读取当前根运动旋转模式名称。@param LayerName FName，目标层。@return FName，当前模式名称。 */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Sekiro|Lua Animation")
    FName GetLuaAnimRootMotionRotationModeName(FName LayerName) const;

    /** 作用：读取当前根运动目标世界偏航角。@param LayerName FName，目标层。@return float，世界偏航角，单位为度。 */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Sekiro|Lua Animation")
    float GetLuaAnimRootMotionTargetWorldYaw(FName LayerName) const;

    /** 作用：读取当前根运动最大转向速度。@param LayerName FName，目标层。@return float，最大角速度，单位为度每秒。 */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Sekiro|Lua Animation")
    float GetLuaAnimRootMotionMaxYawRate(FName LayerName) const;

    /** 作用：读取当前根运动转向完成时间。@param LayerName FName，目标层。@return float，完成时间，单位为秒。 */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Sekiro|Lua Animation")
    float GetLuaAnimRootMotionCompletionTimeSeconds(FName LayerName) const;

    /** 作用：求值当前动画的普通浮点曲线。@param LayerName FName，目标层。@param CurveName FName，曲线名。@param DefaultValue float，求值失败时返回值。@return float，曲线值或默认值。 */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Sekiro|Lua Animation")
    float GetLuaAnimCurveValue(FName LayerName, FName CurveName, float DefaultValue) const;

    /** 作用：以环形相位方式求值当前动画曲线。@param LayerName FName，目标层。@param CurveName FName，曲线名。@param DefaultValue float，求值失败时返回值。@return float，范围为 [0,1) 的相位或默认值。 */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Sekiro|Lua Animation")
    float GetLuaAnimCircularCurveValue(FName LayerName, FName CurveName, float DefaultValue) const;

    /** 作用：在目标动画中搜索最匹配环形曲线值的归一化时间。@param AnimationPath const FString&，动画对象路径。@param CurveName FName，曲线名。@param TargetValue float，目标环形曲线值。@param BlendInputX float，混合输入 X。@param BlendInputY float，混合输入 Y。@param BlendInputZ float，混合输入 Z。@param ReferenceNormalizedTime float，同分时优先靠近的参考时间。@param SampleCount int32，离散采样数。@param DefaultValue float，搜索失败时返回值。@return float，匹配的归一化时间或默认值。 */
    UFUNCTION(BlueprintCallable, Category = "Sekiro|Lua Animation")
    float FindLuaAnimCircularCurveMatchingNormalizedTimeByPath(const FString& AnimationPath, FName CurveName, float TargetValue, float BlendInputX, float BlendInputY, float BlendInputZ, float ReferenceNormalizedTime, int32 SampleCount, float DefaultValue);

    /** 作用：求值当前动画曲线并转换为整数。@param LayerName FName，目标层。@param CurveName FName，曲线名。@param DefaultValue int32，求值失败时返回值。@return int32，四舍五入后的曲线值或默认值。 */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Sekiro|Lua Animation")
    int32 GetLuaAnimCurveIntValue(FName LayerName, FName CurveName, int32 DefaultValue) const;

    /** 作用：检查当前整数曲线是否包含指定标志。@param LayerName FName，目标层。@param CurveName FName，曲线名。@param FlagMask int32，待检查标志掩码。@return bool，至少一个标志命中时为 true。 */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Sekiro|Lua Animation")
    bool HasLuaAnimCurveFlag(FName LayerName, FName CurveName, int32 FlagMask) const;

    /** 作用：计算当前动画的归一化播放时间。@param LayerName FName，目标层。@param DefaultValue float，无法计算时返回值。@return float，范围为 [0,1] 的播放进度或默认值。 */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Sekiro|Lua Animation")
    float GetLuaAnimNormalizedTime(FName LayerName, float DefaultValue) const;

    /** 作用：计算当前动画距离非循环末帧的剩余播放时间。@param LayerName FName，目标层。@param DefaultValue float，动画或播放速率无效时的返回值。@return float，按当前播放倍率换算的剩余秒数，最小为 0。 */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Sekiro|Lua Animation")
    float GetLuaAnimRemainingTime(FName LayerName, float DefaultValue) const;

protected:
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sekiro|Lua Animation")
    bool bAutoUpdateLuaDrivenAnimation = true; // 是否在 NativeUpdateAnimation 中自动更新 Lua 动画

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sekiro|Lua Animation")
    FString DefaultLuaAnimModuleName;       // 默认 Lua 动画模块名称

    UPROPERTY(Transient, BlueprintReadOnly, Category = "Sekiro|Lua Animation")
    FString LuaAnimModuleName;            // Lua 动画模块名称

    UPROPERTY(Transient, BlueprintReadOnly, Category = "Sekiro|Lua Animation")
    FName DefaultLuaAnimLayerName = FName(TEXT("Default")); // 默认动画层名称

    UPROPERTY(Transient, BlueprintReadOnly, Category = "Sekiro|Lua Animation")
    TArray<FName> LuaAnimLayerNames;       // Lua 注册的动画层名称

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Sekiro|Lua Animation")
    FSekiroLuaAnimSnapshot LuaAnimSnapshot; // 默认动画快照

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Sekiro|Lua Animation")
    TMap<FName, FSekiroLuaAnimSnapshot> LuaAnimLayerSnapshots; // 动画层快照

    UPROPERTY(Transient)
    TMap<FString, TObjectPtr<UAnimationAsset>> LuaAnimAssetCache; // Lua 动画资源缓存

    UPROPERTY(Transient)
    TMap<int32, FSekiroLuaSequencePlayerSnapshot> LuaSequencePlayers; // 游戏线程持久 SequencePlayer 参数

    UPROPERTY(Transient)
    TMap<int32, FSekiroLuaStateResultSnapshot> LuaStateResults; // 游戏线程持久 StateResult 拓扑

    /** 作用：按覆盖、接口和默认值优先级解析 Lua 模块名。@param 无。@return FString，最终模块名。 */
    FString ResolveLuaAnimModuleName() const;
    /** 作用：将 None 层名解析为默认层名。@param LayerName FName，候选层名。@return FName，最终层名。 */
    FName ResolveLuaAnimLayerName(FName LayerName) const;
    /** 作用：调用 Lua 配置入口并注册动画层。@param 无。@return bool，配置完成或此前已完成时为 true。 */
    bool ConfigureLuaAnimation();
    /** 作用：调用 Lua 更新入口并读取返回或排队决策。@param LayerName FName，目标层。@param DeltaSeconds float，本帧秒数。@param OutDecision FSekiroLuaAnimDecision&，输出决策。@return bool，获得有效决策时为 true。 */
    bool EvaluateLuaAnimDecision(FName LayerName, float DeltaSeconds, FSekiroLuaAnimDecision& OutDecision);
    /** 作用：在锁外构建新快照并短锁发布。@param LayerName FName，目标层。@param Decision const FSekiroLuaAnimDecision&，待应用决策。@return bool，决策成功应用时为 true。 */
    bool ApplyLuaAnimDecision(FName LayerName, const FSekiroLuaAnimDecision& Decision);
    /** 作用：在锁外推进指定层动画时间并短锁发布。@param LayerName FName，目标层。@param DeltaSeconds float，本帧秒数。@return void，无返回值。 */
    void AdvanceLuaAnimSnapshot(FName LayerName, float DeltaSeconds);
    /** 作用：把当前游戏线程快照发布到自定义 Proxy 并准备稳定拓扑。@param 无。@return void，无返回值。 */
    void PublishLuaAnimSnapshotsToProxy();

private:
    /** 作用：校验并保存 Lua 在本次调用中提交的决策。@param LayerName FName，目标层。@param Decision const FSekiroLuaAnimDecision&，待排队决策。@return bool，决策有效时为 true。 */
    bool QueueLuaAnimPose(FName LayerName, const FSekiroLuaAnimDecision& Decision);
    /** 作用：在线程安全短锁内发布指定层快照并同步默认快照。@param LayerName FName，目标层。@param Snapshot const FSekiroLuaAnimSnapshot&，完整快照副本。@return void，无返回值。 */
    void PublishLuaAnimSnapshot(FName LayerName, const FSekiroLuaAnimSnapshot& Snapshot);
    /** 作用：按句柄查找可修改的持久 SequencePlayer。@param PoseLink const FSekiroLuaPoseLink&，目标节点句柄。@return FSekiroLuaSequencePlayerSnapshot*，句柄有效时返回节点地址，否则返回 nullptr。 */
    FSekiroLuaSequencePlayerSnapshot* FindLuaSequencePlayer(const FSekiroLuaPoseLink& PoseLink);
    /** 作用：按句柄查找只读持久 SequencePlayer。@param PoseLink const FSekiroLuaPoseLink&，目标节点句柄。@return const FSekiroLuaSequencePlayerSnapshot*，句柄有效时返回节点地址，否则返回 nullptr。 */
    const FSekiroLuaSequencePlayerSnapshot* FindLuaSequencePlayer(const FSekiroLuaPoseLink& PoseLink) const;
    /** 作用：按句柄查找可修改的持久 StateResult。@param PoseLink const FSekiroLuaPoseLink&，目标节点句柄。@return FSekiroLuaStateResultSnapshot*，句柄有效时返回节点地址，否则返回 nullptr。 */
    FSekiroLuaStateResultSnapshot* FindLuaStateResult(const FSekiroLuaPoseLink& PoseLink);
    /** 作用：按句柄查找只读持久 StateResult。@param PoseLink const FSekiroLuaPoseLink&，目标节点句柄。@return const FSekiroLuaStateResultSnapshot*，句柄有效时返回节点地址，否则返回 nullptr。 */
    const FSekiroLuaStateResultSnapshot* FindLuaStateResult(const FSekiroLuaPoseLink& PoseLink) const;
    /** 作用：解析 StateResult 当前连接的可修改 SequencePlayer。@param StateResultPoseLink const FSekiroLuaPoseLink&，StateResult 句柄。@return FSekiroLuaSequencePlayerSnapshot*，输入有效时返回播放器地址，否则返回 nullptr。 */
    FSekiroLuaSequencePlayerSnapshot* FindLuaStateResultInputSequencePlayer(const FSekiroLuaPoseLink& StateResultPoseLink);
    /** 作用：解析 StateResult 当前连接的只读 SequencePlayer。@param StateResultPoseLink const FSekiroLuaPoseLink&，StateResult 句柄。@return const FSekiroLuaSequencePlayerSnapshot*，输入有效时返回播放器地址，否则返回 nullptr。 */
    const FSekiroLuaSequencePlayerSnapshot* FindLuaStateResultInputSequencePlayer(const FSekiroLuaPoseLink& StateResultPoseLink) const;
    /** 作用：把目标层全部持久节点复制到图快照并同步兼容调试字段。@param LayerName FName，目标动画层。@param Snapshot FSekiroLuaAnimSnapshot&，待填充并发布的层快照。@return void，无返回值。 */
    void BuildLuaPoseGraphSnapshot(FName LayerName, FSekiroLuaAnimSnapshot& Snapshot) const;
    /** 作用：为 Proxy 复制按层排序的不可变快照。@param OutSnapshots TArray<FSekiroLuaAnimProxyLayerSnapshot>&，输出层快照。@param OutDefaultLayerName FName&，输出默认层。@return void，无返回值。 */
    void CopyLuaAnimSnapshotsForProxy(TArray<FSekiroLuaAnimProxyLayerSnapshot>& OutSnapshots, FName& OutDefaultLayerName) const;
    /** 作用：应用 Proxy PostUpdate 回传的原生播放器时间和活动节点。@param RuntimeStates const TArray<FSekiroLuaPoseGraphRuntimeState>&，原生运行时状态。@return void，无返回值。 */
    void ApplyLuaAnimProxyRuntimeStates(const TArray<FSekiroLuaPoseGraphRuntimeState>& RuntimeStates);

    bool bLuaAnimConfigured = false;       // Lua 动画配置是否已执行
    bool bLuaAnimModuleNameOverridden = false; // Lua 模块名是否被运行时手动覆盖

    TMap<FName, FSekiroLuaAnimDecision> PendingLuaAnimDecisions; // Lua 通过 C++ 接口提交的待应用姿势
    TMap<FName, FSekiroLuaRootMotionRotationPolicy> PendingLuaAnimRootMotionRotationPolicies; // 与本帧姿势决策一同原子应用的根运动旋转策略
    TMap<FName, bool> PendingLuaAnimPreserveNormalizedTime; // 同状态循环资产切换时是否保留归一化播放进度
    TSet<FName> PublishedLuaPoseGraphLayers; // 当前 Lua 求值中已发布 OutputPose 的动画层
    TMap<int32, FName> LuaSequencePlayerLayers; // NodeId 到所属动画层的索引
    TMap<int32, FName> LuaStateResultLayers; // StateResult NodeId 到所属动画层的索引
    TMap<FName, int32> LuaPoseGraphTopologySerials; // 每层拓扑变化序号
    TSet<int32> ActiveLuaPoseNodeIds;     // Proxy 上一帧回传的活动节点编号
    int32 ActiveLuaPoseGraphGeneration = 0; // 活动节点集合所属图代次
    int32 LuaPoseGraphGeneration = 1;       // 当前 Pose Graph 代次
    int32 NextLuaPoseNodeId = 0;            // 下一持久节点编号
    mutable FRWLock LuaAnimSnapshotLock; // 仅保护动画层快照容器和默认快照的复制与发布

    friend struct FSekiroLuaAnimInstanceProxy;
};
