#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "World/Facility/MD_FingerprintInterlock.h"
#include "World/Facility/MD_FingerprintPad.h"
#include "World/Trash/MD_TrashSorter.h"
#include "World/Trash/MD_TrashItem.h"
#include "Gameplay/Trash/MD_TrashLibrary.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PostProcessComponent.h"
#include "UObject/UnrealType.h"
#include "../../Component/Facility/MD_FacilityPlayTestGameMode.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "UObject/StrongObjectPtr.h"
#include "Character/MD_Character.h"
#include "Camera/CameraComponent.h"
#include "Component/Interaction/MD_InteractionComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/GameStateBase.h"
#include "Gameplay/Merchant/MD_MerchantSubsystem.h"
#include "Component/Facility/MD_FacilityRepairComponent.h"
#include "AbilitySystemComponent.h"
#include "GameplayTags/MD_GameplayTags.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDFingerprintBalanceTest, "MarineDemolition.Fingerprint.Balance",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMDFingerprintBalanceTest::RunTest(const FString&)
{
 const int32 Rates[] = {0,5,10,20};
 for (int32 Count=1; Count<=4; ++Count)
  TestEqual(FString::Printf(TEXT("%dp C=5 charge"),Count),AMD_FingerprintInterlock::CalculateLossPerSecond(Count),Rates[Count-1]);
 TestEqual(TEXT("No negative participant index"),AMD_FingerprintInterlock::CalculateLossPerSecond(-1),0);
 TestEqual(TEXT("Clamp at four participants"),AMD_FingerprintInterlock::CalculateLossPerSecond(100),20);
 TestEqual(TEXT("Invalid negative cost does not credit money"),AMD_FingerprintInterlock::CalculateLossPerSecond(4,-1),0);
 TestEqual(TEXT("Large cost saturates without overflow"),AMD_FingerprintInterlock::CalculateLossPerSecond(4,MAX_int32),MAX_int32);
 const auto* Defaults=GetDefault<AMD_FingerprintInterlock>();
 TestEqual(TEXT("Automatic interval minimum five minutes"),Defaults->MinFaultInterval,300.f);
 TestEqual(TEXT("Automatic interval maximum seven minutes"),Defaults->MaxFaultInterval,420.f);
 return true;
}

namespace
{
class FFingerprintPIECheck final : public IAutomationLatentCommand
{
public:
 FFingerprintPIECheck(FAutomationTestBase* InTest,bool bInSolo):Test(InTest),bSolo(bInSolo)
 {
  Settings.Reset(DuplicateObject<ULevelEditorPlaySettings>(GetDefault<ULevelEditorPlaySettings>(),GetTransientPackage()));
  Settings->SetPlayNetMode(PIE_ListenServer); Settings->SetRunUnderOneProcess(true);
  Settings->SetPlayNumberOfClients(bSolo?1:2);
  FRequestPlaySessionParams Params; Params.EditorPlaySettings=Settings.Get();
  Params.GameModeOverride=AMD_FacilityPlayTestGameMode::StaticClass();
  Params.GlobalMapOverride=TEXT("/Engine/Maps/Entry"); Params.bAllowOnlineSubsystem=false;
  Start=FPlatformTime::Seconds(); GEditor->RequestPlaySession(Params);
 }
 ~FFingerprintPIECheck() override { if(GEditor && GEditor->PlayWorld) GEditor->RequestEndPlayMap(); }
 bool Update() override
 {
  if(FPlatformTime::Seconds()-Start>100.) { Test->AddError(FString::Printf(TEXT("Fingerprint PIE timeout phase %d"),Phase)); return true; }
  if(!World)
  {
   for(const auto& Context:GEngine->GetWorldContexts())
    if(Context.WorldType==EWorldType::PIE && Context.World() && Context.World()->HasBegunPlay())
    {
     if(Context.World()->GetNetMode()!=NM_Client) World=Context.World();
     else Client=Context.World();
    }
   if(!World || (!bSolo && !Client)) { World=nullptr; return false; }
   for(TActorIterator<APlayerController> It(World);It;++It)
    if(It->IsLocalController()) { PC=*It; Character=Cast<AMD_Character>(It->GetPawn()); }
    else ServerRemote=Cast<AMD_Character>(It->GetPawn());
   if(!Character || (!bSolo && !ServerRemote)) { World=nullptr; return false; }
   Character->GetCharacterMovement()->DisableMovement();
   Interaction=Character->FindComponentByClass<UMD_InteractionComponent>();
   Merchant=World->GetGameInstance()->GetSubsystem<UMD_MerchantSubsystem>();
   if(!Test->TestNotNull(TEXT("Real player interaction"),Interaction) || !Test->TestNotNull(TEXT("Team wallet"),Merchant)) return true;
   // 지문 비용 검증을 위한 시스템 초기 잔액 지급
   Merchant->AddCredits(1000);
   if(!bSolo)
   {
    for(TActorIterator<APlayerController> It(Client);It;++It)
     if(It->IsLocalController()) { RemotePC=*It; RemoteCharacter=Cast<AMD_Character>(It->GetPawn()); }
    if(!RemoteCharacter) { World=nullptr; return false; }
    ServerRemote->GetCharacterMovement()->DisableMovement(); RemoteCharacter->GetCharacterMovement()->DisableMovement();
    RemoteInteraction=RemoteCharacter->FindComponentByClass<UMD_InteractionComponent>();
   }
   FActorSpawnParameters Spawn; Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
   Event=World->SpawnActorDeferred<AMD_FingerprintInterlock>(AMD_FingerprintInterlock::StaticClass(),FTransform());
   Event->bAutomaticFaults=false; Event->SoloWindowSeconds=1.2f;
   Item=World->SpawnActor<AMD_TrashItem>(FVector(15000,6000,500),FRotator::ZeroRotator,Spawn);
   Item->FindComponentByClass<UStaticMeshComponent>()->SetEnableGravity(false);
   for(int32 I=0;I<2;++I)
   {
    Pads[I]=World->SpawnActor<AMD_FingerprintPad>(LoadClass<AMD_FingerprintPad>(nullptr,TEXT("/Game/Blueprint/Facility/BP_MD_FingerprintPad.BP_MD_FingerprintPad_C")),FVector(10000,I*3000,500),FRotator::ZeroRotator,Spawn);
    Pads[I]->Interlock=Event; Pads[I]->PadIndex=I; Event->Pads.Add(Pads[I]);
   }
   for(int32 I=0;I<2;++I)
   {
    Sorters[I]=World->SpawnActorDeferred<AMD_TrashSorter>(LoadClass<AMD_TrashSorter>(nullptr,TEXT("/Game/Blueprint/Resource/Trash/BP_TrashSorter.BP_TrashSorter_C")),FTransform(FVector(20000,I*3000,500)));
    if(!Test->TestNotNull(TEXT("Actual sorter BP"),Sorters[I])) return true;
    *FindFProperty<FStructProperty>(Sorters[I]->GetClass(),TEXT("AcceptedCategory"))->ContainerPtrToValuePtr<FGameplayTag>(Sorters[I])=Item->GetTrashCategory_Implementation();
    Sorters[I]->bAlwaysRelevant=true; Event->LinkedSorters.Add(Sorters[I]);
    Sorters[I]->FinishSpawning(FTransform(FVector(20000,I*3000,500)));
   }
   Event->FinishSpawning(FTransform());
   Test->TestEqual(TEXT("Actual participants"),Event->GetParticipantCount(),bSolo?1:2);
   Test->TestTrue(TEXT("Configured event starts"),Event->StartFault());
   Test->TestFalse(TEXT("Duplicate event cannot allocate another incident"),Event->StartFault());
   Test->TestTrue(TEXT("Both sorters safety-locked"),Sorters[0]->IsSafetyInterlocked()&&Sorters[1]->IsSafetyInterlocked());
   Test->TestFalse(TEXT("Locked sorter rejects intake"),Sorters[0]->CanInteract_Implementation(Character));
   // 기존 지문 비용 검사를 독립 시설의 추가 차감과 분리
   Sorters[1]->FacilityRepair->bFaultCostEnabled=false;
   Test->TestTrue(TEXT("Independent sorter fault can coexist"),Sorters[1]->FacilityRepair->TryBreak());
   CreditsBefore=Merchant->GetCredits(); LossBefore=Event->TotalFaultLoss;
   Aim(Character,PC,Pads[0]);
   Next(0.7f); return false;
  }
  if(World->GetTimeSeconds()-PhaseTime<Delay) return false;
  if(bSolo) return SoloSteps();
  switch(Phase)
  {
  case 0:
   for(TActorIterator<AMD_FingerprintPad> It(Client);It;++It)
    if(It->GetFName()==Pads[1]->GetFName()) RemotePad=*It;
   for(TActorIterator<AMD_FingerprintInterlock> It(Client);It;++It)
    if(It->GetFName()==Event->GetFName()) RemoteEvent=*It;
   if(!Test->TestNotNull(TEXT("Second endpoint replicates"),RemotePad)||!Test->TestNotNull(TEXT("Controller replicates"),RemoteEvent)) return true;
   Test->TestTrue(TEXT("Client receives active fault"),RemoteEvent->IsActive());
   Test->TestTrue(TEXT("Remote global locator enabled without scanner equipment"),RemoteEvent->FaultHighlight->bEnabled);
   Test->TestTrue(TEXT("Locator has no distance bound"),RemoteEvent->FaultHighlight->bUnbound);
   Test->TestTrue(TEXT("Locator material assigned"),RemoteEvent->FaultHighlight->Settings.WeightedBlendables.Array.Num()>0);
   for(TActorIterator<AMD_FingerprintPad> It(Client);It;++It)
    if(It->Interlock==RemoteEvent)
    {
     It->SetInteractionHighlighted_Implementation(false);
     Test->TestTrue(TEXT("Both remote endpoints remain outlined after looking away"),It->PadMesh->bRenderCustomDepth);
     Test->TestEqual(TEXT("Fault stencil distinct from interaction and handheld scan"),It->PadMesh->CustomDepthStencilValue,AMD_FingerprintPad::FaultHighlightStencilValue);
    }
   // GetPlayerViewPoint uses the camera manager's per-frame cache. Aim, then wait
   // for a real frame before input, exactly as a human moving between targets.
   if(PickupStage==0) { Aim(Character,PC,Item); ++PickupStage; WaitFrame(); return false; }
   if(PickupStage==1)
   {
    Press(Interaction,Item);
    Test->TestEqual(TEXT("Trash is still collectable during shutdown"),Interaction->GetHeldActor(),static_cast<AActor*>(Item));
    Aim(Character,PC,Sorters[0]); ++PickupStage; WaitFrame(); return false;
   }
   if(PickupStage==2)
   {
    Interaction->TickComponent(0.f,LEVELTICK_All,nullptr); Interaction->Interact();
    Test->TestEqual(TEXT("Shutdown retains carried trash"),Interaction->GetHeldActor(),static_cast<AActor*>(Item));
    Test->TestEqual(TEXT("Shutdown does not pay or consume trash"),Sorters[0]->GetSortedCount(),0);
    Interaction->ReleaseHeldActor(); Item->SetActorLocation(FVector(15000,6000,500));
    Aim(Character,PC,Pads[0]); ++PickupStage; WaitFrame(); return false;
   }
   Press(Interaction,Pads[0]);
   Test->TestEqual(TEXT("Host E hold registers"),Pads[0]->ActiveScanner.Get(),static_cast<APawn*>(Character));
   Next(1.3f); break;
  case 1:
   Test->TestTrue(TEXT("Active incident deducts actual team money"),Merchant->GetCredits()<CreditsBefore);
   Test->TestEqual(TEXT("Two-player rate five per second"),Event->LossPerSecond,5);
   Test->TestEqual(TEXT("Expense matches actual wallet loss"),int64(CreditsBefore-Merchant->GetCredits()),Event->TotalFaultLoss-LossBefore);
   Interaction->InteractReleased();
   Test->TestNull(TEXT("E release clears holder"),Pads[0]->ActiveScanner.Get());
   Aim(ServerRemote,Cast<APlayerController>(ServerRemote->GetController()),Pads[1]);
   Aim(RemoteCharacter,RemotePC,RemotePad); Next(0.4f); break;
  case 2:
   Press(RemoteInteraction,RemotePad); Next(0.5f); break;
  case 3:
   Test->TestEqual(TEXT("Client E RPC registers on server"),Pads[1]->ActiveScanner.Get(),static_cast<APawn*>(ServerRemote));
   Test->TestTrue(TEXT("Non-overlapping scans do not repair"),Event->IsActive());
   Press(Interaction,Pads[0]); Next(0.5f); break;
  case 4:
   Test->TestFalse(TEXT("Two distinct held scans complete"),Event->IsActive());
   Test->TestFalse(TEXT("Client receives recovery"),RemoteEvent->IsActive());
   Test->TestFalse(TEXT("Remote locator disabled after authentication"),RemoteEvent->FaultHighlight->bEnabled);
   RemotePad->SetInteractionHighlighted_Implementation(false);
   Test->TestFalse(TEXT("Remote pad outline clears after repair and look away"),RemotePad->PadMesh->bRenderCustomDepth);
   RemotePad->SetInteractionHighlighted_Implementation(true);
   Test->TestEqual(TEXT("Healthy pad retains normal hover stencil"),RemotePad->PadMesh->CustomDepthStencilValue,IMD_Interactable::HighlightStencilValue);
   RemotePad->SetInteractionHighlighted_Implementation(false);
   Test->TestFalse(TEXT("Healthy sorter resumes"),Sorters[0]->IsOperationBlocked());
   Test->TestTrue(TEXT("Independent device fault remains"),Sorters[1]->IsOperationBlocked()&&Sorters[1]->FacilityRepair->IsFaultActive());
   Test->TestFalse(TEXT("Event removes only its own sorter lock"),Sorters[1]->IsSafetyInterlocked());
   CreditsBefore=Merchant->GetCredits(); Interaction->InteractReleased(); RemoteInteraction->InteractReleased();
   Next(1.3f); break;
  case 5:
   Test->TestEqual(TEXT("No drain after authentication"),Merchant->GetCredits(),CreditsBefore);
   Test->TestTrue(TEXT("New incident after repair"),Event->StartFault());
   Aim(Character,PC,Pads[0]); Press(Interaction,Pads[0]);
   Character->GetAbilitySystemComponent()->AddLooseGameplayTag(MDGameplayTags::State_Dead);
   Next(0.3f); break;
  case 6:
   Test->TestNull(TEXT("Death invalidates held scan"),Pads[0]->ActiveScanner.Get());
   Character->GetAbilitySystemComponent()->RemoveLooseGameplayTag(MDGameplayTags::State_Dead);
   Interaction->InteractReleased();
   Aim(Character,PC,Pads[0]); Press(Interaction,Pads[0]); PC->SetControlRotation(FRotator(0,90,0));
   Next(0.3f); break;
  case 7:
   Test->TestNull(TEXT("Look away invalidates held scan"),Pads[0]->ActiveScanner.Get());
   ServerRemote->GetPlayerState()->SetIsSpectator(true); Next(0.3f); break;
  case 8:
   Test->TestEqual(TEXT("Spectator excluded, solo charge zero"),Event->LossPerSecond,0);
   Test->TestEqual(TEXT("Spectator excluded from participant count"),Event->GetParticipantCount(),1);
   ServerRemote->GetPlayerState()->SetIsSpectator(false);
   ExtraStates.Add(World->SpawnActor<APlayerState>()); Next(0.3f); break;
  case 9:
   Test->TestEqual(TEXT("Third participant rate"),Event->LossPerSecond,10);
   ExtraStates.Add(World->SpawnActor<APlayerState>()); Next(0.3f); break;
  case 10:
   Test->TestEqual(TEXT("Fourth participant rate"),Event->LossPerSecond,20);
   Pads[1]->Destroy(); Next(0.3f); break;
  case 11:
   Test->TestFalse(TEXT("Removed endpoint releases unrepairable event"),Event->IsActive());
   Test->TestFalse(TEXT("Removed endpoint releases healthy sorter"),Sorters[0]->IsSafetyInterlocked());
   Test->TestFalse(TEXT("Invalid configuration rejects another incident"),Event->StartFault());
   Aim(Character,PC,Item); Next(0.3f); break;
  case 12:
   Press(Interaction,Item); Aim(Character,PC,Sorters[0]); Next(0.3f); break;
  case 13:
   ExpectedRefund=UMD_TrashLibrary::EvaluateSort(Item->GetTrashSortDescriptor_Implementation(),Sorters[0]->GetAcceptedCategory()).RefundCredits;
   CreditsBefore=Merchant->GetCredits(); Press(Interaction,Sorters[0]); Next(0.5f); break;
  case 14:
   Test->TestEqual(TEXT("Recovered StateTree consumes exactly once"),Sorters[0]->GetSortedCount(),1);
   Test->TestTrue(TEXT("Recovered sorter destroys sorted item"),!IsValid(Item));
   Test->TestEqual(TEXT("Recovered sorter immediately pays unchanged price"),Merchant->GetCredits(),CreditsBefore+ExpectedRefund);
   Test->AddInfo(TEXT("Actual two-player hold/release/RPC/recovery, money drain, preserved unrelated fault, blocked intake and resumed immediate sorting verified."));
   return true;
  }
  return false;
 }
private:
 bool SoloSteps()
 {
  switch(Phase)
  {
  case 0:
   Press(Interaction,Pads[0]); SavedDeadline=Event->SoloDeadline;
   Test->TestTrue(TEXT("Solo global locator enabled"),Event->FaultHighlight->bEnabled);
   for(auto* Pad : Pads)
   {
    Pad->SetInteractionHighlighted_Implementation(false);
    Test->TestTrue(TEXT("Solo both pads outlined without hover"),Pad->PadMesh->bRenderCustomDepth);
   }
   Test->TestEqual(TEXT("Solo first endpoint armed"),Event->SoloFirstPad,0);
   Interaction->InteractReleased(); Next(0.4f); break;
  case 1:
   Press(Interaction,Pads[0]); Test->TestEqual(TEXT("Repeated same pad cannot extend solo deadline"),Event->SoloDeadline,SavedDeadline);
   Interaction->InteractReleased(); Next(1.f); break;
  case 2:
   Test->TestEqual(TEXT("Solo timeout clears first endpoint"),Event->SoloFirstPad,INDEX_NONE);
   Test->TestEqual(TEXT("Solo never loses money"),Merchant->GetCredits(),CreditsBefore);
   Event->SoloWindowSeconds=4.f;
   Press(Interaction,Pads[0]); Interaction->InteractReleased();
   Aim(Character,PC,Pads[1]); Next(0.3f); break;
  case 3:
   Press(Interaction,Pads[1]);
   Test->TestFalse(TEXT("Solo opposite endpoint within window repairs"),Event->IsActive());
   Test->TestFalse(TEXT("Solo restores normal sorter"),Sorters[0]->IsOperationBlocked());
   Test->TestTrue(TEXT("Solo preserves unrelated sorter fault"),Sorters[1]->FacilityRepair->IsFaultActive());
   Interaction->InteractReleased();
   Event->MinFaultInterval=0.5f; Event->MaxFaultInterval=0.5f; Event->bAutomaticFaults=true;
   // Timer configuration is applied at BeginPlay, as for serialized placed controllers.
   AutoEvent=World->SpawnActorDeferred<AMD_FingerprintInterlock>(AMD_FingerprintInterlock::StaticClass(),FTransform());
   AutoEvent->MinFaultInterval=0.5f; AutoEvent->MaxFaultInterval=0.5f;
   AutoEvent->SoloWindowSeconds=1.f;
   for(int32 I=0;I<2;++I)
   {
    auto* Pad=World->SpawnActor<AMD_FingerprintPad>(FVector(30000,I*3000,500),FRotator::ZeroRotator);
    Pad->Interlock=AutoEvent; Pad->PadIndex=I; AutoEvent->Pads.Add(Pad);
   }
   AutoEvent->LinkedSorters.Add(Sorters[0]); AutoEvent->FinishSpawning(FTransform()); Next(0.8f); break;
  case 4:
  {
   Test->TestTrue(TEXT("Real automatic timer triggers incident"),AutoEvent->IsActive());
   Test->TestTrue(TEXT("Automatic incident locks linked sorter"),Sorters[0]->IsSafetyInterlocked());
   const int32 Id=AutoEvent->Fault->GetSnapshot().FaultId;
   AutoEvent->Fault->CompleteRepairFromValidatedSource(Id); Next(0.8f); break;
  }
  case 5:
   Test->TestTrue(TEXT("Automatic timer recurs after repair"),AutoEvent->IsActive());
   Test->TestEqual(TEXT("Recurring timer starts exactly second incident"),AutoEvent->Fault->GetSnapshot().FaultId,2);
   AutoEvent->Destroy();
   Test->TestFalse(TEXT("Controller removal releases its safety lock"),Sorters[0]->IsSafetyInterlocked());
   return true;
  }
  return false;
 }
 void Next(float Seconds=0.25f) { ++Phase; PhaseTime=World->GetTimeSeconds(); Delay=Seconds; }
 void WaitFrame() { PhaseTime=World->GetTimeSeconds(); Delay=0.3f; }
 void Aim(AMD_Character* Pawn,APlayerController* Controller,AActor* Target)
 {
  const auto* Mesh=Target->FindComponentByClass<UStaticMeshComponent>();
  const FVector Center=Target->IsA<AMD_FingerprintPad>() || !Mesh ? Target->GetActorLocation() : Mesh->Bounds.Origin;
  Pawn->SetActorLocation(Center-FVector(140,0,Pawn->BaseEyeHeight)); Controller->SetControlRotation(FRotator::ZeroRotator);
  if(const auto* Camera=Pawn->FindComponentByClass<UCameraComponent>()) Pawn->AddActorWorldOffset(Center-FVector(140,0,0)-Camera->GetComponentLocation());
 }
 void Press(UMD_InteractionComponent* Component,AActor* Target)
 {
  Component->TickComponent(0.f,LEVELTICK_All,nullptr);
  Test->TestEqual(TEXT("Target acquired through actual player trace"),Component->GetCurrentTarget(),Target);
  Component->Interact();
 }
 FAutomationTestBase* Test;
 bool bSolo; int32 Phase=-1,CreditsBefore=0; int64 LossBefore=0;
 double Start=0.,PhaseTime=0.,SavedDeadline=0.; float Delay=0.f;
 TStrongObjectPtr<ULevelEditorPlaySettings> Settings;
 UWorld* World=nullptr; UWorld* Client=nullptr;
 APlayerController* PC=nullptr; APlayerController* RemotePC=nullptr;
 AMD_Character* Character=nullptr; AMD_Character* ServerRemote=nullptr; AMD_Character* RemoteCharacter=nullptr;
 UMD_InteractionComponent* Interaction=nullptr; UMD_InteractionComponent* RemoteInteraction=nullptr;
 UMD_MerchantSubsystem* Merchant=nullptr;
 AMD_FingerprintInterlock* Event=nullptr; AMD_FingerprintInterlock* RemoteEvent=nullptr; AMD_FingerprintInterlock* AutoEvent=nullptr;
 AMD_FingerprintPad* Pads[2]={}; AMD_FingerprintPad* RemotePad=nullptr; AMD_TrashSorter* Sorters[2]={};
 AMD_TrashItem* Item=nullptr; int32 ExpectedRefund=0,PickupStage=0;
 TArray<APlayerState*> ExtraStates;
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDFingerprintMultiplayerTest,"MarineDemolition.Fingerprint.TwoPlayerPIE",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMDFingerprintMultiplayerTest::RunTest(const FString&)
{
 if(!GEditor||GEditor->PlayWorld) { AddError(TEXT("Idle validation editor required.")); return false; }
 AddExpectedError(TEXT("Online Subsystem의 External UIExternal UI 모듈이 유효하지 않습니다"),EAutomationExpectedErrorFlags::Contains,2);
 ADD_LATENT_AUTOMATION_COMMAND(FFingerprintPIECheck(this,false)); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDFingerprintSoloTest,"MarineDemolition.Fingerprint.SoloPIE",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMDFingerprintSoloTest::RunTest(const FString&)
{
 if(!GEditor||GEditor->PlayWorld) { AddError(TEXT("Idle validation editor required.")); return false; }
 AddExpectedError(TEXT("Online Subsystem의 External UIExternal UI 모듈이 유효하지 않습니다"),EAutomationExpectedErrorFlags::Contains,1);
 ADD_LATENT_AUTOMATION_COMMAND(FFingerprintPIECheck(this,true)); return true;
}
#endif
