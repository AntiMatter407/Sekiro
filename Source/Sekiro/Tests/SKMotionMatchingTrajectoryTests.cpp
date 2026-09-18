#include "Character/SKCharacter.h"
#include "Input/SKInputManager.h"
#include "Movement/SKMotionMatchingTrajectoryComponent.h"
#include "Movement/SKMovementComponent.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
    /** 只在显式运行测试时创建隔离角色世界；不载入关卡、不启动 PIE。 */
    struct FSKMotionMatchingTrajectoryFixture
    {
        UWorld* World = nullptr; // 测试专用世界
        ASKCharacter* Character = nullptr; // 提供项目输入与移动组件的测试角色
        USKInputManager* InputManager = nullptr; // MoveIntent 测试写入入口
        USKMovementComponent* MovementComponent = nullptr; // 当前速度与移动档位来源
        USKMotionMatchingTrajectoryComponent* TrajectoryComponent = nullptr; // 被测轨迹组件

        FSKMotionMatchingTrajectoryFixture();
        ~FSKMotionMatchingTrajectoryFixture();
        bool IsReady() const;
        FTransformTrajectory Predict(float PredictionSeconds) const;
    };

    /**
     * 游戏线程创建不进入 BeginPlay 的独立原生角色，并注册被测轨迹组件。
     * 夹具关闭 Lua Movement Tick，仅通过公开快照入口布置测试数据。
     */
    FSKMotionMatchingTrajectoryFixture::FSKMotionMatchingTrajectoryFixture()
    {
        World = UWorld::CreateWorld(EWorldType::Game, false);
        if (!World || !GEngine) return;

        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        Character = World->SpawnActor<ASKCharacter>();
        if (!Character) return;

        InputManager = Character->GetInputManager();
        MovementComponent = Cast<USKMovementComponent>(Character->GetMovementComponent());
        if (!InputManager || !MovementComponent) return;

        MovementComponent->SetUseLuaMovementLogic(false);
        MovementComponent->RefreshCachedMovementComponents();
        MovementComponent->CurrentMovementTier = ESKMovementTier::Run;

        TrajectoryComponent = NewObject<USKMotionMatchingTrajectoryComponent>(Character);
        Character->AddInstanceComponent(TrajectoryComponent);
        TrajectoryComponent->RegisterComponent();
        TrajectoryComponent->TickComponent(1.f / 30.f, LEVELTICK_All, nullptr);
    }

    /** 游戏线程销毁测试世界及对应上下文，不影响编辑器或其他测试世界。 */
    FSKMotionMatchingTrajectoryFixture::~FSKMotionMatchingTrajectoryFixture()
    {
        if (!World) return;
        if (GEngine) GEngine->DestroyWorldContext(World);
        World->DestroyWorld(false);
    }

    /** 返回夹具所有被测依赖是否创建成功；只读，可在游戏线程调用。 */
    bool FSKMotionMatchingTrajectoryFixture::IsReady() const
    {
        return World && Character && InputManager && MovementComponent && TrajectoryComponent;
    }

    /**
     * 游戏线程按指定未来时域读取不含历史的角色局部轨迹。
     * @param PredictionSeconds 预测时长，单位秒；被测组件负责处理非法值。
     * @return 包含唯一零时刻和未来样本的轨迹副本。
     */
    FTransformTrajectory FSKMotionMatchingTrajectoryFixture::Predict(float PredictionSeconds) const
    {
        FMotionTrajectorySettings Settings;
        Settings.Seconds = PredictionSeconds;
        return TrajectoryComponent
            ? TrajectoryComponent->GetTrajectoryWithSettings(Settings, false)
            : FTransformTrajectory();
    }

    /** 检查向量三个分量是否均为有限值；只读取值类型，可在任意线程调用。 */
    bool IsSKFiniteVectorForTest(const FVector& Value)
    {
        return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y) && FMath::IsFinite(Value.Z);
    }

    /** 检查测试样本的时间、位置和 Facing 是否全部为有限值。 */
    bool IsSKFiniteTrajectorySampleForTest(const FTransformTrajectorySample& Sample)
    {
        return FMath::IsFinite(Sample.TimeInSeconds)
            && IsSKFiniteVectorForTest(Sample.Position)
            && FMath::IsFinite(Sample.Facing.X)
            && FMath::IsFinite(Sample.Facing.Y)
            && FMath::IsFinite(Sample.Facing.Z)
            && FMath::IsFinite(Sample.Facing.W);
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSKMotionMatchingTrajectoryFiniteValuesTest,
    "Sekiro.MotionMatching.Trajectory.FiniteValues",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 游戏线程验证非有限配置和 MoveIntent 不会把 NaN 或无穷值传播到轨迹样本。
 * @param Parameters 自动化框架参数，当前测试不使用。
 * @return 测试流程完成返回 true，失败由 Test 系列断言记录。
 */
bool FSKMotionMatchingTrajectoryFiniteValuesTest::RunTest(const FString& Parameters)
{
    static_cast<void>(Parameters);
    FSKMotionMatchingTrajectoryFixture Fixture;
    if (!TestTrue(TEXT("轨迹测试夹具创建成功"), Fixture.IsReady())) return false;

    FSKTrajectoryPredictionSettings PredictionSettings;
    PredictionSettings.Acceleration = std::numeric_limits<float>::infinity();
    PredictionSettings.Deceleration = std::numeric_limits<float>::quiet_NaN();
    PredictionSettings.FacingTurnRateDegrees = -std::numeric_limits<float>::infinity();
    PredictionSettings.InputThreshold = std::numeric_limits<float>::quiet_NaN();
    Fixture.TrajectoryComponent->SetTrajectoryPredictionSettings(PredictionSettings);
    Fixture.InputManager->SetMoveIntentForScript(
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(),
        1.f,
        0.f);

    const FTransformTrajectory Trajectory = Fixture.Predict(
        std::numeric_limits<float>::quiet_NaN());
    TestTrue(TEXT("非法预测参数仍生成当前样本"), Trajectory.Samples.Num() > 0);
    for (const FTransformTrajectorySample& Sample : Trajectory.Samples)
    {
        TestTrue(TEXT("轨迹样本所有字段均为有限值"), IsSKFiniteTrajectorySampleForTest(Sample));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSKMotionMatchingTrajectorySamplingOrderTest,
    "Sekiro.MotionMatching.Trajectory.SamplingOrder",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 游戏线程验证无历史查询包含唯一零时刻，且所有未来采样时间严格递增。
 * @param Parameters 自动化框架参数，当前测试不使用。
 * @return 测试流程完成返回 true，失败由 Test 系列断言记录。
 */
bool FSKMotionMatchingTrajectorySamplingOrderTest::RunTest(const FString& Parameters)
{
    static_cast<void>(Parameters);
    FSKMotionMatchingTrajectoryFixture Fixture;
    if (!TestTrue(TEXT("轨迹测试夹具创建成功"), Fixture.IsReady())) return false;

    Fixture.InputManager->SetMoveIntentForScript(0.f, 1.f, 1.f, 0.f);
    const FTransformTrajectory Trajectory = Fixture.Predict(0.5f);
    TestTrue(TEXT("轨迹包含当前与未来样本"), Trajectory.Samples.Num() > 1);

    int32 ZeroSampleCount = 0;
    float PreviousSeconds = -TNumericLimits<float>::Max();
    for (const FTransformTrajectorySample& Sample : Trajectory.Samples)
    {
        if (FMath::IsNearlyZero(Sample.TimeInSeconds)) ++ZeroSampleCount;
        TestTrue(TEXT("采样时间严格递增"), Sample.TimeInSeconds > PreviousSeconds);
        PreviousSeconds = Sample.TimeInSeconds;
    }
    TestEqual(TEXT("轨迹只包含一个零时刻样本"), ZeroSampleCount, 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSKMotionMatchingTrajectoryZeroInputTest,
    "Sekiro.MotionMatching.Trajectory.ZeroInput",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 游戏线程验证角色静止且 MoveIntent 为零时，未来轨迹位置保持原点。
 * @param Parameters 自动化框架参数，当前测试不使用。
 * @return 测试流程完成返回 true，失败由 Test 系列断言记录。
 */
bool FSKMotionMatchingTrajectoryZeroInputTest::RunTest(const FString& Parameters)
{
    static_cast<void>(Parameters);
    FSKMotionMatchingTrajectoryFixture Fixture;
    if (!TestTrue(TEXT("轨迹测试夹具创建成功"), Fixture.IsReady())) return false;

    Fixture.MovementComponent->Velocity = FVector::ZeroVector;
    Fixture.InputManager->SetMoveIntentForScript(0.f, 0.f, 0.f, 0.f);
    Fixture.TrajectoryComponent->TickComponent(1.f / 30.f, LEVELTICK_All, nullptr);
    const FTransformTrajectory Trajectory = Fixture.Predict(0.5f);
    TestTrue(TEXT("零输入轨迹包含未来样本"), Trajectory.Samples.Num() > 1);

    for (const FTransformTrajectorySample& Sample : Trajectory.Samples)
    {
        TestTrue(TEXT("零输入轨迹位置保持原点"), Sample.Position.IsNearlyZero());
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSKMotionMatchingTrajectorySuddenReverseTest,
    "Sekiro.MotionMatching.Trajectory.SuddenReverse",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 游戏线程验证角色仍以正 X 前进时突然输入后退，预测速度先减小并在时域内反向。
 * @param Parameters 自动化框架参数，当前测试不使用。
 * @return 测试流程完成返回 true，失败由 Test 系列断言记录。
 */
bool FSKMotionMatchingTrajectorySuddenReverseTest::RunTest(const FString& Parameters)
{
    static_cast<void>(Parameters);
    FSKMotionMatchingTrajectoryFixture Fixture;
    if (!TestTrue(TEXT("轨迹测试夹具创建成功"), Fixture.IsReady())) return false;

    constexpr double InitialForwardSpeed = 300.0;
    Fixture.MovementComponent->Velocity = FVector(InitialForwardSpeed, 0.f, 0.f);
    Fixture.InputManager->SetMoveIntentForScript(0.f, -1.f, 1.f, 0.f);
    Fixture.TrajectoryComponent->TickComponent(1.f / 30.f, LEVELTICK_All, nullptr);
    const FTransformTrajectory Trajectory = Fixture.Predict(1.f);
    if (!TestTrue(TEXT("反向轨迹包含未来样本"), Trajectory.Samples.Num() > 1)) return false;

    const FTransformTrajectorySample& PresentSample = Trajectory.Samples[0];
    const FTransformTrajectorySample& FirstFutureSample = Trajectory.Samples[1];
    const FTransformTrajectorySample& LastFutureSample = Trajectory.Samples.Last();
    TestTrue(TEXT("当前样本保持原点"), PresentSample.Position.IsNearlyZero());
    TestTrue(TEXT("首个未来样本仍沿原前进方向位移"), FirstFutureSample.Position.Y < 0.f);
    TestTrue(TEXT("预测时域内位移完成反向"), LastFutureSample.Position.Y > 0.f);
    TestTrue(
        TEXT("后退输入不会产生可观测横向位移"),
        FMath::IsNearlyZero(LastFutureSample.Position.X, 0.01f));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSKMotionMatchingTrajectoryStartFromRestTest,
    "Sekiro.MotionMatching.Trajectory.StartFromRest",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 游戏线程验证静止角色收到前进意图后，Desired Trajectory 在查询空间中从零开始向前增长。
 * @param Parameters 自动化框架参数，当前测试不使用。
 * @return 测试流程完成返回 true，失败由 Test 系列断言记录。
 */
bool FSKMotionMatchingTrajectoryStartFromRestTest::RunTest(const FString& Parameters)
{
    static_cast<void>(Parameters);
    FSKMotionMatchingTrajectoryFixture Fixture;
    if (!TestTrue(TEXT("轨迹测试夹具创建成功"), Fixture.IsReady())) return false;

    Fixture.MovementComponent->Velocity = FVector::ZeroVector;
    Fixture.InputManager->SetMoveIntentForScript(0.f, 1.f, 1.f, 0.f);
    Fixture.TrajectoryComponent->TickComponent(1.f / 30.f, LEVELTICK_All, nullptr);
    const FTransformTrajectory Trajectory = Fixture.Predict(0.5f);
    if (!TestTrue(TEXT("起步轨迹包含当前与未来样本"), Trajectory.Samples.Num() > 2)) return false;

    TestTrue(TEXT("当前样本保持原点"), Trajectory.Samples[0].Position.IsNearlyZero());
    TestTrue(TEXT("首个未来样本沿动画查询前向移动"), Trajectory.Samples[1].Position.Y < 0.f);
    TestTrue(
        TEXT("未来位移随起步预测继续增长"),
        Trajectory.Samples.Last().Position.Y < Trajectory.Samples[1].Position.Y);
    TestTrue(
        TEXT("前进起步不产生可观测横向位移"),
        FMath::IsNearlyZero(Trajectory.Samples.Last().Position.X, 0.01f));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSKMotionMatchingTrajectoryReleaseToStopTest,
    "Sekiro.MotionMatching.Trajectory.ReleaseToStop",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 游戏线程验证角色仍有前进实际速度时释放输入，Desired Trajectory 会继续制动并在预测时域内停止。
 * @param Parameters 自动化框架参数，当前测试不使用。
 * @return 测试流程完成返回 true，失败由 Test 系列断言记录。
 */
bool FSKMotionMatchingTrajectoryReleaseToStopTest::RunTest(const FString& Parameters)
{
    static_cast<void>(Parameters);
    FSKMotionMatchingTrajectoryFixture Fixture;
    if (!TestTrue(TEXT("轨迹测试夹具创建成功"), Fixture.IsReady())) return false;

    Fixture.MovementComponent->Velocity = FVector(300.f, 0.f, 0.f);
    Fixture.InputManager->SetMoveIntentForScript(0.f, 0.f, 0.f, 0.f);
    Fixture.TrajectoryComponent->TickComponent(1.f / 30.f, LEVELTICK_All, nullptr);
    const FTransformTrajectory Trajectory = Fixture.Predict(0.5f);
    if (!TestTrue(TEXT("停止轨迹包含足够未来样本"), Trajectory.Samples.Num() > 3)) return false;

    const FVector& FirstFuturePosition = Trajectory.Samples[1].Position;
    const FVector& PenultimatePosition = Trajectory.Samples[Trajectory.Samples.Num() - 2].Position;
    const FVector& LastPosition = Trajectory.Samples.Last().Position;
    TestTrue(TEXT("释放后保留短暂向前制动距离"), FirstFuturePosition.Y < 0.f);
    TestTrue(TEXT("停止位置晚于首个预测点"), LastPosition.Y < FirstFuturePosition.Y);
    TestTrue(
        TEXT("预测时域末端已经停止"),
        LastPosition.Equals(PenultimatePosition, 0.01f));
    TestTrue(
        TEXT("释放停止不产生可观测横向位移"),
        FMath::IsNearlyZero(LastPosition.X, 0.01f));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSKMotionMatchingTrajectoryGaitProfileTest,
    "Sekiro.MotionMatching.Trajectory.GaitProfile",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 游戏线程验证未来查询速度只由独立 Gait Profile 配置决定，不需要本帧 Motion Matching 选择结果。
 * @param Parameters 自动化框架参数，当前测试不使用。
 * @return 测试流程完成返回 true，失败由 Test 系列断言记录。
 */
bool FSKMotionMatchingTrajectoryGaitProfileTest::RunTest(const FString& Parameters)
{
    static_cast<void>(Parameters);
    FSKMotionMatchingTrajectoryFixture Fixture;
    if (!TestTrue(TEXT("轨迹测试夹具创建成功"), Fixture.IsReady())) return false;

    Fixture.MovementComponent->Velocity = FVector::ZeroVector;
    Fixture.InputManager->SetMoveIntentForScript(0.f, 1.f, 1.f, 0.f);
    Fixture.TrajectoryComponent->TickComponent(1.f / 30.f, LEVELTICK_All, nullptr);

    FSKAnimationRootMotionSpeedProfile SlowProfile;
    SlowProfile.Run = 200.f;
    Fixture.TrajectoryComponent->SetAnimationRootMotionSpeedProfile(SlowProfile);
    const FTransformTrajectory SlowTrajectory = Fixture.Predict(0.5f);

    FSKAnimationRootMotionSpeedProfile FastProfile;
    FastProfile.Run = 400.f;
    Fixture.TrajectoryComponent->SetAnimationRootMotionSpeedProfile(FastProfile);
    const FTransformTrajectory FastTrajectory = Fixture.Predict(0.5f);
    if (!TestTrue(
        TEXT("两组 Gait Profile 都生成未来样本"),
        SlowTrajectory.Samples.Num() > 1 && FastTrajectory.Samples.Num() > 1)) return false;

    TestTrue(
        TEXT("更高的独立 Run Profile 产生更远的意图位移"),
        FMath::Abs(FastTrajectory.Samples.Last().Position.Y)
            > FMath::Abs(SlowTrajectory.Samples.Last().Position.Y));
    return true;
}

#endif
