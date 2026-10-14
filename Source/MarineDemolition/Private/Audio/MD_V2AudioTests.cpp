#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Audio/MD_AudioSubsystem.h"
#include "Data/Audio/MD_AudioCategoryData.h"
#include "Component/Facility/MD_FacilityRepairComponent.h"
#include "../Component/Facility/MD_FacilityPlayTestGameMode.h"
#include "World/Trash/MD_TrashSorter.h"
#include "World/Wall/MD_CoolingPump.h"
#include "Character/MD_Character.h"
#include "Components/AudioComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Editor.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"
#include "PlayInEditorDataTypes.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Sound/SoundBase.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectIterator.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV2AudioCatalog, "MarineDemolition.V2Audio.Catalog", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FV2AudioCatalog::RunTest(const FString&)
{
	auto* Library = LoadObject<UMD_AudioLibrary>(nullptr, TEXT("/Game/SFX/Data/DA_MD_AudioLibrary.DA_MD_AudioLibrary"));
	if (!TestNotNull(TEXT("Configured sound library exists"), Library)) return false;
	TArray<FMD_AudioEntry> Entries; Library->CollectEntries(Entries);
	int32 NewSounds = 0; TSet<FGameplayTag> V2Tags;
	for (const auto& Entry : Entries)
	{
		USoundBase* Sound = Entry.Sound.LoadSynchronous();
		TestNotNull(TEXT("Every library entry resolves a sound"), Sound);
		if (!Sound || !Sound->GetPathName().StartsWith(TEXT("/Game/SFX/V2/"))) continue;
		++NewSounds; TestTrue(TEXT("V2 sound tag registered"), Entry.EventTag.IsValid());
		TestFalse(TEXT("No ambiguous duplicate V2 tag"), V2Tags.Contains(Entry.EventTag)); V2Tags.Add(Entry.EventTag);
		TestTrue(TEXT("V2 sound limits overlapping voices"), !Entry.Concurrency.IsNull());
		if (Entry.Space == EMD_AudioSpace::UI)
		{
			TestTrue(TEXT("Private/UI feedback uses 2D"), Entry.b2D);
			TestTrue(TEXT("Private/UI feedback has no distance attenuation"), Entry.Attenuation.IsNull());
		}
		else TestTrue(TEXT("World sound has distance attenuation"), !Entry.Attenuation.IsNull());
	}
	TestEqual(TEXT("New V2 feedback and approved hit sound registered alongside preserved legacy sounds"), NewSounds, 50);
	return true;
}

namespace
{
int32 CountLoop(UWorld* World, const TCHAR* Name)
{
	int32 Count = 0;
	for (TObjectIterator<UAudioComponent> It; It; ++It)
		if (It->GetWorld() == World && It->Sound && It->Sound->GetFName() == FName(Name) && It->IsPlaying()) ++Count;
	return Count;
}
class FV2AudioPIE final : public IAutomationLatentCommand
{
public:
	FV2AudioPIE(FAutomationTestBase* InTest, bool Solo) : Test(InTest), bSolo(Solo)
	{
		Settings.Reset(DuplicateObject<ULevelEditorPlaySettings>(GetDefault<ULevelEditorPlaySettings>(), GetTransientPackage()));
		Settings->SetPlayNetMode(PIE_ListenServer); Settings->SetRunUnderOneProcess(true); Settings->SetPlayNumberOfClients(Solo ? 1 : 2);
		FRequestPlaySessionParams P; P.EditorPlaySettings = Settings.Get(); P.GlobalMapOverride = TEXT("/Engine/Maps/Entry");
		P.GameModeOverride = AMD_FacilityPlayTestGameMode::StaticClass(); P.bAllowOnlineSubsystem = false;
		Start = FPlatformTime::Seconds(); GEditor->RequestPlaySession(P);
	}
	~FV2AudioPIE() override { if (GEditor && GEditor->PlayWorld) GEditor->RequestEndPlayMap(); }
	bool Update() override
	{
		if (FPlatformTime::Seconds() - Start > 90.) { Test->AddError(TEXT("V2 audio PIE timed out")); return true; }
		if (!World)
		{
			for (const auto& C : GEngine->GetWorldContexts()) if (C.WorldType == EWorldType::PIE && C.World() && C.World()->HasBegunPlay())
				if (C.World()->GetNetMode() == NM_Client) Client = C.World(); else World = C.World();
			if (!World || (!bSolo && !Client)) { World = nullptr; return false; }
			for (TActorIterator<APlayerController> It(World); It; ++It)
				if (auto* Pawn = Cast<AMD_Character>(It->GetPawn())) { Pawn->GetCharacterMovement()->DisableMovement(); if (It->IsLocalController()) Host = Pawn; }
			if (!Host) { World = nullptr; return false; }
			FActorSpawnParameters P; P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Sorter = World->SpawnActor<AMD_TrashSorter>(FVector(2000, 0, 0), FRotator::ZeroRotator, P);
			if (!Test->TestNotNull(TEXT("Real sorter fixture"), Sorter)) return true;
			Sorter->FacilityRepair->bRandomFaultEligible = false;
			Test->TestTrue(TEXT("Start real replicated facility incident"), Sorter->FacilityRepair->TryBreak());
			Wait(); return false;
		}
		if (World->GetTimeSeconds() - At < .6) return false;
		switch (Phase++)
		{
		case 0:
			Check(0, 1); FaultId = Sorter->FacilityRepair->GetSnapshot().FaultId;
			Test->TestTrue(TEXT("Begin manual repair"), Sorter->FacilityRepair->BeginRepair(Host, FaultId)); break;
		case 1:
			Check(1, 1); Test->TestTrue(TEXT("Progress updates are accepted"), Sorter->FacilityRepair->AddRepairProgress(Host, FaultId, .2f)); break;
		case 2:
			Check(1, 1); Test->TestTrue(TEXT("Cancel repair"), Sorter->FacilityRepair->CancelRepair(Host, FaultId)); break;
		case 3:
			Check(0, 1); Test->TestTrue(TEXT("Resume repair"), Sorter->FacilityRepair->BeginRepair(Host, FaultId)); break;
		case 4:
			Check(1, 1); Test->TestTrue(TEXT("Finish repair"), Sorter->FacilityRepair->AddRepairProgress(Host, FaultId, 1.f)); break;
		case 5:
			Check(0, 0); Test->TestTrue(TEXT("Second incident"), Sorter->FacilityRepair->TryBreak()); break;
		case 6:
			Check(0, 1); Sorter->Destroy(); break;
		case 7:
		{
			Check(0, 0);
			FActorSpawnParameters P; P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Pump = World->SpawnActor<AMD_CoolingPump>(FVector(2000, 200, 0), FRotator::ZeroRotator, P);
			if (!Test->TestNotNull(TEXT("Real cooling pump fixture"), Pump)) return true;
			Pump->FacilityRepair->bRandomFaultEligible = false;
			Test->TestTrue(TEXT("Pump incident starts"), Pump->FacilityRepair->TryBreak()); break;
		}
		case 8:
			CheckPump(1); Test->TestTrue(TEXT("Pump repair accepted"), Pump->FacilityRepair->CompleteRepairFromValidatedSource(Pump->FacilityRepair->GetSnapshot().FaultId)); break;
		case 9:
			CheckPump(0); Test->TestTrue(TEXT("Pump second incident starts"), Pump->FacilityRepair->TryBreak()); break;
		case 10:
			CheckPump(1); Pump->Destroy(); break;
		case 11:
			CheckPump(0); GEditor->RequestEndPlayMap(); return true;
		}
		Wait(); return false;
	}
private:
	void CheckPump(int32 Loops)
	{
		for (UWorld* W : {World, Client}) if (W)
			Test->TestEqual(TEXT("Original pump leak sound follows fault, repair and destruction on each peer"), CountLoop(W, TEXT("S_C_Pipe_GushLoop_01")), Loops);
	}
	void Wait() { At = World->GetTimeSeconds(); }
	void Check(int32 Repairs, int32 Faults)
	{
		for (UWorld* W : {World, Client}) if (W)
		{
			Test->TestEqual(TEXT("Exactly one repair loop per active repair, on each peer"), CountLoop(W, TEXT("S_V2_Facility_Repair_Loop")), Repairs);
			Test->TestEqual(TEXT("Fault loop tracks persistent incident on each peer"), CountLoop(W, TEXT("S_V2_Facility_Sorter_FaultLoop")), Faults);
		}
	}
	FAutomationTestBase* Test; bool bSolo; TStrongObjectPtr<ULevelEditorPlaySettings> Settings;
	double Start = 0., At = 0.; int32 Phase = 0, FaultId = 0;
	UWorld* World = nullptr; UWorld* Client = nullptr; AMD_Character* Host = nullptr; AMD_TrashSorter* Sorter = nullptr; AMD_CoolingPump* Pump = nullptr;
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV2AudioSolo, "MarineDemolition.V2Audio.SoloPIE", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FV2AudioSolo::RunTest(const FString&)
{
	AddExpectedError(TEXT("Online Subsystem의 External UIExternal UI 모듈이 유효하지 않습니다"), EAutomationExpectedErrorFlags::Contains, 1);
	ADD_LATENT_AUTOMATION_COMMAND(FV2AudioPIE(this, true)); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV2AudioPeer, "MarineDemolition.V2Audio.TwoPlayerPIE", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FV2AudioPeer::RunTest(const FString&)
{
	AddExpectedError(TEXT("Online Subsystem의 External UIExternal UI 모듈이 유효하지 않습니다"), EAutomationExpectedErrorFlags::Contains, 2);
	ADD_LATENT_AUTOMATION_COMMAND(FV2AudioPIE(this, false)); return true;
}
#endif
