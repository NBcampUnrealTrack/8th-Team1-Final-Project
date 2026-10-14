#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "World/Facility/MD_RepairInstruction.h"
#include "World/Wall/MD_PowerConduit.h"
#include "Component/Facility/MD_FacilityRepairComponent.h"
#include "Component/Interaction/MD_InteractionComponent.h"
#include "../../Component/Facility/MD_FacilityPlayTestGameMode.h"
#include "Character/MD_Character.h"
#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Camera/CameraActor.h"
#include "UI/Game/MD_BreakerOperateWidget.h"
#include "AbilitySystemComponent.h"
#include "GameplayTags/MD_GameplayTags.h"
#include "UI/MD_LocalUIManager.h"
#include "UI/Foundation/MD_WidgetStack.h"
#include "Engine/LocalPlayer.h"
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
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Widgets/SWidget.h"
#include "Input/Events.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "UnrealClient.h"

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
	FInstructionPIE(FAutomationTestBase* InTest,bool InSolo,bool InStartBroken=false,bool InScreenEvents=false):Test(InTest),bSolo(InSolo),bStartBroken(InStartBroken),bScreenEvents(InScreenEvents)
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
			CheckGuideLight(Guide,Event->State);
			Test->TestFalse(TEXT("Separate restart mesh is hidden"),Restart->Body->IsVisible());
			Test->TestFalse(TEXT("Separate restart label is hidden"),Restart->Heading->IsVisible());
			Test->TestTrue(TEXT("Restart query is centered on the original button"),Restart->GetActorLocation().Equals(Guide->GetRestartButtonWorldLocation()+Guide->GetActorForwardVector()*7.,.01));
			Test->TestTrue(TEXT("Independent breaker incident starts instruction event"),Event->State.bActive);
			Test->TestFalse(TEXT("Room occupancy alone never locks"),Event->State.bDoorLocked);
			Test->TestTrue(TEXT("Door starts open"),DoorOpen());
			Test->TestFalse(TEXT("Duplicate starts rejected"),Event->StartRepairIncident());
			SavedTargets=Event->State.TargetRows; SavedRevision=Event->State.Revision; SavedFault=Event->Fault->GetSnapshot();
			if(!bSolo)
			{
				for(TActorIterator<AMD_RepairInstructionController> It(Client);It;++It) if(It->GetFName()==Event->GetFName()) RemoteEvent=*It;
				Test->TestNotNull(TEXT("Controller replicated to client"),RemoteEvent);
				if(RemoteEvent) CheckGuideLight(RemoteEvent->Guide,RemoteEvent->State);
			}
			Press(Interaction,Guide); Phase=1; Wait(.25f); break;
		case 1:
			CheckGuideLight(Guide,Event->State);
			if(!bSolo && RemoteEvent) CheckGuideLight(RemoteEvent->Guide,RemoteEvent->State);
			Test->TestTrue(TEXT("E input reads guide"),Event->State.bGuideRead);
			Test->TestEqual(TEXT("Only multiplayer interaction locks"),Event->State.bDoorLocked,!bSolo);
			Test->TestEqual(TEXT("Door pose follows lock"),DoorOpen(),bSolo);
			if(!bSolo && RemoteEvent)
			{
				Test->TestNotNull(TEXT("Client receives managed door"),RemoteEvent->InstructionDoor.Get());
				if(RemoteEvent->InstructionDoor) Test->TestFalse(TEXT("Managed door is closed on remote peer"),FindFProperty<FBoolProperty>(RemoteEvent->InstructionDoor->GetClass(),TEXT("bDoorOpen"))->GetPropertyValue_InContainer(RemoteEvent->InstructionDoor));
			}
			Aim(Pawn,PC,Restart); Phase=2; Wait(.2f); break;
		case 2:
			if(!bSolo && RemoteEvent)
			{
				Aim(ServerRemote,Cast<APlayerController>(ServerRemote->GetController()),Restart);
				Aim(RemotePawn,RemotePC,RemoteEvent->Restart);
				Phase=15; Wait(.3f); break;
			}
			Press(Interaction,Restart); Phase=3; Wait(.25f); break;
		case 15: Press(RemoteInteraction,RemoteEvent->Restart); Phase=3; Wait(.25f); break;
		case 3:
			CheckGuideLight(Guide,Event->State);
			Test->TestTrue(TEXT("Restart button briefly lights on host"),CastChecked<UMaterialInstanceDynamic>(Guide->Body->GetMaterial(0))->K2_GetScalarParameterValue(TEXT("RestartPulse"))>0.f);
			if(!bSolo && RemoteEvent && RemoteEvent->Guide)
				Test->TestTrue(TEXT("Restart flash replicates to peer"),CastChecked<UMaterialInstanceDynamic>(RemoteEvent->Guide->Body->GetMaterial(0))->K2_GetScalarParameterValue(TEXT("RestartPulse"))>0.f);
			if(!bSolo && RemoteEvent) CheckGuideLight(RemoteEvent->Guide,RemoteEvent->State);
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
			Test->TestEqual(TEXT("Restart button light expires"),CastChecked<UMaterialInstanceDynamic>(Guide->Body->GetMaterial(0))->K2_GetScalarParameterValue(TEXT("RestartPulse")),0.f);
			Test->TestTrue(TEXT("Elapsed time and occupancy keep restarted door open"),DoorOpen());
			Aim(Pawn,PC,Guide); Phase=5; Wait(.2f); break;
		case 5: Press(Interaction,Guide); Phase=6; Wait(.25f); break;
		case 6:
			CheckGuideLight(Guide,Event->State);
			if(!bSolo && RemoteEvent) CheckGuideLight(RemoteEvent->Guide,RemoteEvent->State);
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
			if(!bOpened)
			{
				// NullRHI doesn't paint the stack's Slate transition animation. Only this test world's stack skips it.
				auto* OperatorPC=bSolo?PC:RemotePC;
				if(!bScreenEvents) if(auto* UI=OperatorPC->GetLocalPlayer()->GetSubsystem<UMD_LocalUIManager>())
					if(auto* Stack=UI->FindLayerStack(MDGameplayTags::UI_Layer_GameMenu)) Stack->SetTransitionDuration(0);
				if(bSolo) Aim(Pawn,PC,Event,2);
				else { Aim(ServerRemote,Cast<APlayerController>(ServerRemote->GetController()),Event,2); Aim(RemotePawn,RemotePC,RemoteEvent,2); }
				Phase=11; Wait(.25f); break;
			}
			if(!bSolo)
			{
				if(!RemoteEvent) return true;
				Test->TestTrue(TEXT("Client receives exact restart answer"),RemoteEvent->State.TargetRows==Event->State.TargetRows);
				Test->TestEqual(TEXT("Remote owns four integrated knob components"),RemoteEvent->KnobBounds.Num(),4);
				Test->TestTrue(TEXT("Remote knob rows match the server"),RemoteEvent->State.CurrentRows==Event->State.CurrentRows);
			}
			if(Event->State.CurrentRows[Column]==Event->State.TargetRows[Column])
				{ if(++Column<4) { Wait(0); break; } Phase=10; Wait(.3f); break; }
			Phase=9; Wait(.25f); break;
		case 9:
		{
			auto* Widget=(bSolo?Interaction:RemoteInteraction)->GetBreakerWidget();
			if(!Test->TestNotNull(TEXT("Operating client owns drag widget"),Widget)) return true;
			auto* Board=bSolo?Event:RemoteEvent; auto* Operator=bSolo?Pawn:ServerRemote;
			const int32 Id=Event->OperationId; const int32 Row=Event->State.TargetRows[Column];
			Test->TestFalse(TEXT("Legacy E-to-cycle cannot bypass drag session"),Event->ApplyKnob(Operator,Column,Event->State.Revision));
			Test->TestFalse(TEXT("Stale operation rejected"),Event->ApplyKnobSlot(Operator,Column,Row,Event->State.Revision,Id+1));
			Test->TestFalse(TEXT("Stale answer revision rejected"),Event->ApplyKnobSlot(Operator,Column,Row,Event->State.Revision-1,Id));
			Test->TestFalse(TEXT("Out of range slots rejected"),Event->ApplyKnobSlot(Operator,Column,5,Event->State.Revision,Id));
			if(!bSolo) Test->TestFalse(TEXT("Reader cannot spoof the operator"),Event->ApplyKnobSlot(Pawn,Column,Row,Event->State.Revision,Id));
			const FVector Normal=Board->GetPanelNormal();
			const FVector Start=Board->GetActorTransform().TransformPosition(Board->GetKnobSlotCenter(Column,Board->State.CurrentRows[Column]));
			const FGeometry Viewport=UWidgetLayoutLibrary::GetViewportWidgetGeometry(Widget);
			auto AbsolutePoint=[&](const FVector& WorldPoint)
			{
				FVector2D Screen; auto* OperatorPC=bSolo?PC:RemotePC; Test->TestTrue(TEXT("Knob projects to the operation viewport"),OperatorPC->ProjectWorldLocationToScreen(WorldPoint,Screen));
				int32 W=0,H=0; OperatorPC->GetViewportSize(W,H);
				return Viewport.LocalToAbsolute(Screen/FVector2D(W,H)*Viewport.GetLocalSize());
			};
			FVector2D AbsStart;
			if(bScreenEvents)
			{
				AbsStart=AbsolutePoint(Start); TSet<FKey> Buttons; Buttons.Add(EKeys::LeftMouseButton);
				const FPointerEvent Mouse(0,AbsStart,AbsStart,Buttons,EKeys::LeftMouseButton,0.f,FModifierKeysState());
				Widget->TakeWidget()->OnMouseButtonDown(Widget->GetCachedGeometry(),Mouse);
				Test->TestEqual(TEXT("Slate mouse-down selects the projected knob"),Widget->GetDraggingColumn(),Column);
			}
			else Test->TestTrue(TEXT("Left mouse press selects the actual knob"),Widget->PointerPressedRay(Start+Normal*180,-Normal));
			const auto BeforeRows=Event->State.CurrentRows;
			const FVector NearHole=Board->GetActorTransform().TransformPosition(Board->GetKnobSlotCenter(Column,Row)+FVector(0,30,Row==0?-2:2));
			FVector2D AbsEnd;
			if(bScreenEvents)
			{
				AbsEnd=AbsolutePoint(NearHole); TSet<FKey> Buttons; Buttons.Add(EKeys::LeftMouseButton);
				Widget->TakeWidget()->OnMouseMove(Widget->GetCachedGeometry(),FPointerEvent(0,AbsEnd,AbsStart,Buttons,EKeys::Invalid,0.f,FModifierKeysState()));
			}
			else Widget->PointerMovedRay(NearHole+Normal*180,-Normal);
			Test->TestTrue(TEXT("Drag preview does not mutate authoritative rows"),Event->State.CurrentRows==BeforeRows);
			const auto Bounds=Board->KnobMeshes[Column]->CalcBounds(Board->KnobMeshes[Column]->GetRelativeTransform());
			Test->TestTrue(TEXT("Horizontal mouse movement cannot switch columns"),FMath::Abs(Bounds.Origin.Y-Board->GetKnobSlotCenter(Column,Row).Y)<.02);
			if(bScreenEvents)
				Widget->TakeWidget()->OnMouseButtonUp(Widget->GetCachedGeometry(),FPointerEvent(0,AbsEnd,AbsEnd,TSet<FKey>(),EKeys::LeftMouseButton,0.f,FModifierKeysState()));
			else Test->TestTrue(TEXT("Mouse release requests the nearest exact slot"),Widget->PointerReleased());
			if(Event->State.bActive) { Phase=8; Wait(.25f); }
			else { Phase=10; Wait(.5f); }
			break;
		}
		case 11:
			BeforeOpenRows=Event->State.CurrentRows;
			if(bSolo) Press(Interaction,Event); else Press(RemoteInteraction,RemoteEvent);
			Phase=12; Wait(.7f); break;
		case 12:
		{
			auto* Widget=(bSolo?Interaction:RemoteInteraction)->GetBreakerWidget();
			if(!Test->TestNotNull(TEXT("E opens the operation screen"),Widget)) return true;
			auto* OperatorPC=bSolo?PC:RemotePC;
			Test->TestTrue(TEXT("E opens without advancing any knob"),Event->State.CurrentRows==BeforeOpenRows);
			Test->TestEqual(TEXT("Camera blends to the breaker front"),OperatorPC->GetViewTarget(),static_cast<AActor*>(Widget->GetOperationCamera()));
			Test->TestTrue(TEXT("Gameplay look and movement blocked during drag"),OperatorPC->IsMoveInputIgnored() && OperatorPC->IsLookInputIgnored());
Test->TestTrue(TEXT("Local body cannot obscure the breaker camera"),OperatorPC->HiddenActors.Contains(bSolo?Pawn:RemotePawn));
			if(!bSolo) Test->TestEqual(TEXT("Reader's camera is unaffected"),PC->GetViewTarget(),static_cast<AActor*>(Pawn));
			if(bScreenEvents && bTestedExit)
			{
				OperatorPC->ConsoleCommand(TEXT("viewmode unlit"));
				const FString Dir=FPaths::ProjectSavedDir()/TEXT("Automation/BreakerDrag"); IFileManager::Get().MakeDirectory(*Dir,true);
				FScreenshotRequest::RequestScreenshot(Dir/TEXT("front.png"),true,false);
			}
			if(!bTestedExit)
			{
				SavedOperationId=Event->OperationId;
if(bStartBroken) Pawn->GetAbilitySystemComponent()->AddLooseGameplayTag(MDGameplayTags::State_Dead); else Widget->DeactivateWidget();
Phase=13; Wait(.5f); break;
			}
			bOpened=true; Phase=8; Wait(.1f); break;
		}
		case 13:
		{
			auto* OperatorPC=bSolo?PC:RemotePC;
			Test->TestEqual(TEXT("Closing restores the player's camera"),OperatorPC->GetViewTarget(),static_cast<AActor*>(bSolo?Pawn:RemotePawn));
			Test->TestFalse(TEXT("Closing releases gameplay input"),OperatorPC->IsMoveInputIgnored() || OperatorPC->IsLookInputIgnored());
Test->TestFalse(TEXT("Closing restores local body visibility"),OperatorPC->HiddenActors.Contains(bSolo?Pawn:RemotePawn));
if(bStartBroken) Pawn->GetAbilitySystemComponent()->RemoveLooseGameplayTag(MDGameplayTags::State_Dead);
			Test->TestNull(TEXT("Closing releases the authoritative operator"),Event->OperatingPawn.Get());
			Test->TestFalse(TEXT("Closed operation rejects queued drags"),Event->ApplyKnobSlot(bSolo?Pawn:ServerRemote,0,1,Event->State.Revision,SavedOperationId));
			bTestedExit=true; Phase=8; Wait(.1f); break;
		}
		case 10:
			CheckGuideLight(Guide,Event->State);
			Test->TestFalse(TEXT("All columns matching completes event"),Event->State.bActive);
			Test->TestTrue(TEXT("Validated puzzle preserves tape-repaired wire incident"),Wire->FacilityRepair->IsFaultActive());
			Test->TestTrue(TEXT("Unrelated wire remains broken"),UnrelatedWire->FacilityRepair->IsFaultActive());
			Test->TestTrue(TEXT("Readers can leave after success"),DoorOpen());
			Test->TestFalse(TEXT("Completed incident rejects late guide input"),Event->ApplyControl(Guide,Pawn,Event->State.Revision));
			if(!bSolo && RemoteEvent) Test->TestFalse(TEXT("Remote receives repaired state"),RemoteEvent->State.bActive);
			Test->TestNull(TEXT("Completion closes the operation widget"),(bSolo?Interaction:RemoteInteraction)->GetBreakerWidget());
			Test->TestEqual(TEXT("Completion restores operator camera"),(bSolo?PC:RemotePC)->GetViewTarget(),static_cast<AActor*>(bSolo?Pawn:RemotePawn));
			GEditor->RequestEndPlayMap(); return true;
		}
		return false;
	}
private:
	void CheckGuideLight(AMD_RepairInstructionControl* Control,const FMD_RepairInstructionState& State)
	{
		if(!Test->TestNotNull(TEXT("Guide display replicated to this peer"),Control)) return;
		auto* MID=Cast<UMaterialInstanceDynamic>(Control->Body->GetMaterial(0));
		if(!Test->TestNotNull(TEXT("Original guide mesh has a dynamic circle light material"),MID)) return;
		Test->TestEqual(TEXT("Guide illumination requires an active incident and E guide use"),MID->K2_GetScalarParameterValue(TEXT("GuideActive")),State.bActive && State.bGuideRead?1.f:0.f);
		for(int32 Col=0;Col<4;++Col)
		{
			const FLinearColor UV=MID->K2_GetVectorParameterValue(*FString::Printf(TEXT("GuideTarget%d"),Col));
			const FVector2D Expected=AMD_RepairInstructionControl::GetGuideCircleUV(Col,State.TargetRows[Col]);
			Test->TestTrue(TEXT("Each column lights its exact original atlas target"),FMath::IsNearlyEqual(double(UV.R),Expected.X,1e-6) && FMath::IsNearlyEqual(double(UV.G),Expected.Y,1e-6));
		}
	}
	void Setup()
	{
		Event=World->SpawnActorDeferred<AMD_RepairInstructionController>(LoadClass<AMD_RepairInstructionController>(nullptr,TEXT("/Game/Blueprint/Facility/RepairInstruction/BP_MD_RepairInstructionController.BP_MD_RepairInstructionController_C")),FTransform());
		Event->Fault->bStartBroken=bStartBroken;
		FActorSpawnParameters S; S.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Room=World->SpawnActor<ATriggerBox>(FVector(10000,100,500),FRotator::ZeroRotator,S);
		Cast<UBoxComponent>(Room->GetCollisionComponent())->SetBoxExtent(FVector(250,400,250)); Room->SetActorEnableCollision(false);
		auto* DC=LoadClass<AActor>(nullptr,TEXT("/Game/Assets/X7/Interior/Objects/Blueprints/Door/BP_Door.BP_Door_C"));
		Door=World->SpawnActorDeferred<AActor>(DC,FTransform(FVector(11000,1000,500))); Door->Tags.Add(UMD_FacilityFaultComponent::FaultExemptActorTag); Door->bAlwaysRelevant=true;
		if(auto* P=FindFProperty<FBoolProperty>(Door->GetClass(),TEXT("bLocked"))) P->SetPropertyValue_InContainer(Door,false);
		Door->FinishSpawning(FTransform(FVector(11000,1000,500)));
		Event->InstructionDoor=Door; Event->InstructionRoom=Room;
		Guide=Control(EMD_InstructionControlKind::Guide,0,FVector(10000,0,500)); Event->Guide=Guide;
		for(UActorComponent* Component:Guide->GetComponents())
			Test->TestFalse(TEXT("Guide has no overlay screen, marker or label components"),Component->GetName()==TEXT("ScreenSurface") || Component->GetName().StartsWith(TEXT("Marker_")) || Component->GetName().StartsWith(TEXT("Label_")));
		Restart=Control(EMD_InstructionControlKind::Restart,0,FVector(10000,250,500)); Event->Restart=Restart;
		Event->bUseIntegratedKnobs=true; Event->Knobs.Empty();
		Wire=World->SpawnActor<AMD_PowerConduit>(FVector(20000,1000,500),FRotator::ZeroRotator,S);
		UnrelatedWire=World->SpawnActor<AMD_PowerConduit>(FVector(23000,1000,500),FRotator::ZeroRotator,S);
		Event->PowerConduits.Add(Wire); Event->FinishSpawning(FTransform(FVector(10000,2000,500)));
		Test->TestEqual(TEXT("Breaker owns four knob meshes"),Event->KnobMeshes.Num(),4);
		Test->TestTrue(TEXT("No separate knob actors are needed"),Event->Knobs.IsEmpty());
		Test->TestTrue(TEXT("Fixture configured"),Event->HasValidConfiguration());
		// All twenty atlas holes, not just a successful input on one generic rail.
		const double ExpectedColumns[]={27.16,9.135,-8.895,-26.92};
		const double ExpectedRows[]={25.962,12.604,-.623,-14.112,-26.942};
		for(int32 Row=0;Row<5;++Row)
		{
			Event->State.CurrentRows={Row,Row,Row,Row}; Event->RefreshKnobs();
			for(int32 Col=0;Col<4;++Col)
			{
				const FVector Surface=Event->GetKnobSlotCenter(Col,Row);
				Test->TestTrue(TEXT("Knob follows actual atlas hole center"),Surface.Equals(FVector(2.128,ExpectedColumns[Col],ExpectedRows[Row]),.08));
				const auto* Mesh=Event->KnobMeshes[Col].Get();
				const auto Bounds=Mesh->CalcBounds(Mesh->GetRelativeTransform());
				Test->TestTrue(TEXT("Compact cap fits hole width and height"),Bounds.BoxExtent.Y*2<4.5 && Bounds.BoxExtent.Z*2<6.2);
				Test->TestTrue(TEXT("Cap centered on hole and touches front face"),FMath::Abs(Bounds.Origin.Y-Surface.Y)<.02 && FMath::Abs(Bounds.Origin.Z-Surface.Z)<.02 && FMath::Abs(Bounds.Origin.X-Bounds.BoxExtent.X-Surface.X)<.02);
				if(Row<4) Test->TestTrue(TEXT("Row order moves downward"),Surface.Z>Event->GetKnobSlotCenter(Col,Row+1).Z);
				if(Col<3) Test->TestTrue(TEXT("Adjacent column interaction boxes do not overlap"),FMath::Abs(Event->KnobBounds[Col]->GetRelativeLocation().Y-Event->KnobBounds[Col+1]->GetRelativeLocation().Y)>15.);
			}
		}
		const FVector OriginalScale=Event->KnobMeshes[0]->GetRelativeScale3D();
		const FRotator OriginalRotation=Event->KnobMeshes[0]->GetRelativeRotation();
		const FVector EditedScale=OriginalScale*FVector(1.25,.8,1.1);
		const FRotator EditedRotation=OriginalRotation+FRotator(0,0,12);
		const FVector NeighborScale=Event->KnobMeshes[1]->GetRelativeScale3D();
		Event->KnobMeshes[0]->SetRelativeScale3D(EditedScale);
		Event->KnobMeshes[0]->SetRelativeRotation(EditedRotation);
		for(int32 Row=0;Row<5;++Row)
		{
			Event->State.CurrentRows={Row,Row,Row,Row}; Event->OnConstruction(Event->GetActorTransform()); Event->RefreshKnobs();
			Test->TestTrue(TEXT("Construction and row updates preserve edited scale"),Event->KnobMeshes[0]->GetRelativeScale3D().Equals(EditedScale));
			Test->TestTrue(TEXT("Construction and row updates preserve edited rotation"),Event->KnobMeshes[0]->GetRelativeRotation().Equals(EditedRotation));
			Test->TestTrue(TEXT("Editing one knob leaves neighboring scale unchanged"),Event->KnobMeshes[1]->GetRelativeScale3D().Equals(NeighborScale));
			const FVector Surface=Event->GetKnobSlotCenter(0,Row);
			const auto Bounds=Event->KnobMeshes[0]->CalcBounds(Event->KnobMeshes[0]->GetRelativeTransform());
			Test->TestTrue(TEXT("Edited nonuniform cap stays centered and touches the face"),FMath::Abs(Bounds.Origin.Y-Surface.Y)<.02 && FMath::Abs(Bounds.Origin.Z-Surface.Z)<.02 && FMath::Abs(Bounds.Origin.X-Bounds.BoxExtent.X-Surface.X)<.02);
		}
		Event->KnobMeshes[0]->SetRelativeScale3D(OriginalScale); Event->KnobMeshes[0]->SetRelativeRotation(OriginalRotation);
		Event->State.CurrentRows={0,0,0,0}; Event->RefreshKnobs();
		Test->TestTrue(TEXT("Unrelated power incident starts independently"),UnrelatedWire->FacilityRepair->TryBreak());
		Test->TestTrue(TEXT("Cut wire fault starts independently"),Wire->FacilityRepair->TryBreak());
		if (!bStartBroken) Test->TestFalse(TEXT("Cut wire does not start breaker puzzle"),Event->State.bActive);
		if (!Event->State.bActive) Test->TestTrue(TEXT("Independent breaker incident starts"),Event->StartRepairIncident());
	}
	AMD_RepairInstructionControl* Control(EMD_InstructionControlKind Kind,int32 Col,FVector Location)
	{
		const TCHAR* Path=Kind==EMD_InstructionControlKind::Guide?TEXT("/Game/Blueprint/Facility/RepairInstruction/BP_MD_RepairInstructionGuide.BP_MD_RepairInstructionGuide_C"):
			Kind==EMD_InstructionControlKind::Restart?TEXT("/Game/Blueprint/Facility/RepairInstruction/BP_MD_RepairInstructionRestart.BP_MD_RepairInstructionRestart_C"):TEXT("/Game/Blueprint/Facility/RepairInstruction/BP_MD_RepairInstructionKnob.BP_MD_RepairInstructionKnob_C");
		auto* C=World->SpawnActorDeferred<AMD_RepairInstructionControl>(LoadClass<AMD_RepairInstructionControl>(nullptr,Path),FTransform(Location));
		C->Controller=Event; C->ControlKind=Kind; C->Column=Col; C->FinishSpawning(FTransform(Location)); return C;
	}
	void Aim(AMD_Character* P,APlayerController* C,AActor* Target,int32 TargetColumn=INDEX_NONE)
	{
		const auto* Breaker=Cast<AMD_RepairInstructionController>(Target);
		const FVector Point=Breaker && Breaker->KnobBounds.IsValidIndex(TargetColumn)?Breaker->KnobBounds[TargetColumn]->GetComponentLocation():Target->GetActorLocation();
		const FVector Eye=Point+Target->GetActorForwardVector()*140;
		P->SetActorLocation(Eye); C->SetControlRotation((Point-Eye).Rotation());
		if(const auto* Camera=P->FindComponentByClass<UCameraComponent>()) P->AddActorWorldOffset(Eye-Camera->GetComponentLocation());
	}
	void Press(UMD_InteractionComponent* I,AActor* Target)
	{
		I->TickComponent(0.f,LEVELTICK_All,nullptr); Test->TestEqual(TEXT("Real aim focuses requested control"),I->GetCurrentTarget(),Target);
		if(const auto* C=Cast<AMD_RepairInstructionControl>(Target); C && C->ControlKind==EMD_InstructionControlKind::Restart)
		{
			const int32 Revision=C->Controller->State.Revision;
			I->Interact(); Test->TestEqual(TEXT("E does not restart"),C->Controller->State.Revision,Revision);
			Test->TestTrue(TEXT("Left click consumes restart input"),I->TryClickInstructionRestart());
		}
		else I->Interact();
	}
	bool DoorOpen() const { return FindFProperty<FBoolProperty>(Door->GetClass(),TEXT("bDoorOpen"))->GetPropertyValue_InContainer(Door); }
	void Wait(float D) { At=World->GetTimeSeconds(); Delay=D; }
	FAutomationTestBase* Test; bool bSolo; bool bStartBroken; bool bScreenEvents; int32 Phase=0,Column=0,SavedRevision=0;
	double Started=0.,At=0.; float Delay=0;
	TStrongObjectPtr<ULevelEditorPlaySettings> Settings;
	UWorld* World=nullptr; UWorld* Client=nullptr;
	AMD_Character* Pawn=nullptr; AMD_Character* ServerRemote=nullptr; AMD_Character* RemotePawn=nullptr;
	APlayerController* PC=nullptr; APlayerController* RemotePC=nullptr; APlayerState* RemotePS=nullptr;
	UMD_InteractionComponent* Interaction=nullptr; UMD_InteractionComponent* RemoteInteraction=nullptr;
	AMD_RepairInstructionController* Event=nullptr; AMD_RepairInstructionController* RemoteEvent=nullptr;
	AMD_RepairInstructionControl* Guide=nullptr; AMD_RepairInstructionControl* Restart=nullptr;
	AActor* Door=nullptr; ATriggerBox* Room=nullptr; AMD_PowerConduit* Wire=nullptr; AMD_PowerConduit* UnrelatedWire=nullptr;
	TArray<int32> SavedTargets,BeforeOpenRows; FMD_FacilitySnapshot SavedFault;
	bool bOpened=false,bTestedExit=false; int32 SavedOperationId=0;
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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDBreakerMouseScreenTest,"MarineDemolition.BreakerMouse.ScreenPIE",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMDBreakerMouseScreenTest::RunTest(const FString&)
{
	if(!GEditor || GEditor->PlayWorld) { AddError(TEXT("Idle validation editor required")); return false; }
	AddExpectedError(TEXT("Online Subsystem의 External UIExternal UI 모듈이 유효하지 않습니다"),EAutomationExpectedErrorFlags::Contains,1);
	ADD_LATENT_AUTOMATION_COMMAND(FInstructionPIE(this,true,false,true)); return true;
}
#endif
