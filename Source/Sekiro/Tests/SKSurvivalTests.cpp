#include "AbilitySystem/SKAbilitySystemComponent.h"
#include "AbilitySystem/Attributes/SKCharacterAttributeSet.h"
#include "Character/SKSurvivalComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
    /** 只在显式执行自动化测试时创建隔离宿主，不启动 BeginPlay、Tick 或 Lua 工作流。 */
    struct FSKSurvivalContractFixture
    {
        UWorld* World = nullptr; // 本测试专用且不推进时间的世界
        AActor* Owner = nullptr; // 不具有项目角色行为的隔离宿主
        USKAbilitySystemComponent* ASC = nullptr; // 属性与资源策略入口
        USKCharacterAttributeSet* CharacterSet = nullptr; // 统一生命、战斗与躯干属性存储
        USKSurvivalComponent* Survival = nullptr; // 被验证的原生机械状态组件

        explicit FSKSurvivalContractFixture(bool bRequirePolicy = true);
        ~FSKSurvivalContractFixture();
        bool Initialize(const FSKAttributeInitialization& Values);
        FActiveGameplayEffectHandle AddStatistic(const FGameplayAttribute& Attribute, float Amount);
    };

    /**
     * 游戏线程创建独立 Actor、ASC、属性集与 Survival，保持组件未开始运行。
     * @param bRequirePolicy 是否要求初始化前必须存在资源策略；false 用于验证晚绑定。
     * 只注册组件和 ActorInfo，不载入关卡、不调用 BeginPlay、Tick 或任何 Lua 覆盖接口。
     */
    FSKSurvivalContractFixture::FSKSurvivalContractFixture(bool bRequirePolicy)
    {
        World = UWorld::CreateWorld(EWorldType::Game, false);
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        Owner = World->SpawnActor<AActor>();
        ASC = NewObject<USKAbilitySystemComponent>(Owner);
        Owner->AddInstanceComponent(ASC);
        ASC->RegisterComponent();
        CharacterSet = NewObject<USKCharacterAttributeSet>(Owner);
        ASC->AddAttributeSetSubobject(CharacterSet);
        ASC->InitAbilityActorInfo(Owner, Owner);
        if (bRequirePolicy) ASC->RequireResourcePolicy();
        Survival = NewObject<USKSurvivalComponent>(Owner);
        Owner->AddInstanceComponent(Survival);
        Survival->RegisterComponent();
    }

    /** 游戏线程先解绑策略再销毁测试世界及上下文；不触发玩法死亡或推进世界。 */
    FSKSurvivalContractFixture::~FSKSurvivalContractFixture()
    {
        ASC->UnregisterResourcePolicy(Survival);
        GEngine->DestroyWorldContext(World);
        World->DestroyWorld(false);
    }

    /**
     * 游戏线程按正常顺序绑定 Survival 并提交完整测试数值。
     * @param Values 有效且完整的初始配置，函数不持有引用。
     * @return 策略绑定和 GAS 初始化均成功时返回 true；不主动发布 BeginPlay 前缓存事件。
     */
    bool FSKSurvivalContractFixture::Initialize(const FSKAttributeInitialization& Values)
    {
        return Survival->BindAttributeSystem(ASC) && ASC->InitializeFromValues(Values);
    }

    /**
     * 游戏线程直接施加测试专用无限统计属性 GE，覆盖底层聚合与移除路径。
     * @param Attribute 测试需要观察的非资源统计字段，不持有外部引用。
     * @param Amount 有限增减量；仅用于测试，不写入项目默认配置。
     * @return 本测试 ASC 上的活动效果句柄，可用于显式移除。
     */
    FActiveGameplayEffectHandle FSKSurvivalContractFixture::AddStatistic(
        const FGameplayAttribute& Attribute, float Amount)
    {
        UGameplayEffect* Effect = NewObject<UGameplayEffect>(World);
        Effect->DurationPolicy = EGameplayEffectDurationType::Infinite;
        FGameplayModifierInfo Modifier;
        Modifier.Attribute = Attribute;
        Modifier.ModifierOp = EGameplayModOp::Additive;
        Modifier.ModifierMagnitude = FScalableFloat(Amount);
        Effect->Modifiers.Add(Modifier);
        const FGameplayEffectSpec Spec(Effect, ASC->MakeEffectContext(), 1.f);
        return ASC->ApplyGameplayEffectSpecToSelf(Spec);
    }

    /** 任意线程返回独立的完整测试配置；不依赖 Lua 默认表或其他测试翻译单元的工厂。 */
    FSKAttributeInitialization MakeSKSurvivalContractAttributes()
    {
        FSKAttributeInitialization Values;
        Values.MaxHealth = 100.f;
        Values.InitialHealth = 100.f;
        Values.AttackPower = 20.f;
        Values.Armor = 10.f;
        Values.MaxPosture = 80.f;
        Values.InitialPosture = 0.f;
        Values.PostureRecoveryRate = 8.f;
        Values.PostureRecoveryDelay = 0.75f;
        Values.PostureRecoveryRampDuration = 4.f;
        Values.PostureRecoveryMinRateScale = 1.f / 6.f;
        Values.PostureRecoveryMaxRateScale = 1.f;
        Values.PostureDeflectSuccessCapRatio = 0.98f;
        Values.PostureAttackCapRatio = 0.98f;
        Values.PostureMinGainScale = 0.35f;
        Values.PostureGainFalloffExponent = 1.25f;
        Values.PostureGainDeflectSuccess = 6.f;
        Values.PostureGainGuarded = 14.f;
        Values.PostureGainDeflectFailed = 24.f;
        Values.PostureGainAttackSuccess = 4.f;
        Values.PostureGainAttackGuarded = 10.f;
        Values.PostureGainAttackDeflected = 18.f;
        Values.PostureStrengthLight = 1.f;
        Values.PostureStrengthHeavy = 1.35f;
        Values.PostureStrengthThrust = 1.5f;
        Values.PostureStrengthSpecial = 1.75f;
        Values.PostureBreakMinimumDuration = 1.5f;
        Values.PostureBreakBlendInTime = 0.04f;
        Values.PostureBreakBlendOutTime = 0.12f;
        Values.PostureRecoveryTargetRatio = 0.f;
        Values.RevivePostureRatio = 0.f;
        return Values;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSKSurvivalInitializationOrderTest,
    "Sekiro.Survival.InitializationOrder", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 游戏线程验证配置标签、必需策略缺失、正常绑定、解绑后的失败关闭，以及已就绪 ASC 的晚绑定。
 * @param Parameters 自动化框架参数，本测试不使用。
 * @return 流程完成返回 true，断言失败由自动化框架记录；不运行任何组件生命周期。
 */
bool FSKSurvivalInitializationOrderTest::RunTest(const FString& Parameters)
{
    FSKSurvivalContractFixture Required;
    const FSKAttributeInitialization Values = MakeSKSurvivalContractAttributes();
    const FName RequiredTagNames[] = {
        TEXT("State.Life.Dying"), TEXT("State.Life.Dead"), TEXT("State.Life.Reviving"),
        TEXT("State.Posture.Broken"), TEXT("State.Damage.Immune"), TEXT("State.Posture.Immune"), TEXT("State.Revive.Blocked")
    };
    bool bAllTagsLoaded = true;
    for (const FName TagName : RequiredTagNames)
    {
        const bool bLoaded = FGameplayTag::RequestGameplayTag(TagName, false).IsValid();
        TestTrue(FString::Printf(TEXT("必需标签已由配置字典加载：%s"), *TagName.ToString()), bLoaded);
        bAllTagsLoaded = bAllTagsLoaded && bLoaded;
    }
    if (!bAllTagsLoaded)
    {
        AddExpectedError(TEXT("Survival 必需 GameplayTag 缺失"), EAutomationExpectedErrorFlags::Contains, 1);
        TestFalse(TEXT("缺标签时拒绝绑定策略"), Required.Survival->BindAttributeSystem(Required.ASC));
        TestFalse(TEXT("缺标签时不注册部分策略"), Required.ASC->IsResourcePolicyBoundTo(Required.Survival));
        TestFalse(TEXT("缺标签时行动门禁保持关闭"), Required.Survival->CanAct());
        return false;
    }
    TestFalse(TEXT("必需策略缺失时初始化拒绝"), Required.ASC->InitializeFromValues(Values));
    TestFalse(TEXT("缺策略不提前产生生命状态"), Required.Survival->IsSurvivalReady());
    TestEqual(TEXT("未初始化伤害拒绝"), Required.ASC->ApplyHealthDamage(10.f).Code, ESKNumericResultCode::NotReady);
    TestTrue(TEXT("先绑定策略再初始化"), Required.Initialize(Values));
    TestEqual(TEXT("生存流程只依赖一个统一角色属性集"), Required.ASC->GetSpawnedAttributes().Num(), 1);
    TestTrue(TEXT("初始化结束Survival已就绪"), Required.Survival->IsSurvivalReady());
    TestTrue(TEXT("正常初始生命允许行动"), Required.Survival->CanAct());
    TestTrue(TEXT("同一绑定幂等"), Required.Survival->BindAttributeSystem(Required.ASC));
    Required.ASC->UnregisterResourcePolicy(Required.Survival);
    TestEqual(TEXT("策略卸载后资源入口失败关闭"), Required.ASC->ApplyHealthDamage(10.f).Code,
        ESKNumericResultCode::PolicyRejected);
    TestEqual(TEXT("策略卸载未改变生命"), Required.CharacterSet->GetHealth(), 100.f);

    FSKSurvivalContractFixture Late(false);
    TestTrue(TEXT("独立ASC可先初始化"), Late.ASC->InitializeFromValues(Values));
    TestFalse(TEXT("未绑定Survival仍未就绪"), Late.Survival->IsSurvivalReady());
    TestTrue(TEXT("已就绪ASC支持晚绑定"), Late.Survival->BindAttributeSystem(Late.ASC));
    TestTrue(TEXT("晚绑定立即读取完整生命状态"), Late.Survival->IsAlive());
    TestEqual(TEXT("晚绑定首轮生命序号"), Late.Survival->GetLifeSerial(), static_cast<int64>(1));
    TestFalse(TEXT("测试没有开始组件运行"), Late.Survival->HasBegunPlay());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSKSurvivalInitialStatesTest,
    "Sekiro.Survival.InitialStates", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 游戏线程验证初始零生命、初始满躯干及二者同时出现时的机械状态分类。
 * @param Parameters 自动化框架参数，本测试不使用。
 * @return 流程完成返回 true；不断言 BeginPlay 前尚未发布的事件次数。
 */
bool FSKSurvivalInitialStatesTest::RunTest(const FString& Parameters)
{
    FSKAttributeInitialization Values = MakeSKSurvivalContractAttributes();
    Values.InitialHealth = 0.f;
    FSKSurvivalContractFixture Dead;
    TestTrue(TEXT("零生命初始配置有效"), Dead.Initialize(Values));
    TestTrue(TEXT("零生命直接分类为Dead"), Dead.Survival->IsDead());
    TestFalse(TEXT("初始死亡没有伪造死亡过程令牌"),
        Dead.Survival->IsTransitionTokenValid(Dead.Survival->GetDeathToken()));
    TestTrue(TEXT("初始Dead允许外部请求回生"), Dead.Survival->CanBeginRevive());

    Values.InitialHealth = Values.MaxHealth;
    Values.InitialPosture = Values.MaxPosture;
    FSKSurvivalContractFixture Broken;
    TestTrue(TEXT("满躯干初始配置有效"), Broken.Initialize(Values));
    TestTrue(TEXT("初始满躯干建立Broken"), Broken.Survival->IsPostureBroken());
    TestFalse(TEXT("初始Broken关闭行动"), Broken.Survival->CanAct());
    TestTrue(TEXT("初始Broken具有有效令牌"),
        Broken.Survival->IsTransitionTokenValid(Broken.Survival->GetPostureBreakToken()));

    Values.InitialHealth = 0.f;
    FSKSurvivalContractFixture Both;
    TestTrue(TEXT("零生命和满躯干允许作为初始资源"), Both.Initialize(Values));
    TestTrue(TEXT("初始资源分类以死亡为先"), Both.Survival->IsDead());
    TestFalse(TEXT("非Alive不建立崩溃流程"), Both.Survival->IsPostureBroken());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSKSurvivalDeathPriorityTest,
    "Sekiro.Survival.DeathPriority", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 游戏线程验证复合伤害的最终状态只进入死亡、直接 ASC 治疗门禁和死亡收尾幂等。
 * @param Parameters 自动化框架参数，本测试不使用。
 * @return 流程完成返回 true；检查真实双通道变化，不假设通用事务回滚。
 */
bool FSKSurvivalDeathPriorityTest::RunTest(const FString& Parameters)
{
    FSKSurvivalContractFixture Fixture;
    TestTrue(TEXT("建立正常生命"), Fixture.Initialize(MakeSKSurvivalContractAttributes()));
    const FSKSurvivalImpactResult Impact = Fixture.Survival->ApplySurvivalImpact(150.f, 100.f, Fixture.Owner);
    TestEqual(TEXT("双资源GE提交成功"), Impact.Numeric.Code, ESKNumericResultCode::Applied);
    TestEqual(TEXT("保留生命请求量"), Impact.Numeric.RequestedHealthDamage, 150.f);
    TestEqual(TEXT("保留躯干请求量"), Impact.Numeric.RequestedPostureDamage, 100.f);
    TestEqual(TEXT("生命实际扣除按剩余资源截断"), Impact.Numeric.ActualHealthDamage, 100.f);
    TestEqual(TEXT("躯干实际增长按上限截断"), Impact.Numeric.ActualPostureDamage, 80.f);
    TestTrue(TEXT("复合请求首次触发死亡"), Impact.bDeathStarted);
    TestFalse(TEXT("同笔致死不另建立Broken"), Impact.bPostureBroken);
    TestEqual(TEXT("完整提交返回时已是Dying"), Impact.Snapshot.LifeState, ESKLifeState::Dying);
    TestFalse(TEXT("Dying尚非Dead"), Fixture.Survival->IsDead());
    TestFalse(TEXT("致死后行动关闭"), Fixture.Survival->CanAct());
    TestEqual(TEXT("Dying直接ASC治疗被拒绝"), Fixture.ASC->RestoreHealth(50.f).Code,
        ESKNumericResultCode::PolicyRejected);
    TestEqual(TEXT("Dying不能提前回生"), Fixture.Survival->BeginRevive(Fixture.Survival->GetLifeSerial()).Code,
        ESKSurvivalResultCode::InvalidState);

    const FSKSurvivalTransitionToken DeathToken = Fixture.Survival->GetDeathToken();
    TestEqual(TEXT("完成死亡收尾"), Fixture.Survival->FinishDeath(DeathToken).Code, ESKSurvivalResultCode::Applied);
    TestTrue(TEXT("收尾后成为Dead"), Fixture.Survival->IsDead());
    TestEqual(TEXT("相同死亡令牌收尾幂等"), Fixture.Survival->FinishDeath(DeathToken).Code, ESKSurvivalResultCode::NoChange);
    TestEqual(TEXT("Dead普通治疗也拒绝"), Fixture.Survival->RestoreHealth(50.f).Code, ESKNumericResultCode::PolicyRejected);
    const FSKSurvivalImpactResult Rejected = Fixture.Survival->ApplySurvivalImpact(10.f, 10.f);
    TestEqual(TEXT("死亡整体拒绝后续复合伤害"), Rejected.Numeric.Code, ESKNumericResultCode::PolicyRejected);
    TestEqual(TEXT("死亡生命通道拒绝"), Rejected.Numeric.HealthChannelCode, ESKNumericResultCode::PolicyRejected);
    TestEqual(TEXT("死亡躯干通道拒绝"), Rejected.Numeric.PostureChannelCode, ESKNumericResultCode::PolicyRejected);
    TestFalse(TEXT("后续请求不重复致死"), Rejected.bDeathStarted);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSKSurvivalPostureRecoveryTest,
    "Sekiro.Survival.PostureRecovery", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 游戏线程验证Broken的资源清零、通道独立性、GAS时长门禁与带令牌恢复。
 * @param Parameters 自动化框架参数，本测试不使用。
 * @return 流程完成返回 true；用统计GE改变最短时长，不推进世界时间或播放动画。
 */
bool FSKSurvivalPostureRecoveryTest::RunTest(const FString& Parameters)
{
    FSKSurvivalContractFixture Fixture;
    FSKAttributeInitialization Values = MakeSKSurvivalContractAttributes();
    Values.PostureRecoveryTargetRatio = 0.25f;
    TestTrue(TEXT("建立躯干恢复配置"), Fixture.Initialize(Values));
    TestEqual(TEXT("普通伤害填满躯干"), Fixture.Survival->ApplyPostureDamage(80.f).Code, ESKNumericResultCode::Applied);
    const FSKSurvivalTransitionToken BreakToken = Fixture.Survival->GetPostureBreakToken();
    TestTrue(TEXT("满躯干产生崩溃"), Fixture.Survival->IsPostureBroken());
    TestEqual(TEXT("未到最短持续时间拒绝恢复"), Fixture.Survival->CompletePostureRecovery(BreakToken).Code,
        ESKSurvivalResultCode::Blocked);
    TestEqual(TEXT("持有效崩溃令牌可清零资源"), Fixture.Survival->ResetBrokenPosture(BreakToken).Code,
        ESKSurvivalResultCode::Applied);
    TestEqual(TEXT("躯干资源已清零"), Fixture.Survival->GetPosture(), 0.f);
    TestTrue(TEXT("清零不解除Broken"), Fixture.Survival->IsPostureBroken());
    TestFalse(TEXT("清零后仍不允许行动"), Fixture.Survival->CanAct());
    TestEqual(TEXT("Broken普通Reset入口仍拒绝"), Fixture.ASC->ResetPosture().Code, ESKNumericResultCode::PolicyRejected);

    const FSKSurvivalImpactResult Impact = Fixture.Survival->ApplySurvivalImpact(10.f, 20.f);
    TestEqual(TEXT("Broken复合请求保留生命通道"), Impact.Numeric.ActualHealthDamage, 10.f);
    TestEqual(TEXT("Broken躯干通道拒绝"), Impact.Numeric.PostureChannelCode, ESKNumericResultCode::PolicyRejected);
    TestEqual(TEXT("Broken躯干不重复累积"), Impact.Numeric.ActualPostureDamage, 0.f);
    TestFalse(TEXT("Broken后续复合请求不重复崩溃"), Impact.bPostureBroken);

    const FActiveGameplayEffectHandle Duration = Fixture.AddStatistic(
        USKCharacterAttributeSet::GetPostureBreakMinimumDurationAttribute(), -1.5f);
    TestTrue(TEXT("时长增益存在"), Duration.IsValid());
    TestEqual(TEXT("恢复读取最新GAS最短时长"), Fixture.Survival->CompletePostureRecovery(BreakToken).Code,
        ESKSurvivalResultCode::Applied);
    TestEqual(TEXT("恢复目标读取GAS比例"), Fixture.Survival->GetPosture(), 20.f);
    TestFalse(TEXT("成功恢复解除Broken"), Fixture.Survival->IsPostureBroken());
    TestTrue(TEXT("成功恢复重新允许行动"), Fixture.Survival->CanAct());
    TestEqual(TEXT("已完成崩溃令牌失效"), Fixture.Survival->ResetBrokenPosture(BreakToken).Code,
        ESKSurvivalResultCode::StaleTransition);
    Fixture.Survival->ApplyPostureDamage(80.f);
    const FSKSurvivalTransitionToken Cancelled = Fixture.Survival->GetPostureBreakToken();
    Fixture.Survival->ApplyHealthDamage(100.f);
    TestFalse(TEXT("死亡取消现存崩溃"), Fixture.Survival->IsPostureBroken());
    TestFalse(TEXT("死亡后旧崩溃令牌失效"), Fixture.Survival->IsTransitionTokenValid(Cancelled));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSKSurvivalReviveTokensTest,
    "Sekiro.Survival.ReviveTokens", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 游戏线程验证回生开始/取消/完成、最新上限、生命轮次和跨拥有者令牌隔离。
 * @param Parameters 自动化框架参数，本测试不使用。
 * @return 流程完成返回 true；不消费回生费用、不清除无关效果、不执行演出。
 */
bool FSKSurvivalReviveTokensTest::RunTest(const FString& Parameters)
{
    FSKAttributeInitialization Values = MakeSKSurvivalContractAttributes();
    Values.InitialHealth = 0.f;
    Values.RevivePostureRatio = 0.25f;
    FSKSurvivalContractFixture Fixture;
    TestTrue(TEXT("建立初始死亡状态"), Fixture.Initialize(Values));
    const int64 OriginalLife = Fixture.Survival->GetLifeSerial();
    TestEqual(TEXT("旧生命轮次不能开始回生"), Fixture.Survival->BeginRevive(OriginalLife + 1).Code,
        ESKSurvivalResultCode::StaleTransition);
    const FSKSurvivalTransitionResult First = Fixture.Survival->BeginRevive(OriginalLife);
    TestEqual(TEXT("开始回生"), First.Code, ESKSurvivalResultCode::Applied);
    TestEqual(TEXT("回生开始不提前恢复生命"), Fixture.Survival->GetHealth(), 0.f);
    TestFalse(TEXT("回生中行动禁用"), Fixture.Survival->CanAct());
    TestEqual(TEXT("回生中普通治疗拒绝"), Fixture.ASC->RestoreHealth(10.f).Code, ESKNumericResultCode::PolicyRejected);
    TestEqual(TEXT("无效回生生命比例拒绝"), Fixture.Survival->CompleteRevive(First.Token, 0.f).Code,
        ESKSurvivalResultCode::InvalidInput);
    TestEqual(TEXT("无效比例保持Reviving"), Fixture.Survival->GetLifeState(), ESKLifeState::Reviving);
    TestEqual(TEXT("取消回生成功"), Fixture.Survival->CancelRevive(First.Token).Code, ESKSurvivalResultCode::Applied);
    TestTrue(TEXT("取消回生回到Dead"), Fixture.Survival->IsDead());
    TestEqual(TEXT("取消后生命为零"), Fixture.Survival->GetHealth(), 0.f);
    TestEqual(TEXT("取消后躯干为零"), Fixture.Survival->GetPosture(), 0.f);
    TestEqual(TEXT("取消令牌不能再完成"), Fixture.Survival->CompleteRevive(First.Token, 1.f).Code,
        ESKSurvivalResultCode::StaleTransition);

    const FSKSurvivalTransitionResult Second = Fixture.Survival->BeginRevive(OriginalLife);
    TestEqual(TEXT("重新开始回生"), Second.Code, ESKSurvivalResultCode::Applied);
    TestTrue(TEXT("新回生使用新过程编号"), Second.Token.TransitionSerial > First.Token.TransitionSerial);
    Fixture.AddStatistic(USKCharacterAttributeSet::GetMaxHealthAttribute(), 100.f);
    Fixture.AddStatistic(USKCharacterAttributeSet::GetMaxPostureAttribute(), 20.f);
    const FSKSurvivalTransitionResult Completed = Fixture.Survival->CompleteRevive(Second.Token, 0.5f);
    TestEqual(TEXT("回生资源提交成功"), Completed.Code, ESKSurvivalResultCode::Applied);
    TestTrue(TEXT("回生进入Alive"), Fixture.Survival->IsAlive());
    TestEqual(TEXT("回生读取提交时最新生命上限"), Fixture.Survival->GetHealth(), 100.f);
    TestEqual(TEXT("回生读取最新躯干上限和GAS比例"), Fixture.Survival->GetPosture(), 25.f);
    TestEqual(TEXT("回生成功递增生命轮次"), Fixture.Survival->GetLifeSerial(), OriginalLife + 1);
    TestEqual(TEXT("重复完成旧回生令牌拒绝"), Fixture.Survival->CompleteRevive(Second.Token, 1.f).Code,
        ESKSurvivalResultCode::StaleTransition);
    TestEqual(TEXT("旧令牌不能再次恢复生命"), Fixture.Survival->GetHealth(), 100.f);

    FSKSurvivalContractFixture Other;
    TestTrue(TEXT("建立另一拥有者"), Other.Initialize(Values));
    const FSKSurvivalTransitionResult OtherRevive = Other.Survival->BeginRevive(Other.Survival->GetLifeSerial());
    TestFalse(TEXT("拥有者身份禁止跨角色令牌"), Fixture.Survival->IsTransitionTokenValid(OtherRevive.Token));
    TestEqual(TEXT("跨角色令牌完成回生拒绝"), Other.Survival->CompleteRevive(Second.Token, 1.f).Code,
        ESKSurvivalResultCode::StaleTransition);
    TestEqual(TEXT("跨角色请求不改变对方资源"), Other.Survival->GetHealth(), 0.f);
    TestEqual(TEXT("对方自身令牌仍能取消"), Other.Survival->CancelRevive(OtherRevive.Token).Code,
        ESKSurvivalResultCode::Applied);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSKSurvivalReentrantRequestsTest,
    "Sekiro.Survival.ReentrantRequests", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 游戏线程验证底层属性同步回调内的数值和公开生命周期请求均拒绝重入。
 * @param Parameters 自动化框架参数，本测试不使用。
 * @return 流程完成返回 true；仅注册原生Lambda，不依赖Lua或动态委托测试资产。
 */
bool FSKSurvivalReentrantRequestsTest::RunTest(const FString& Parameters)
{
    FSKSurvivalContractFixture Fixture;
    TestTrue(TEXT("初始化重入测试"), Fixture.Initialize(MakeSKSurvivalContractAttributes()));
    bool bObservedCallback = false;
    ESKNumericResultCode NestedNumeric = ESKNumericResultCode::Applied;
    ESKSurvivalResultCode NestedTransition = ESKSurvivalResultCode::Applied;
    const FDelegateHandle Handle = Fixture.ASC->GetGameplayAttributeValueChangeDelegate(
        USKCharacterAttributeSet::GetHealthAttribute()).AddLambda(
        [&Fixture, &bObservedCallback, &NestedNumeric, &NestedTransition](const FOnAttributeChangeData& Data)
        {
            bObservedCallback = true;
            NestedNumeric = Fixture.Survival->RestoreHealth(10.f).Code;
            NestedTransition = Fixture.Survival->BeginRevive(Fixture.Survival->GetLifeSerial()).Code;
        });
    const FSKNumericResult Result = Fixture.Survival->ApplyHealthDamage(100.f);
    Fixture.ASC->GetGameplayAttributeValueChangeDelegate(USKCharacterAttributeSet::GetHealthAttribute()).Remove(Handle);
    TestTrue(TEXT("确实进入属性同步回调"), bObservedCallback);
    TestEqual(TEXT("回调内资源请求拒绝重入"), NestedNumeric, ESKNumericResultCode::Reentrant);
    TestEqual(TEXT("回调内生命周期请求拒绝重入"), NestedTransition, ESKSurvivalResultCode::Reentrant);
    TestEqual(TEXT("外层实际伤害未混入递归治疗"), Result.ActualAmount, 100.f);
    TestEqual(TEXT("外层提交后生命状态已收敛"), Fixture.Survival->GetLifeState(), ESKLifeState::Dying);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSKSurvivalMaximumChangesTest,
    "Sekiro.Survival.MaximumChanges", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 游戏线程验证统计GE降低躯干上限立即触发崩溃，移除增益不隐式解除流程。
 * @param Parameters 自动化框架参数，本测试不使用。
 * @return 流程完成返回 true；显式移除模拟聚合变化，不推进世界计时器。
 */
bool FSKSurvivalMaximumChangesTest::RunTest(const FString& Parameters)
{
    FSKSurvivalContractFixture Fixture;
    FSKAttributeInitialization Values = MakeSKSurvivalContractAttributes();
    Values.InitialPosture = 60.f;
    Values.PostureBreakMinimumDuration = 0.f;
    TestTrue(TEXT("初始化未满躯干"), Fixture.Initialize(Values));
    TestTrue(TEXT("上限下降前允许行动"), Fixture.Survival->CanAct());
    const FActiveGameplayEffectHandle Decrease = Fixture.AddStatistic(
        USKCharacterAttributeSet::GetMaxPostureAttribute(), -40.f);
    TestTrue(TEXT("降低上限统计效果已应用"), Decrease.IsValid());
    TestEqual(TEXT("上限下降后资源截断"), Fixture.Survival->GetPosture(), 40.f);
    TestEqual(TEXT("基础资源也被实际截断"), Fixture.CharacterSet->Posture.GetBaseValue(), 40.f);
    TestTrue(TEXT("上限下降使躯干满值立即Broken"), Fixture.Survival->IsPostureBroken());
    const FSKSurvivalTransitionToken BreakToken = Fixture.Survival->GetPostureBreakToken();
    TestTrue(TEXT("移除统计效果"), Fixture.ASC->RemoveAttributeEffect(Decrease));
    TestEqual(TEXT("移除后上限恢复"), Fixture.Survival->GetMaxPosture(), 80.f);
    TestEqual(TEXT("移除上限效果不回补已截断资源"), Fixture.Survival->GetPosture(), 40.f);
    TestTrue(TEXT("上限恢复不自动结束Broken"), Fixture.Survival->IsPostureBroken());
    TestTrue(TEXT("已有崩溃令牌仍有效"), Fixture.Survival->IsTransitionTokenValid(BreakToken));
    TestEqual(TEXT("零最短时长允许显式完成恢复"), Fixture.Survival->CompletePostureRecovery(BreakToken).Code,
        ESKSurvivalResultCode::Applied);
    TestTrue(TEXT("显式恢复后允许行动"), Fixture.Survival->CanAct());
    return true;
}

#endif
