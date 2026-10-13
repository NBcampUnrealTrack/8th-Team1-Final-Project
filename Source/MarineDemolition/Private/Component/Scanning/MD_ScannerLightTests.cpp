#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Component/Scanning/MD_ScannerComponent.h"
#include "Component/Equipment/MD_EquipHotbarComponent.h"
#include "Component/Facility/MD_FacilityFaultManagerComponent.h"
#include "Component/Facility/MD_FacilityRepairComponent.h"
#include "../Facility/MD_FacilityPlayTestGameMode.h"
#include "Character/MD_Character.h"
#include "World/Wall/MD_PowerConduit.h"
#include "Data/Equipment/MD_ToolDefinition.h"
#include "Components/SpotLightComponent.h"
#include "AbilitySystemComponent.h"
#include "GameplayTags/MD_GameplayTags.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "Editor.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "PlayInEditorDataTypes.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
class FScannerLightPIE final:public IAutomationLatentCommand
{
public:
	FScannerLightPIE(FAutomationTestBase* T,bool Solo):Test(T),bSolo(Solo)
	{
		Settings.Reset(DuplicateObject<ULevelEditorPlaySettings>(GetDefault<ULevelEditorPlaySettings>(),GetTransientPackage()));
		Settings->SetPlayNetMode(PIE_ListenServer);Settings->SetRunUnderOneProcess(true);Settings->SetPlayNumberOfClients(Solo?1:2);
		FRequestPlaySessionParams P;P.EditorPlaySettings=Settings.Get();P.GameModeOverride=AMD_FacilityPlayTestGameMode::StaticClass();
		P.GlobalMapOverride=TEXT("/Engine/Maps/Entry");P.bAllowOnlineSubsystem=false;Start=FPlatformTime::Seconds();GEditor->RequestPlaySession(P);
	}
	~FScannerLightPIE() override { if(GEditor && GEditor->PlayWorld) GEditor->RequestEndPlayMap(); }
	bool Update() override
	{
		if(FPlatformTime::Seconds()-Start>90) { Test->AddError(TEXT("Scanner light PIE timed out"));return true; }
		if(!World)
		{
			for(const auto& C:GEngine->GetWorldContexts()) if(C.WorldType==EWorldType::PIE && C.World() && C.World()->HasBegunPlay())
				{ if(C.World()->GetNetMode()==NM_Client) Client=C.World();else World=C.World(); }
			if(!World || (!bSolo && !Client)) { World=nullptr;return false; }
			for(TActorIterator<APlayerController> It(World);It;++It) if(It->IsLocalController()) Host=Cast<AMD_Character>(It->GetPawn());else Remote=Cast<AMD_Character>(It->GetPawn());
			if(!Host || (!bSolo && !Remote)) { World=nullptr;return false; }
			Manager=World->GetGameState()->FindComponentByClass<UMD_FacilityFaultManagerComponent>();if(!Manager) { World=nullptr;return false; }
			auto* Tool=LoadObject<UMD_ToolDefinition>(nullptr,TEXT("/Game/Equipment/Tools/DA_Tool_Scanner.DA_Tool_Scanner"));
			if(!Test->TestNotNull(TEXT("Actual scanner tool asset"),Tool)) return true;
			for(AMD_Character* Pawn:{Host,Remote}) if(Pawn)
			{
				auto* Hotbar=Pawn->GetHotbarComponent();for(int32 I=0;I<Hotbar->GetSlotCount();++I) Hotbar->SetItemAtSlot(I,nullptr,0);
				Test->TestTrue(TEXT("Equip actual scanner"),Hotbar->SetItemAtSlot(0,Tool,1));Hotbar->SelectSlot(0);
			}
			FActorSpawnParameters S;S.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			WireA=World->SpawnActor<AMD_PowerConduit>(FVector(20000,0,0),FRotator::ZeroRotator,S);
			WireB=World->SpawnActor<AMD_PowerConduit>(FVector(22000,0,0),FRotator::ZeroRotator,S);
			AActor* Panel=World->SpawnActor<AActor>();auto* Fault=NewObject<UMD_FacilityFaultComponent>(Panel);Fault->Kind=EMD_FacilityKind::WiringPanel;
			Fault->RegisterComponent();Manager->RegisterFacility(Fault);Fault->TryBreak();Wait();return false;
		}
		if(World->GetTimeSeconds()-At<.6f) return false;
		switch(Phase++)
		{
		case 0:
			Test->TestFalse(TEXT("Ordinary wiring panel is not a blackout"),Manager->IsBlackoutActive());CheckLights(false);
			Test->TestTrue(TEXT("Actual wire fault"),WireA->FacilityRepair->TryBreak());break;
		case 1:
			Test->TestTrue(TEXT("Wire fault sets blackout"),Manager->IsBlackoutActive());CheckLights(true);
			Host->GetHotbarComponent()->SelectSlot(1);break;
		case 2:
			Test->TestFalse(TEXT("Scanner in inventory does not light an empty hand"),Host->GetScannerComponent()->IsBlackoutLightActive());
			if(Client)
			{
				int32 MatchedHostCount=0;
				for(TActorIterator<AMD_Character> It(Client);It;++It)
					if(It->GetPlayerState() && It->GetPlayerState()->GetPlayerId()==Host->GetPlayerState()->GetPlayerId())
					{
						++MatchedHostCount;
						Test->TestFalse(TEXT("Empty hand also switches peer light off"),It->GetScannerComponent()->IsBlackoutLightActive());
					}
				Test->TestEqual(TEXT("Peer check resolves exactly one host pawn"),MatchedHostCount,1);
			}
			Host->GetHotbarComponent()->SelectSlot(0);
			Host->GetAbilitySystemComponent()->AddLooseGameplayTag(MDGameplayTags::State_Dead);break;
		case 3:
			Test->TestFalse(TEXT("Dead holder has no light"),Host->GetScannerComponent()->IsBlackoutLightActive());
			Host->GetAbilitySystemComponent()->RemoveLooseGameplayTag(MDGameplayTags::State_Dead);
			Test->TestTrue(TEXT("Second wire breaks"),WireB->FacilityRepair->TryBreak());WireA->DebugForceRepair(nullptr);break;
		case 4:
			Test->TestTrue(TEXT("One remaining wire keeps blackout"),Manager->IsBlackoutActive());CheckLights(true);WireB->DebugForceRepair(nullptr);break;
		case 5:
			Test->TestFalse(TEXT("Final repair restores power"),Manager->IsBlackoutActive());CheckLights(false);
			Test->TestTrue(TEXT("Wire can break again"),WireA->FacilityRepair->TryBreak());WireA->Destroy();break;
		case 6:
			Test->TestFalse(TEXT("Removing last broken source clears blackout"),Manager->IsBlackoutActive());CheckLights(false);
			GEditor->RequestEndPlayMap();return true;
		}
		Wait();return false;
	}
private:
	void CheckLights(bool On)
	{
		for(UWorld* W:{World,Client}) if(W)
		{
			auto* M=W->GetGameState()->FindComponentByClass<UMD_FacilityFaultManagerComponent>();Test->TestEqual(TEXT("Blackout state reaches each peer"),M->IsBlackoutActive(),On);
			for(TActorIterator<AMD_Character> It(W);It;++It)
			{
				auto* Scanner=It->GetScannerComponent();Test->TestEqual(TEXT("Held scanner light on owning and simulated peers"),Scanner->IsBlackoutLightActive(),On);
				Test->TestNotNull(TEXT("Runtime spotlight exists"),Scanner->GetBlackoutLight());
			}
		}
	}
	void Wait() { At=World->GetTimeSeconds(); }
	FAutomationTestBase* Test;bool bSolo;TStrongObjectPtr<ULevelEditorPlaySettings> Settings;
	double Start=0,At=0;int32 Phase=0;UWorld* World=nullptr;UWorld* Client=nullptr;
	AMD_Character* Host=nullptr;AMD_Character* Remote=nullptr;UMD_FacilityFaultManagerComponent* Manager=nullptr;
	AMD_PowerConduit* WireA=nullptr;AMD_PowerConduit* WireB=nullptr;
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FScannerLightSoloTest,"MarineDemolition.ScannerLight.SoloPIE",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FScannerLightSoloTest::RunTest(const FString&)
{
	AddExpectedError(TEXT("Online Subsystem의 External UIExternal UI 모듈이 유효하지 않습니다"),EAutomationExpectedErrorFlags::Contains,1);
	ADD_LATENT_AUTOMATION_COMMAND(FScannerLightPIE(this,true));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FScannerLightPeerTest,"MarineDemolition.ScannerLight.TwoPlayerPIE",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FScannerLightPeerTest::RunTest(const FString&)
{
	AddExpectedError(TEXT("Online Subsystem의 External UIExternal UI 모듈이 유효하지 않습니다"),EAutomationExpectedErrorFlags::Contains,2);
	ADD_LATENT_AUTOMATION_COMMAND(FScannerLightPIE(this,false));return true;
}
#endif
