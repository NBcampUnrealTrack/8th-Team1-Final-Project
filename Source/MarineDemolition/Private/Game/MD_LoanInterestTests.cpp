#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Game/MD_GameModeBase.h"
#include "Data/Merchant/MD_SiteDefinition.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Gameplay/Merchant/MD_MerchantSubsystem.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "TimerManager.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

// 실제 GameMode 인스턴스의 보호된 예약 경로를 검증하기 위한 멤버 접근 제공
class FMDLoanInterestTimerAccess : public AMD_GameModeBase
{
public:
    using AMD_GameModeBase::CheckGameTimeEvents;
    using AMD_GameModeBase::LoanInterestTimerHandle;
    using AMD_GameModeBase::StartLoanInterestTimer;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDLoanInterestMapEntryTimer, "MarineDemolition.LoanInterest.MapEntryTimer",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMDLoanInterestMapEntryTimer::RunTest(const FString& Parameters)
{
    if (TestNotNull(TEXT("Engine available for timer test"), GEngine) == false)
    {
        return false;
    }

    auto StartTimer = &FMDLoanInterestTimerAccess::StartLoanInterestTimer;
    auto CheckTimeEvents = &FMDLoanInterestTimerAccess::CheckGameTimeEvents;
    auto TimerHandleMember = &FMDLoanInterestTimerAccess::LoanInterestTimerHandle;

    for (double PreviousWorldTime : {0.0, 300.0, 1200.0})
    {
        // 이전 맵에서 이어진 누적 시간을 가진 독립 월드 구성
        UWorld::InitializationValues InitializationValues = UWorld::InitializationValues()
            .AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false)
            .CreateAISystem(false).ShouldSimulatePhysics(false);
        FName WorldName(*FString::Printf(TEXT("LoanInterestTimer_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits)));
        UPackage* WorldPackage = CreatePackage(*FString::Printf(TEXT("/Temp/%s"), *WorldName.ToString()));
        WorldPackage->SetFlags(RF_Transient);
        UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, WorldPackage,
            true, ERHIFeatureLevel::Num, &InitializationValues);
        if (TestNotNull(TEXT("Isolated loan timer world"), World) == false)
        {
            return false;
        }
        // 설정 저장과 온라인 세션 초기화 없이 경제 원장에 필요한 월드 문맥만 연결
        UGameInstance* GameInstance = NewObject<UGameInstance>(GEngine);
        FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
        WorldContext.OwningGameInstance = GameInstance;
        WorldContext.SetCurrentWorld(World);
        World->SetGameInstance(GameInstance);
        GameInstance->OnWorldChanged(nullptr, World);
        UMD_MerchantSubsystem* Merchant = NewObject<UMD_MerchantSubsystem>(GameInstance);
        AMD_GameModeBase* GameMode = nullptr;
        ON_SCOPE_EXIT
        {
            Merchant->OnMerchantStateChanged.RemoveAll(GameMode);
            GameInstance->OnWorldChanged(World, nullptr);
            World->DestroyWorld(false);
            GEngine->DestroyWorldContext(World);
        };
        World->TimeSeconds = PreviousWorldTime;

        // 누적 시간 복사 이후 생성되는 실제 현장 GameMode와 현장 데이터 연결
        GameMode = World->SpawnActor<AMD_GameModeBase>();
        if (TestNotNull(TEXT("New map GameMode"), GameMode) == false)
        {
            return false;
        }
        UMD_SiteDefinition* Site = NewObject<UMD_SiteDefinition>(GameMode);
        Site->QuestMap = TSoftObjectPtr<UWorld>(World);
        FObjectPropertyBase* SiteProperty = FindFProperty<FObjectPropertyBase>(GameMode->GetClass(), TEXT("ExpeditionSite"));
        if (TestNotNull(TEXT("Editable expedition site property"), SiteProperty) == false)
        {
            return false;
        }
        SiteProperty->SetObjectPropertyValue_InContainer(GameMode, Site);
        TestEqual(TEXT("Test site resolves to the current map"), GameMode->FindExpeditionSiteForWorld(World), Site);
        FTimerHandle& TimerHandle = GameMode->*TimerHandleMember;

        // 부채가 없는 계약에서는 이자 예약을 생성하지 않는지 검증
        Merchant->BeginContract(Site, 0);
        TestTrue(TEXT("Debt-free contract is active"), Merchant->HasActiveContract());
        Merchant->OnMerchantStateChanged.AddUObject(GameMode, StartTimer, Merchant);
        (GameMode->*StartTimer)(Merchant);
        TestFalse(TEXT("No debt leaves the interest timer stopped"), World->GetTimerManager().TimerExists(TimerHandle));

        // 이전 맵의 누적 시간과 무관한 첫 징수 유예 및 반복 주기 검증
        World->TimeSeconds += 30.0;
        TestTrue(TEXT("First loan succeeds after map setup"), Merchant->TryBorrowCredits(500));
        TestTrue(TEXT("New debt receives the full nine-minute grace period"),
            FMath::IsNearlyEqual(World->GetTimerManager().GetTimerRemaining(TimerHandle), 540.0f));
        TestEqual(TEXT("Interest repeats every three minutes"),
            World->GetTimerManager().GetTimerRate(TimerHandle), 180.0f);

        // 재호출로 기존 예약이 연장되거나 앞당겨지지 않는지 검증
        (GameMode->*StartTimer)(Merchant);
        TestTrue(TEXT("Repeated initialization preserves the existing reservation"),
            FMath::IsNearlyEqual(World->GetTimerManager().GetTimerRemaining(TimerHandle), 540.0f));

        // 일부 대기 시간이 지난 예약을 부분 상환과 추가 대출 및 이자 갱신이 유지하는지 검증
        World->GetTimerManager().SetTimer(TimerHandle, GameMode, CheckTimeEvents, 180.0f, true, 120.0f);
        TestTrue(TEXT("Partial repayment succeeds"), Merchant->TryRepayLoan(100));
        TestTrue(TEXT("Partial repayment preserves the current countdown"),
            FMath::IsNearlyEqual(World->GetTimerManager().GetTimerRemaining(TimerHandle), 120.0f));
        TestTrue(TEXT("Additional loan succeeds"), Merchant->TryBorrowCredits(50));
        TestTrue(TEXT("Additional debt preserves the current countdown"),
            FMath::IsNearlyEqual(World->GetTimerManager().GetTimerRemaining(TimerHandle), 120.0f));
        Merchant->AddCredits(EMD_DailyReportCategory::Other, 1000);
        TestTrue(TEXT("Unrelated credit updates preserve the current countdown"),
            FMath::IsNearlyEqual(World->GetTimerManager().GetTimerRemaining(TimerHandle), 120.0f));
        TestTrue(TEXT("Existing debt accrues interest"), Merchant->ApplyDailyLoanInterest() > 0);
        TestTrue(TEXT("Interest updates preserve the current countdown"),
            FMath::IsNearlyEqual(World->GetTimerManager().GetTimerRemaining(TimerHandle), 120.0f));

        // 완납 시 예약 해제와 이후 재대출 시 전체 초기 대기 재시작 검증
        TestTrue(TEXT("Full repayment succeeds"), Merchant->TryRepayLoan(Merchant->GetDebtBalance()));
        TestFalse(TEXT("Full repayment removes the interest timer"), World->GetTimerManager().TimerExists(TimerHandle));
        World->TimeSeconds += 1200.0;
        TestTrue(TEXT("New loan succeeds after a debt-free interval"), Merchant->TryBorrowCredits(100));
        TestTrue(TEXT("Debt-free time does not reduce the new grace period"),
            FMath::IsNearlyEqual(World->GetTimerManager().GetTimerRemaining(TimerHandle), 540.0f));

        // 계약이 끝난 이후에는 부채가 남아 있어도 예약이 유지되지 않는지 검증
        Merchant->FinalizeContract();
        TestFalse(TEXT("Contract end removes the interest timer"), World->GetTimerManager().TimerExists(TimerHandle));
    }

    return true;
}

#endif
