#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Component/Oxygen/MD_OxygenComponent.h"
#include "../Facility/MD_FacilityPlayTestGameMode.h"
#include "AbilitySystemComponent.h"
#include "Character/MD_Character.h"
#include "Components/AudioComponent.h"
#include "Components/HorizontalBox.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Gameplay/Attribute/MD_CharacterAttributeSet.h"
#include "GameplayTags/MD_GameplayTags.h"
#include "Misc/AutomationTest.h"
#include "PlayInEditorDataTypes.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectIterator.h"

class FMDOxygenWarningPIE final : public IAutomationLatentCommand
{
public:
	FMDOxygenWarningPIE(FAutomationTestBase* InTest, bool bInSolo) : Test(InTest), bSolo(bInSolo)
	{
		Settings.Reset(DuplicateObject<ULevelEditorPlaySettings>(GetDefault<ULevelEditorPlaySettings>(), GetTransientPackage()));
		Settings->SetPlayNetMode(PIE_ListenServer); Settings->SetRunUnderOneProcess(true); Settings->SetPlayNumberOfClients(bSolo ? 1 : 2);
		FRequestPlaySessionParams P; P.EditorPlaySettings = Settings.Get(); P.GlobalMapOverride = TEXT("/Engine/Maps/Entry");
		P.GameModeOverride = AMD_FacilityPlayTestGameMode::StaticClass(); P.bAllowOnlineSubsystem = false;
		Start = FPlatformTime::Seconds(); GEditor->RequestPlaySession(P);
	}
	~FMDOxygenWarningPIE() override { if (GEditor && GEditor->PlayWorld) GEditor->RequestEndPlayMap(); }
	bool Update() override
	{
		if (FPlatformTime::Seconds() - Start > 60.) { Test->AddError(TEXT("Oxygen warning PIE timeout")); return true; }
		if (!World)
		{
			for (const auto& C : GEngine->GetWorldContexts()) if (C.WorldType == EWorldType::PIE && C.World() && C.World()->HasBegunPlay())
				if (C.World()->GetNetMode() == NM_Client) Client = C.World(); else World = C.World();
			if (!World || (!bSolo && !Client)) { World = nullptr; return false; }
			for (UWorld* W : {World, Client}) if (W)
			{
				bool bLocalReady = false;
				for (TActorIterator<AMD_Character> It(W); It; ++It) if (auto* O = It->FindComponentByClass<UMD_OxygenComponent>())
				{
					if (!O->GetAttributeSet() || !O->GetAbilitySystemComponent()) { World = nullptr; return false; }
					bLocalReady |= It->IsLocallyControlled();
					W->GetTimerManager().ClearTimer(O->EvaluationTimerHandle);
					It->GetCharacterMovement()->DisableMovement();
					O->BreathState = EMD_BreathState::Submerged;
					// 경고 경계 검증 중 수면 회복 GE가 산소를 다시 채우지 않도록 고정한다.
					if (It->HasAuthority())
					{
						auto* ASC = O->GetAbilitySystemComponent();
						for (const auto& Effect : {O->OxygenDrainEffectClass, O->OxygenRecoveryEffectClass, O->OxygenDepletedDamageEffectClass})
							if (Effect) ASC->RemoveActiveGameplayEffectBySourceEffect(Effect, ASC);
					}
				}
				if (!bLocalReady) { World = nullptr; return false; }
			}
			SetRatio(.3751f); Wait(.6f); return false;
		}
		if (World->GetTimeSeconds() < At) return false;
		// 클라이언트의 실제 산소 복제가 도착한 뒤 경계와 재생 상태를 검사한다.
		bool bOxygenReplicated = true;
		ForOxygen([&](UMD_OxygenComponent* O)
		{
			const auto* Pawn = Cast<APawn>(O->GetOwner());
			if (Pawn && Pawn->IsLocallyControlled())
				bOxygenReplicated &= FMath::IsNearlyEqual(O->GetAttributeSet()->GetOxygen() / O->GetAttributeSet()->GetMaxOxygen(), TargetRatio, .000001f);
		});
		if (!bOxygenReplicated) return false;
		switch (Phase++)
		{
		case 0:
			Refresh(); CheckLow(0); CheckHeartbeat(0); SetRatio(.375f); break;
		case 1:
			Refresh(); CheckLow(1); CheckHeartbeat(1);
			for (int32 I = 0; I < 5; ++I) Refresh();
			CheckLow(1); CheckHeartbeat(1); Wait(4.2f); return false;
		case 2:
			Refresh(); CheckLow(1); CheckHeartbeat(1); SetRatio(.3751f); break;
		case 3:
			Refresh(); break;
		case 4:
			CheckLow(0); CheckHeartbeat(0); SetRatio(.375f); break;
		case 5:
			Refresh(); CheckLow(1); SetBreath(EMD_BreathState::Surface); Refresh(); break;
		case 6:
			CheckLow(0); CheckHeartbeat(0); SetBreath(EMD_BreathState::Submerged); Refresh(); CheckLow(1); break;
		case 7:
			ForOxygen([](UMD_OxygenComponent* O) { O->GetAbilitySystemComponent()->AddLooseGameplayTag(MDGameplayTags::State_Dead); });
			Refresh(); break;
		case 8:
			CheckLow(0); CheckHeartbeat(0); GEditor->RequestEndPlayMap(); return true;
		}
		Wait(.6f); return false;
	}
private:
	template<typename F> void ForOxygen(F Fn)
	{
		for (UWorld* W : {World, Client}) if (W)
			for (TActorIterator<AMD_Character> It(W); It; ++It) if (auto* O = It->FindComponentByClass<UMD_OxygenComponent>()) Fn(O);
	}
	void SetRatio(float Ratio)
	{
		TargetRatio = Ratio;
		for (TActorIterator<AMD_Character> It(World); It; ++It) if (auto* O = It->FindComponentByClass<UMD_OxygenComponent>())
			O->GetAbilitySystemComponent()->SetNumericAttributeBase(UMD_CharacterAttributeSet::GetOxygenAttribute(), O->GetAttributeSet()->GetMaxOxygen() * Ratio);
	}
	void SetBreath(EMD_BreathState State) { ForOxygen([State](UMD_OxygenComponent* O) { O->BreathState = State; }); }
	void Refresh() { ForOxygen([](UMD_OxygenComponent* O) { O->RefreshSurvivalAudio(); }); }
	void Wait(float Seconds) { At = World->GetTimeSeconds() + Seconds; }
	int32 CountSound(UWorld* W, const TCHAR* Name)
	{
		int32 Count = 0;
		for (TObjectIterator<UAudioComponent> It; It; ++It)
			if (It->GetWorld() == W && It->Sound && It->Sound->GetFName() == FName(Name) && It->IsPlaying()) ++Count;
		return Count;
	}
	void CheckLow(int32 Expected)
	{
		for (UWorld* W : {World, Client}) if (W)
		{
			for (TActorIterator<AMD_Character> It(W); It; ++It) if (It->IsLocallyControlled())
			{
				auto* O = It->FindComponentByClass<UMD_OxygenComponent>();
				Test->AddInfo(FString::Printf(TEXT("Phase %d peer %d oxygen %.5f/%.5f low %d"), Phase, int32(W->GetNetMode()), O->GetAttributeSet()->GetOxygen(), O->GetAttributeSet()->GetMaxOxygen(), O->bAudioOxygenLow));
			}
			Test->TestEqual(TEXT("Low warning plays once per local player at three of eight cells, repeats after four seconds, and stops above threshold"), CountSound(W, TEXT("S_V2_Character_Oxygen_Low")), Expected);
			for (TActorIterator<AMD_Character> It(W); It; ++It) if (!It->IsLocallyControlled())
				Test->TestFalse(TEXT("Remote pawn never starts a private low oxygen warning"), It->FindComponentByClass<UMD_OxygenComponent>()->bAudioOxygenLow);
		}
	}
	void CheckHeartbeat(int32 Expected)
	{
		for (UWorld* W : {World, Client}) if (W)
			Test->TestEqual(TEXT("Heartbeat has one loop per local player and stops on recovery, surfacing or death"), CountSound(W, TEXT("S_E_Player_HeartbeatLoop_01")), Expected);
	}
	FAutomationTestBase* Test; bool bSolo; TStrongObjectPtr<ULevelEditorPlaySettings> Settings;
	double Start = 0.; float At = 0.f; float TargetRatio = 1.f; int32 Phase = 0; UWorld* World = nullptr; UWorld* Client = nullptr;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDOxygenWarningHUD, "MarineDemolition.OxygenWarning.HUD", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMDOxygenWarningHUD::RunTest(const FString&)
{
	auto* Container = LoadObject<UHorizontalBox>(nullptr, TEXT("/Game/UI/HUD/PlayerStatus/WBP_MD_CommonPlayerStatus.WBP_MD_CommonPlayerStatus:WidgetTree.HB_OxygenContainer"));
	if (TestNotNull(TEXT("Live HUD oxygen container exists"), Container)) TestEqual(TEXT("Warning threshold matches the eight-cell HUD"), Container->GetChildrenCount(), 8);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDOxygenWarningSolo, "MarineDemolition.OxygenWarning.SoloPIE", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMDOxygenWarningSolo::RunTest(const FString&)
{
	AddExpectedError(TEXT("Online Subsystem의 External UIExternal UI 모듈이 유효하지 않습니다"), EAutomationExpectedErrorFlags::Contains, 1);
	ADD_LATENT_AUTOMATION_COMMAND(FMDOxygenWarningPIE(this, true)); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDOxygenWarningPeers, "MarineDemolition.OxygenWarning.TwoPlayerPIE", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMDOxygenWarningPeers::RunTest(const FString&)
{
	AddExpectedError(TEXT("Online Subsystem의 External UIExternal UI 모듈이 유효하지 않습니다"), EAutomationExpectedErrorFlags::Contains, 2);
	ADD_LATENT_AUTOMATION_COMMAND(FMDOxygenWarningPIE(this, false)); return true;
}
#endif
