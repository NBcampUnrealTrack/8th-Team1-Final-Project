#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "World/Facility/MD_RepairInstruction.h"
#include "World/Wall/MD_PowerConduit.h"
#include "Component/Facility/MD_FacilityRepairComponent.h"
#include "Component/Interaction/MD_InteractionComponent.h"
#include "../../Component/Facility/MD_FacilityPlayTestGameMode.h"
#include "Character/MD_Character.h"
#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/TriggerBox.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "Editor.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "PlayInEditorDataTypes.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDInstructionAnswerTest,"MarineDemolition.RepairInstruction.Answers",
	EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMDInstructionAnswerTest::RunTest(const FString&)
{
	FRandomStream Rng(42); TArray<int32> Last={0,0,0,0};
	for(int32 I=0;I<1000;++I)
	{
		auto Next=AMD_RepairInstructionController::GenerateDifferentAnswer(Last,Rng);
		TestEqual(TEXT("Four columns"),Next.Num(),4); TestTrue(TEXT("Restart changes answer"),Next!=Last);
		TestTrue(TEXT("Not initially solved at top"),Next!=TArray<int32>({0,0,0,0}));
		for(int32 Row:Next) TestTrue(TEXT("One of five slots"),Row>=0 && Row<5);
		Last=MoveTemp(Next);
	}
	return true;
}

namespace
{
class FInstructionPIE final : public IAutomationLatentCommand
{
public:
	FInstructionPIE(FAutomationTestBase* InTest,bool InSolo,bool InStartBroken=false):Test(InTest),bSolo(InSolo),bStartBroken(InStartBroken)
	{
		Settings.Reset(DuplicateObject<ULevelEditorPlaySettings>(GetDefault<ULevelEditorPlaySettings>(),GetTransientPackage()));
		Settings->SetPlayNetMode(PIE_ListenServer); Settings->SetRunUnderOneProcess(true); Settings->SetPlayNumberOfClients(bSolo?1:2);
		FRequestPlaySessionParams P; P.EditorPlaySettings=Settings.Get(); P.GameModeOverride=AMD_FacilityPlayTestGameMode::StaticClass();
		P.GlobalMapOverride=TEXT("/Engine/Maps/Entry"); P.bAllowOnlineSubsystem=false; Started=FPlatformTime::Seconds(); GEditor->RequestPlaySession(P);
	}
	~FInstructionPIE() override { if(GEditor && GEditor->PlayWorld) GEditor->RequestEndPlayMap(); }
	bool Update() override
	{
		if(FPlatformTime::Seconds()-Started>100.) { Test->AddError(FString::Printf(TEXT("Instruction timeout phase %d"),Phase)); return true; }
		if(!World)
		{
			for(const auto& C:GEngine->GetWorldContexts()) if(C.WorldType==EWorldType::PIE && C.World() && C.World()->HasBegunPlay())
				{ if(C.World()->GetNetMode()!=NM_Client) World=C.World(); else Client=C.World(); }
			if(!World || (!bSolo && !Client)) { World=nullptr; return false; }
			for(TActorIterator<APlayerController> It(World);It;++It)
				if(It->IsLocalController()) { PC=*It; Pawn=Cast<AMD_Character>(It->GetPawn()); } else ServerRemote=Cast<AMD_Character>(It->GetPawn());
			if(!Pawn || (!bSolo && !ServerRemote)) { World=nullptr; return false; }
			Pawn->GetCharacterMovement()->DisableMovement(); Interaction=Pawn->FindComponentByClass<UMD_InteractionComponent>();
			if(!Test->TestNotNull(TEXT("Actual input component"),Interaction)) return true;
			if(!bSolo)
			{
				for(TActorIterator<APlayerController> It(Client);It;++It) if(It->IsLocalController()) { RemotePC=*It; RemotePawn=Cast<AMD_Character>(It->GetPawn()); }
				if(!RemotePawn) { World=nullptr; return false; }
				ServerRemote->GetCharacterMovement()->DisableMovement(); RemotePawn->GetCharacterMovement()->DisableMovement();
				RemoteInteraction=RemotePawn->FindComponentByClass<UMD_InteractionComponent>();
			}
			Setup(); Aim(Pawn,PC,Guide); Phase=0; Wait(.7f); return false;
		}
		if(World->GetTimeSeconds()-At<Delay) return false;
		switch(Phase)
		{
		case 0:
			Test->TestTrue(TEXT("Linked wire break starts instruction event"),Event->State.bActive);
			Test->TestFalse(TEXT("Room occupancy alone never locks"),Event->State.bDoorLocked);
			Test->TestTrue(TEXT("Door starts open"),DoorOpen());
			Test->TestFalse(TEXT("Duplicate starts rejected"),Event->StartRepairIncident());
			SavedTargets=Event->State.TargetRows; SavedRevision=Event->State.Revision; SavedFault=Event->Fault->GetSnapshot();
			if(!bSolo)
			{
				for(TActorIterator<AMD_RepairInstructionController> It(Client);It;++It) if(It->GetFName()==Event->GetFName()) RemoteEvent=*It;
				Test->TestNotNull(TEXT("Controller replicated to client"),RemoteEvent);
			}
			Press(Interaction,Guide); Phase=1; Wait(.25f); break;
		case 1:
			Test->TestTrue(TEXT("E input reads guide"),Event->State.bGuideRead);
			Test->TestEqual(TEXT("Only multiplayer interaction locks"),Event->State.bDoorLocked,!bSolo);
			Test->TestEqual(TEXT("Door pose follows lock"),DoorOpen(),bSolo);
			if(!bSolo && RemoteEvent)
			{
				Test->TestNotNull(TEXT("Client receives managed door"),RemoteEvent->InstructionDoor.Get());
				if(RemoteEvent->InstructionDoor) Test->TestFalse(TEXT("Managed door is closed on remote peer"),FindFProperty<FBoolProperty>(RemoteEvent->InstructionDoor->GetClass(),TEXT("bDoorOpen"))->GetPropertyValue_InContainer(RemoteEvent->InstructionDoor));
			}
			Aim(Pawn,PC,Restart); Phase=2; Wait(.2f); break;
		case 2: Press(Interaction,Restart); Phase=3; Wait(.25f); break;
		case 3:
			Test->TestTrue(TEXT("Restart opens door"),DoorOpen()); Test->TestFalse(TEXT("Restart is not guide input"),Event->State.bGuideRead);
			if(!bSolo && RemoteEvent && RemoteEvent->InstructionDoor) Test->TestTrue(TEXT("Restart opens door on remote peer"),FindFProperty<FBoolProperty>(RemoteEvent->InstructionDoor->GetClass(),TEXT("bDoorOpen"))->GetPropertyValue_InContainer(RemoteEvent->InstructionDoor));
			Test->TestTrue(TEXT("Restart guarantees different answer"),Event->State.TargetRows!=SavedTargets);
			Test->TestEqual(TEXT("Restart increments input revision"),Event->State.Revision,SavedRevision+1);
			Test->TestEqual(TEXT("Restart preserves original fault ID"),Event->Fault->GetSnapshot().FaultId,SavedFault.FaultId);
			Test->TestEqual(TEXT("Restart preserves event start time"),Event->Fault->GetSnapshot().FaultStartedAt,SavedFault.FaultStartedAt);
			Test->TestTrue(TEXT("Restart does not repair wire"),Wire->FacilityRepair->IsFaultActive());
			Test->TestFalse(TEXT("Queued old revision cannot relock"),Event->ApplyControl(Guide,Pawn,SavedRevision));
			Phase=4; Wait(1.0); break;
		case 4:
			Test->TestTrue(TEXT("Elapsed time and occupancy keep restarted door open"),DoorOpen());
			Aim(Pawn,PC,Guide); Phase=5; Wait(.2f); break;
		case 5: Press(Interaction,Guide); Phase=6; Wait(.25f); break;
		case 6:
			Test->TestEqual(TEXT("A fresh guide input relocks only multiplayer"),Event->State.bDoorLocked,!bSolo);
			if(!bSolo)
			{
				SavedTargets=Event->State.TargetRows;
				RemotePS=ServerRemote->GetPlayerState(); World->GetGameState()->PlayerArray.Remove(RemotePS);
				Phase=7; Wait(.3f); break;
			}
			Phase=8; Wait(0.0); break;
		case 7:
			Test->TestFalse(TEXT("Two to one player releases reader"),Event->State.bDoorLocked); Test->TestTrue(TEXT("Solo fallback physically opens door"),DoorOpen());
			Test->TestTrue(TEXT("Solo fallback preserves answer"),Event->State.TargetRows==SavedTargets);
			World->GetGameState()->PlayerArray.AddUnique(RemotePS);
			Phase=8; Wait(.2f); break;
		case 8:
			if(!bSolo)
			{
				if(!RemoteEvent) return true;
				Test->TestTrue(TEXT("Client receives exact restart answer"),RemoteEvent->State.TargetRows==Event->State.TargetRows);
				for(TActorIterator<AMD_RepairInstructionControl> It(Client);It;++It)
					if(It->Controller==RemoteEvent && It->ControlKind==EMD_InstructionControlKind::Knob && It->Column==Column) RemoteKnob=*It;
			}
			if(Event->State.CurrentRows[Column]==Event->State.TargetRows[Column])
				{ if(++Column<4) { Wait(0); break; } Phase=10; Wait(.3f); break; }
			if(bSolo) Aim(Pawn,PC,Knobs[Column]);
			else
			{
				if(!Test->TestNotNull(TEXT("Remote knob endpoint"),RemoteKnob)) return true;
				Aim(ServerRemote,Cast<APlayerController>(ServerRemote->GetController()),Knobs[Column]); Aim(RemotePawn,RemotePC,RemoteKnob);
			}
			Phase=9; Wait(.25f); break;
		case 9:
			if(bSolo) Press(Interaction,Knobs[Column]); else Press(RemoteInteraction,RemoteKnob);
			if(Event->State.bActive) { Phase=8; Wait(.25f); }
			else { Phase=10; Wait(.5f); }
			break;
		case 10:
			Test->TestFalse(TEXT("All columns matching completes event"),Event->State.bActive);
			Test->TestFalse(TEXT("Validated puzzle completes only linked wire fault"),Wire->FacilityRepair->IsFaultActive());
			Test->TestTrue(TEXT("Unrelated wire remains broken"),UnrelatedWire->FacilityRepair->IsFaultActive());
			Test->TestTrue(TEXT("Readers can leave after success"),DoorOpen());
			Test->TestFalse(TEXT("Completed incident rejects late guide input"),Event->ApplyControl(Guide,Pawn,Event->State.Revision));
			if(!bSolo && RemoteEvent) Test->TestFalse(TEXT("Remote receives repaired state"),RemoteEvent->State.bActive);
			GEditor->RequestEndPlayMap(); return true;
		}
		return false;
	}
private:
	void Setup()
	{
		Event=World->SpawnActorDeferred<AMD_RepairInstructionController>(LoadClass<AMD_RepairInstructionController>(nullptr,TEXT("/Game/Blueprint/Facility/RepairInstruction/BP_MD_RepairInstructionController.BP_MD_RepairInstructionController_C")),FTransform());
		Event->Fault->bStartBroken=bStartBroken;
		FActorSpawnParameters S; S.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Room=World->SpawnActor<ATriggerBox>(FVector(10000,100,500),FRotator::ZeroRotator,S);
		Cast<UBoxComponent>(Room->GetCollisionComponent())->SetBoxExtent(FVector(250,400,250)); Room->SetActorEnableCollision(false);
		auto* DC=LoadClass<AActor>(nullptr,TEXT("/Game/Assets/X7/Interior/Objects/Door/BP_Door.BP_Door_C"));
		Door=World->SpawnActorDeferred<AActor>(DC,FTransform(FVector(11000,1000,500))); Door->Tags.Add(UMD_FacilityFaultComponent::FaultExemptActorTag); Door->bAlwaysRelevant=true;
		if(auto* P=FindFProperty<FBoolProperty>(Door->GetClass(),TEXT("bLocked"))) P->SetPropertyValue_InContainer(Door,false);
		Door->FinishSpawning(FTransform(FVector(11000,1000,500)));
		Event->InstructionDoor=Door; Event->InstructionRoom=Room;
		Guide=Control(EMD_InstructionControlKind::Guide,0,FVector(10000,0,500)); Event->Guide=Guide;
		Restart=Control(EMD_InstructionControlKind::Restart,0,FVector(10000,250,500)); Event->Restart=Restart;
		for(int32 I=0;I<4;++I) { Knobs[I]=Control(EMD_InstructionControlKind::Knob,I,FVector(10000,I*300+2000,500)); Event->Knobs.Add(Knobs[I]); }
		Wire=World->SpawnActor<AMD_PowerConduit>(FVector(20000,1000,500),FRotator::ZeroRotator,S);
		UnrelatedWire=World->SpawnActor<AMD_PowerConduit>(FVector(23000,1000,500),FRotator::ZeroRotator,S);
		Event->PowerConduits.Add(Wire); Event->FinishSpawning(FTransform());
		Test->TestTrue(TEXT("Fixture configured"),Event->HasValidConfiguration());
		Test->TestTrue(TEXT("Unrelated power incident starts independently"),UnrelatedWire->FacilityRepair->TryBreak());
		Test->TestTrue(TEXT("Actual wire shared fault event starts"),Wire->FacilityRepair->TryBreak());
	}
	AMD_RepairInstructionControl* Control(EMD_InstructionControlKind Kind,int32 Col,FVector Location)
	{
		const TCHAR* Path=Kind==EMD_InstructionControlKind::Guide?TEXT("/Game/Blueprint/Facility/RepairInstruction/BP_MD_RepairInstructionGuide.BP_MD_RepairInstructionGuide_C"):
			Kind==EMD_InstructionControlKind::Restart?TEXT("/Game/Blueprint/Facility/RepairInstruction/BP_MD_RepairInstructionRestart.BP_MD_RepairInstructionRestart_C"):TEXT("/Game/Blueprint/Facility/RepairInstruction/BP_MD_RepairInstructionKnob.BP_MD_RepairInstructionKnob_C");
		auto* C=World->SpawnActorDeferred<AMD_RepairInstructionControl>(LoadClass<AMD_RepairInstructionControl>(nullptr,Path),FTransform(Location));
		C->Controller=Event; C->ControlKind=Kind; C->Column=Col; C->FinishSpawning(FTransform(Location)); return C;
	}
	void Aim(AMD_Character* P,APlayerController* C,AActor* Target)
	{
		P->SetActorLocation(Target->GetActorLocation()+FVector(140,0,0)); C->SetControlRotation(FRotator(0,180,0));
		if(const auto* Camera=P->FindComponentByClass<UCameraComponent>()) P->AddActorWorldOffset(Target->GetActorLocation()+FVector(140,0,0)-Camera->GetComponentLocation());
	}
	void Press(UMD_InteractionComponent* I,AActor* Target)
	{
		I->TickComponent(0.f,LEVELTICK_All,nullptr); Test->TestEqual(TEXT("Real aim focuses requested control"),I->GetCurrentTarget(),Target); I->Interact();
	}
	bool DoorOpen() const { return FindFProperty<FBoolProperty>(Door->GetClass(),TEXT("bDoorOpen"))->GetPropertyValue_InContainer(Door); }
	void Wait(float D) { At=World->GetTimeSeconds(); Delay=D; }
	FAutomationTestBase* Test; bool bSolo; bool bStartBroken; int32 Phase=0,Column=0,SavedRevision=0;
	double Started=0.,At=0.; float Delay=0;
	TStrongObjectPtr<ULevelEditorPlaySettings> Settings;
	UWorld* World=nullptr; UWorld* Client=nullptr;
	AMD_Character* Pawn=nullptr; AMD_Character* ServerRemote=nullptr; AMD_Character* RemotePawn=nullptr;
	APlayerController* PC=nullptr; APlayerController* RemotePC=nullptr; APlayerState* RemotePS=nullptr;
	UMD_InteractionComponent* Interaction=nullptr; UMD_InteractionComponent* RemoteInteraction=nullptr;
	AMD_RepairInstructionController* Event=nullptr; AMD_RepairInstructionController* RemoteEvent=nullptr;
	AMD_RepairInstructionControl* Guide=nullptr; AMD_RepairInstructionControl* Restart=nullptr; AMD_RepairInstructionControl* Knobs[4]={}; AMD_RepairInstructionControl* RemoteKnob=nullptr;
	AActor* Door=nullptr; ATriggerBox* Room=nullptr; AMD_PowerConduit* Wire=nullptr; AMD_PowerConduit* UnrelatedWire=nullptr;
	TArray<int32> SavedTargets; FMD_FacilitySnapshot SavedFault;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDInstructionSoloTest,"MarineDemolition.RepairInstruction.SoloPIE",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMDInstructionSoloTest::RunTest(const FString&)
{
	if(!GEditor || GEditor->PlayWorld) { AddError(TEXT("Idle validation editor required")); return false; }
	AddExpectedError(TEXT("Online Subsystem의 External UIExternal UI 모듈이 유효하지 않습니다"),EAutomationExpectedErrorFlags::Contains,1);
	ADD_LATENT_AUTOMATION_COMMAND(FInstructionPIE(this,true)); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDInstructionTwoPlayerTest,"MarineDemolition.RepairInstruction.TwoPlayerPIE",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMDInstructionTwoPlayerTest::RunTest(const FString&)
{
	if(!GEditor || GEditor->PlayWorld) { AddError(TEXT("Idle validation editor required")); return false; }
	AddExpectedError(TEXT("Online Subsystem의 External UIExternal UI 모듈이 유효하지 않습니다"),EAutomationExpectedErrorFlags::Contains,2);
	ADD_LATENT_AUTOMATION_COMMAND(FInstructionPIE(this,false)); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDInstructionInitialFaultTest,"MarineDemolition.RepairInstruction.InitialFaultPIE",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMDInstructionInitialFaultTest::RunTest(const FString&)
{
	if(!GEditor || GEditor->PlayWorld) { AddError(TEXT("Idle validation editor required")); return false; }
	AddExpectedError(TEXT("Online Subsystem의 External UIExternal UI 모듈이 유효하지 않습니다"),EAutomationExpectedErrorFlags::Contains,1);
	ADD_LATENT_AUTOMATION_COMMAND(FInstructionPIE(this,true,true)); return true;
}
#endif
