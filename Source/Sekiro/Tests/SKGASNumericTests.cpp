#include "AbilitySystem/SKAbilitySystemComponent.h"
#include "AbilitySystem/SKAttributeProfile.h"
#include "AbilitySystem/SKNumericGameplayEffect.h"
#include "AbilitySystem/Attributes/SKCharacterAttributeSet.h"
#include "Curves/CurveFloat.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"
#include "UObject/UnrealType.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
    /** 只在显式运行测试时创建隔离世界；不载入项目关卡或启动 PIE。 */
    struct FSKGASNumericFixture
    {
        UWorld* World = nullptr; // 测试专用世界
        USKAbilitySystemComponent* ASC = nullptr; // 测试角色 ASC
        USKCharacterAttributeSet* CharacterSet = nullptr; // 统一生命、战斗与躯干属性存储

        FSKGASNumericFixture();
        ~FSKGASNumericFixture();
        FActiveGameplayEffectHandle AddStatistic(const FGameplayAttribute& Attribute, float Amount);
    };

    /** 游戏线程创建独立 GAS 宿主及属性集，不运行地图、AI 或角色脚本。 */
    FSKGASNumericFixture::FSKGASNumericFixture()
    {
        World = UWorld::CreateWorld(EWorldType::Game, false);
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        AActor* Owner = World->SpawnActor<AActor>();
        ASC = NewObject<USKAbilitySystemComponent>(Owner);
        Owner->AddInstanceComponent(ASC);
        ASC->RegisterComponent();
        CharacterSet = NewObject<USKCharacterAttributeSet>(Owner);
        ASC->AddAttributeSetSubobject(CharacterSet);
        ASC->InitAbilityActorInfo(Owner, Owner);
    }

    /** 游戏线程销毁测试专用世界及上下文，不影响其他编辑器世界。 */
    FSKGASNumericFixture::~FSKGASNumericFixture()
    {
        GEngine->DestroyWorldContext(World);
        World->DestroyWorld(false);
    }

    /**
     * 游戏线程创建测试专用无限统计属性效果，验证底层 GAS 聚合时的 AttributeSet 约束。
     * @param Attribute 本测试属性集中的非资源统计字段，不持有外部引用。
     * @param Amount 有限的属性增减量；仅测试数据，不写入生产默认配置。
     * @return 新活动效果句柄，可用于移除验证。
     */
    FActiveGameplayEffectHandle FSKGASNumericFixture::AddStatistic(const FGameplayAttribute& Attribute, float Amount)
    {
        UGameplayEffect* Effect = NewObject<UGameplayEffect>(World);
        Effect->DurationPolicy = EGameplayEffectDurationType::Infinite;
        FGameplayModifierInfo Modifier;
        Modifier.Attribute = Attribute;
        Modifier.ModifierOp = EGameplayModOp::Additive;
        Modifier.ModifierMagnitude = FScalableFloat(Amount);
        Effect->Modifiers.Add(Modifier);
        FGameplayEffectSpec Spec(Effect, ASC->MakeEffectContext(), 1.f);
        return ASC->ApplyGameplayEffectSpecToSelf(Spec);
    }

    /** 返回测试用完整值配置；纯值工厂，可在任意线程调用。 */
    FSKAttributeInitialization MakeSKTestAttributes()
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSKGASInitializationTest,
    "Sekiro.GAS.Numeric.Initialization", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 游戏线程验证初始配置缺失、幂等性和即时效果无活动句柄的成功语义。
 * @param Parameters 自动化框架参数，当前测试不使用。
 * @return 测试流程完成返回 true，失败由 Test 系列断言记录。
 */
bool FSKGASInitializationTest::RunTest(const FString& Parameters)
{
    FSKGASNumericFixture Fixture;
    TestEqual(TEXT("角色只注册一个统一属性集"), Fixture.ASC->GetSpawnedAttributes().Num(), 1);
    TestFalse(TEXT("初始未就绪"), Fixture.ASC->IsAttributesReady());
    TestEqual(TEXT("未就绪伤害被拒绝"), Fixture.ASC->ApplyHealthDamage(1.f).Code, ESKNumericResultCode::NotReady);
    TestFalse(TEXT("纯值默认配置无效"), FSKAttributeInitialization().IsValid());
    FSKAttributeInitialization InvalidValues = MakeSKTestAttributes();
    InvalidValues.PostureGainFalloffExponent = 0.f;
    TestFalse(TEXT("零衰减指数拒绝"), InvalidValues.IsValid());
    InvalidValues = MakeSKTestAttributes();
    InvalidValues.PostureRecoveryMinRateScale = InvalidValues.PostureRecoveryMaxRateScale + 1.f;
    TestFalse(TEXT("初始化恢复倍率倒置拒绝"), InvalidValues.IsValid());
    InvalidValues = MakeSKTestAttributes();
    InvalidValues.PostureAttackCapRatio = 1.f;
    TestFalse(TEXT("非崩溃封顶必须小于一"), InvalidValues.IsValid());
    InvalidValues = MakeSKTestAttributes();
    InvalidValues.RevivePostureRatio = 1.f;
    TestFalse(TEXT("回生躯干目标不得满值"), InvalidValues.IsValid());
    InvalidValues = MakeSKTestAttributes();
    InvalidValues.PostureStrengthHeavy = std::numeric_limits<float>::infinity();
    TestFalse(TEXT("强度非有限值拒绝"), InvalidValues.IsValid());
    InvalidValues = MakeSKTestAttributes();
    InvalidValues.PostureRecoveryRampDuration = 0.f;
    TestTrue(TEXT("零恢复渐进时长有效"), InvalidValues.IsValid());
    const FSKAttributeInitialization Values = MakeSKTestAttributes();
    TestTrue(TEXT("完整配置初始化"), Fixture.ASC->InitializeFromValues(Values));
    const FSKAttributeSnapshot Snapshot = Fixture.ASC->GetAttributeSnapshot();
    const TArray<FGameplayAttribute> ConfigAttributes = USKCharacterAttributeSet::GetConfigAttributes();
    TestEqual(TEXT("统一配置集合包含全部28项非资源属性"), ConfigAttributes.Num(), 28);
    for (const FGameplayAttribute& Attribute : ConfigAttributes)
    {
        const FFloatProperty* InitialField = FindFProperty<FFloatProperty>(FSKAttributeInitialization::StaticStruct(), *Attribute.GetName());
        const FFloatProperty* SnapshotField = FindFProperty<FFloatProperty>(FSKAttributeSnapshot::StaticStruct(), *Attribute.GetName());
        TestNotNull(FString::Printf(TEXT("配置字段存在：%s"), *Attribute.GetName()), InitialField);
        TestNotNull(FString::Printf(TEXT("快照字段存在：%s"), *Attribute.GetName()), SnapshotField);
        if (!InitialField || !SnapshotField) continue;
        const float ExpectedValue = InitialField->GetPropertyValue_InContainer(&Values);
        TestEqual(FString::Printf(TEXT("统一属性GE写入：%s"), *Attribute.GetName()), Attribute.GetNumericValue(Fixture.CharacterSet), ExpectedValue);
        TestEqual(FString::Printf(TEXT("统一属性快照读取：%s"), *Attribute.GetName()), SnapshotField->GetPropertyValue_InContainer(&Snapshot), ExpectedValue);
    }
    TestEqual(TEXT("生命资源由同一属性集初始化"), Snapshot.Health, Values.InitialHealth);
    TestEqual(TEXT("躯干资源由同一属性集初始化"), Snapshot.Posture, Values.InitialPosture);
    TestEqual(TEXT("即时效果确实扣血"), Fixture.ASC->ApplyHealthDamage(25.f).ActualAmount, 25.f);
    TestTrue(TEXT("重复初始化幂等"), Fixture.ASC->InitializeFromValues(MakeSKTestAttributes()));
    TestEqual(TEXT("重复初始化不回血"), Fixture.ASC->GetAttributeSnapshot().Health, 75.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSKGASResourceTest,
    "Sekiro.GAS.Numeric.Resources", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 游戏线程验证生命/架势截断、临时属性消费和非法输入；不执行死亡表现。
 * @param Parameters 自动化框架参数，当前测试不使用。
 * @return 流程完成返回 true，断言记录契约失败。
 */
bool FSKGASResourceTest::RunTest(const FString& Parameters)
{
    FSKGASNumericFixture Fixture;
    Fixture.ASC->InitializeFromValues(MakeSKTestAttributes());
    const FSKNumericResult Damage = Fixture.ASC->ApplyHealthDamage(150.f);
    TestEqual(TEXT("请求伤害保留"), Damage.RequestedAmount, 150.f);
    TestEqual(TEXT("实际伤害截断"), Damage.ActualAmount, 100.f);
    TestEqual(TEXT("伤害临时属性清零"), Fixture.CharacterSet->GetIncomingDamage(), 0.f);
    TestEqual(TEXT("治疗截断"), Fixture.ASC->RestoreHealth(150.f).ActualAmount, 100.f);
    TestEqual(TEXT("治疗临时属性清零"), Fixture.CharacterSet->GetIncomingHealing(), 0.f);
    TestEqual(TEXT("架势伤害截断"), Fixture.ASC->ApplyPostureDamage(100.f).ActualAmount, 80.f);
    TestEqual(TEXT("架势恢复"), Fixture.ASC->RestorePosture(20.f).ActualAmount, 20.f);
    TestEqual(TEXT("架势重置"), Fixture.ASC->ResetPosture().ActualAmount, 60.f);
    TestEqual(TEXT("重复重置无变化"), Fixture.ASC->ResetPosture().Code, ESKNumericResultCode::NoChange);
    TestEqual(TEXT("架势伤害临时属性清零"), Fixture.CharacterSet->GetIncomingPostureDamage(), 0.f);
    TestEqual(TEXT("架势恢复临时属性清零"), Fixture.CharacterSet->GetIncomingPostureRecovery(), 0.f);
    TestEqual(TEXT("拒绝负伤害"), Fixture.ASC->ApplyHealthDamage(-1.f).Code, ESKNumericResultCode::InvalidInput);
    TestEqual(TEXT("拒绝零治疗"), Fixture.ASC->RestoreHealth(0.f).Code, ESKNumericResultCode::InvalidInput);
    TestEqual(TEXT("拒绝非有限值"), Fixture.ASC->ApplyHealthDamage(std::numeric_limits<float>::infinity()).Code,
        ESKNumericResultCode::InvalidInput);
    TestEqual(TEXT("拒绝 NaN"), Fixture.ASC->RestorePosture(std::numeric_limits<float>::quiet_NaN()).Code,
        ESKNumericResultCode::InvalidInput);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSKGASMaximumTest,
    "Sekiro.GAS.Numeric.MaximumNoGhostRecovery", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 游戏线程验证持续增益上限变化真实截断 BaseValue，移除后不出现隐性回血。
 * @param Parameters 自动化框架参数，当前测试不使用。
 * @return 流程完成返回 true，断言记录失败。
 */
bool FSKGASMaximumTest::RunTest(const FString& Parameters)
{
    FSKGASNumericFixture Fixture;
    Fixture.ASC->InitializeFromValues(MakeSKTestAttributes());
    const FActiveGameplayEffectHandle Increase = Fixture.AddStatistic(USKCharacterAttributeSet::GetMaxHealthAttribute(), 100.f);
    TestTrue(TEXT("增益进入活动集合"), Increase.IsValid());
    TestEqual(TEXT("加上限不回血"), Fixture.CharacterSet->GetHealth(), 100.f);
    Fixture.ASC->RestoreHealth(100.f);
    const FActiveGameplayEffectHandle Decrease = Fixture.AddStatistic(USKCharacterAttributeSet::GetMaxHealthAttribute(), -150.f);
    TestEqual(TEXT("降低上限夹取当前生命"), Fixture.CharacterSet->GetHealth(), 50.f);
    TestEqual(TEXT("基础生命也真实截断"), Fixture.CharacterSet->Health.GetBaseValue(), 50.f);
    TestTrue(TEXT("按句柄移除负上限"), Fixture.ASC->RemoveAttributeEffect(Decrease));
    TestEqual(TEXT("上限恢复不恢复被截断生命"), Fixture.CharacterSet->GetHealth(), 50.f);
    TestTrue(TEXT("按句柄移除正上限"), Fixture.ASC->RemoveAttributeEffect(Increase));
    TestFalse(TEXT("旧句柄再次移除失败"), Fixture.ASC->RemoveAttributeEffect(Increase));
    const FActiveGameplayEffectHandle NegativeGain = Fixture.AddStatistic(
        USKCharacterAttributeSet::GetPostureGainAttackDeflectedAttribute(), -100.f);
    TestEqual(TEXT("运行时增长属性不为负"), Fixture.CharacterSet->GetPostureGainAttackDeflected(), 0.f);
    Fixture.ASC->RemoveAttributeEffect(NegativeGain);
    TestEqual(TEXT("移除增长效果恢复当前配置"), Fixture.ASC->GetAttributeSnapshot().PostureGainAttackDeflected, 18.f);
    const FActiveGameplayEffectHandle ExcessCap = Fixture.AddStatistic(
        USKCharacterAttributeSet::GetPostureAttackCapRatioAttribute(), 1.f);
    TestTrue(TEXT("运行时非崩溃封顶仍严格小于一"), Fixture.CharacterSet->GetPostureAttackCapRatio() < 1.f);
    Fixture.ASC->RemoveAttributeEffect(ExcessCap);
    const FActiveGameplayEffectHandle NegativeExponent = Fixture.AddStatistic(
        USKCharacterAttributeSet::GetPostureGainFalloffExponentAttribute(), -100.f);
    TestTrue(TEXT("运行时衰减指数仍严格为正"), Fixture.CharacterSet->GetPostureGainFalloffExponent() > 0.f);
    Fixture.ASC->RemoveAttributeEffect(NegativeExponent);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSKGASReentrantTest,
    "Sekiro.GAS.Numeric.ReentrantRequest", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 游戏线程验证属性回调不能递归修改资源，前后快照只统计当前请求。
 * @param Parameters 自动化框架参数，当前测试不使用。
 * @return 流程完成返回 true，断言记录失败。
 */
bool FSKGASReentrantTest::RunTest(const FString& Parameters)
{
    FSKGASNumericFixture Fixture;
    Fixture.ASC->InitializeFromValues(MakeSKTestAttributes());
    ESKNumericResultCode NestedCode = ESKNumericResultCode::Applied;
    const FDelegateHandle Handle = Fixture.ASC->GetGameplayAttributeValueChangeDelegate(
        USKCharacterAttributeSet::GetHealthAttribute()).AddLambda(
        [&Fixture, &NestedCode](const FOnAttributeChangeData& Data)
        {
            NestedCode = Fixture.ASC->ApplyHealthDamage(10.f).Code;
        });
    const FSKNumericResult Result = Fixture.ASC->ApplyHealthDamage(20.f);
    Fixture.ASC->GetGameplayAttributeValueChangeDelegate(USKCharacterAttributeSet::GetHealthAttribute()).Remove(Handle);
    TestEqual(TEXT("重入请求被拒绝"), NestedCode, ESKNumericResultCode::Reentrant);
    TestEqual(TEXT("实际值未混入递归伤害"), Result.ActualAmount, 20.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSKGASArmorTest,
    "Sekiro.GAS.Numeric.ArmorCurve", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 游戏线程验证护甲曲线显式配置及输出范围，不选择项目护甲公式。
 * @param Parameters 自动化框架参数，当前测试不使用。
 * @return 流程完成返回 true，断言记录失败。
 */
bool FSKGASArmorTest::RunTest(const FString& Parameters)
{
    FSKGASNumericFixture Fixture;
    Fixture.ASC->InitializeFromValues(MakeSKTestAttributes());
    float Damage = -1.f;
    TestFalse(TEXT("缺曲线拒绝"), Fixture.ASC->CalculateDamageAfterArmor(100.f, nullptr, Damage));
    TestEqual(TEXT("失败输出零"), Damage, 0.f);
    UCurveFloat* Curve = NewObject<UCurveFloat>(Fixture.World);
    TestFalse(TEXT("空曲线拒绝"), Fixture.ASC->CalculateDamageAfterArmor(100.f, Curve, Damage));
    Curve->FloatCurve.AddKey(0.f, 0.5f);
    TestTrue(TEXT("显式有效曲线"), Fixture.ASC->CalculateDamageAfterArmor(100.f, Curve, Damage));
    TestEqual(TEXT("曲线减伤"), Damage, 50.f);
    Curve->FloatCurve.Reset();
    Curve->FloatCurve.AddKey(0.f, 1.2f);
    TestFalse(TEXT("超过一的倍率拒绝"), Fixture.ASC->CalculateDamageAfterArmor(100.f, Curve, Damage));
    return true;
}

#endif
