#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "World/Door/MD_DoorFaultComponent.h"
#include "World/Facility/MD_RepairInstruction.h"
#include "World/Merchant/MD_Merchant.h"
#include "Component/Equipment/MD_EquipHotbarComponent.h"
#include "Component/Interaction/MD_InteractionComponent.h"
#include "Data/Equipment/MD_ToolDefinition.h"
#include "Data/Shop/MD_ShopCatalog.h"
#include "Gameplay/Merchant/MD_MerchantSubsystem.h"
#include "../../Component/Facility/MD_FacilityPlayTestGameMode.h"
#include "Character/MD_Character.h"
#include "Camera/CameraComponent.h"
#include "Camera/CameraActor.h"
#include "EnhancedPlayerInput.h"
#include "Data/Input/MD_InputConfig.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Editor.h"
#include "PlayInEditorDataTypes.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"
#include "Misc/AutomationTest.h"

namespace {
class FCrowbarPIE final : public IAutomationLatentCommand {
public:
 FCrowbarPIE(FAutomationTestBase* T,bool Solo):Test(T),bSolo(Solo) {
  Settings.Reset(DuplicateObject<ULevelEditorPlaySettings>(GetDefault<ULevelEditorPlaySettings>(),GetTransientPackage()));
  Settings->SetPlayNetMode(PIE_ListenServer);Settings->SetRunUnderOneProcess(true);Settings->SetPlayNumberOfClients(Solo?1:2);
  FRequestPlaySessionParams P;P.EditorPlaySettings=Settings.Get();P.GameModeOverride=AMD_FacilityPlayTestGameMode::StaticClass();P.GlobalMapOverride=TEXT("/Engine/Maps/Entry");P.bAllowOnlineSubsystem=false;
  Start=FPlatformTime::Seconds();GEditor->RequestPlaySession(P);
 }
 ~FCrowbarPIE() override {if(GEditor&&GEditor->PlayWorld)GEditor->RequestEndPlayMap();}
 bool Update() override {
  if(FPlatformTime::Seconds()-Start>65.){Test->AddError(FString::Printf(TEXT("Crowbar timeout phase %d"),Phase));return true;}
  if(!World) {
   for(const auto& C:GEngine->GetWorldContexts())if(C.WorldType==EWorldType::PIE&&C.World()&&C.World()->HasBegunPlay())if(C.World()->GetNetMode()==NM_Client)Client=C.World();else World=C.World();
   if(!World||(!bSolo&&!Client)){World=nullptr;return false;}
   for(TActorIterator<APlayerController>It(World);It;++It)if(It->IsLocalController())Host=Cast<AMD_Character>(It->GetPawn());else ServerRemote=Cast<AMD_Character>(It->GetPawn());
   if(!Host||(!bSolo&&!ServerRemote)){World=nullptr;return false;}
   if(Client)for(TActorIterator<APlayerController>It(Client);It;++It)if(It->IsLocalController())Remote=Cast<AMD_Character>(It->GetPawn());
   if(!bSolo&&!Remote){World=nullptr;return false;}
   Tool=LoadObject<UMD_ToolDefinition>(nullptr,TEXT("/Game/Equipment/Tools/DA_Tool_Crowbar"));
   Catalog=LoadObject<UMD_ShopCatalog>(nullptr,TEXT("/Game/Data/Shop/DA_ShopCatalog"));
   if(!Test->TestNotNull(TEXT("Actual crowbar asset"),Tool)||!Test->TestNotNull(TEXT("Actual shop catalog"),Catalog))return true;
   LoadHandle=Catalog->LoadItemDefinitions();
   for(auto* P:{Host,ServerRemote,Remote})if(P)P->GetCharacterMovement()->DisableMovement();
   for(auto* P:{Host,ServerRemote})if(P)for(int I=0;I<4;++I)P->GetHotbarComponent()->SetItemAtSlot(I,nullptr,0);
   Merchant=World->SpawnActorDeferred<AMD_Merchant>(AMD_Merchant::StaticClass(),FTransform(FVector(30000,30000,500)));
   FindFProperty<FObjectPropertyBase>(Merchant->GetClass(),TEXT("ShopCatalog"))->SetObjectPropertyValue_InContainer(Merchant,Catalog);Merchant->FinishSpawning(Merchant->GetActorTransform());
   Economy=World->GetGameInstance()->GetSubsystem<UMD_MerchantSubsystem>();Economy->AddCredits(EMD_DailyReportCategory::Other,2000);
   Door=World->SpawnActor<AActor>(LoadClass<AActor>(nullptr,TEXT("/Game/Assets/X7/Interior/Objects/Blueprints/Door/BP_Door.BP_Door_C")),FVector(20000,20000,500),FRotator::ZeroRotator);
   Black=World->SpawnActor<AActor>(LoadClass<AActor>(nullptr,TEXT("/Game/Assets/X7/Interior/Objects/Blueprints/Door/BP_Door_Black.BP_Door_Black_C")),FVector(21000,20000,500),FRotator::ZeroRotator);
   for(auto* D:{Door,Black}){D->SetReplicates(true);D->bAlwaysRelevant=true;D->ForceNetUpdate();}
   Wait();return false;
  }
  if(World->GetTimeSeconds()-At<.7f)return false;
  switch(Phase++) {
   case 0: {
    FMD_ShopItem Row;Test->TestTrue(TEXT("500 credit catalog entry exists"),Catalog->FindItemByAssetId(Tool->GetPrimaryAssetId(),Row));Test->TestEqual(TEXT("Initial price"),Row.Price,500);
    const int32 Before=Economy->GetCredits();Economy->AddCredits(EMD_DailyReportCategory::Other,-Before);
    Test->TestEqual(TEXT("Insufficient balance denies purchase"),Merchant->TryPurchaseItem(Host,Tool->GetPrimaryAssetId()),EMD_ShopPurchaseResult::InsufficientCredits);
    Test->TestEqual(TEXT("Failed purchase does not grant tool"),Host->GetHotbarComponent()->GetItemCount(Tool),0);
    Test->TestEqual(TEXT("Failed purchase does not debit"),Economy->GetCredits(),0);Economy->AddCredits(EMD_DailyReportCategory::Other,Before);
    Test->TestEqual(TEXT("Actual shop purchase grants tool"),Merchant->TryPurchaseItem(Host,Tool->GetPrimaryAssetId()),EMD_ShopPurchaseResult::Success);
    Test->TestEqual(TEXT("Purchase spends exactly 500"),Economy->GetCredits(),Before-500);
    Test->TestEqual(TEXT("Duplicate purchase while owned denied"),Merchant->TryPurchaseItem(Host,Tool->GetPrimaryAssetId()),EMD_ShopPurchaseResult::FullyStocked);
    Test->TestEqual(TEXT("Duplicate purchase preserves credits"),Economy->GetCredits(),Before-500);
    if(ServerRemote)Test->TestEqual(TEXT("Remote pawn can purchase same item independently"),Merchant->TryPurchaseItem(ServerRemote,Tool->GetPrimaryAssetId()),EMD_ShopPurchaseResult::Success);
    Host->GetHotbarComponent()->SelectSlot(0);if(ServerRemote)ServerRemote->GetHotbarComponent()->SelectSlot(0);
    Fault=Door->FindComponentByClass<UMD_DoorFaultComponent>();Test->TestNotNull(TEXT("Door adapter"),Fault);if(!Fault)return true;
    Test->TestTrue(TEXT("Empty room can be forced broken for death-recovery scenario"),Fault->DebugForceBreak());Id=Fault->GetSnapshot().FaultId;
    Aim(Host,Door,1);break;
   }
   case 1:
    CheckGrip(Host);if(Remote)CheckGrip(Remote);
    Test->TestFalse(TEXT("Stale incident refused"),Fault->TryRepairWithCrowbar(Host,Id-1));
    Test->TestFalse(TEXT("Primary tool input does not pry with the crowbar"),Host->GetHotbarComponent()->TryActivateCurrentItemAbility());
    Test->TestTrue(TEXT("Primary tool input leaves the door broken"),Fault->IsFaultActive());
    PressInteraction(Host);break;
   case 2:
    Check(Door);Test->TestFalse(TEXT("Duplicate completion rejected"),Fault->TryRepairWithCrowbar(Host,Id));
    Test->TestEqual(TEXT("Tool remains owned"),Host->GetHotbarComponent()->GetItemCount(Tool),1);
    Test->TestTrue(TEXT("Permanent tool never runs out of charge"),Host->GetHotbarComponent()->IsToolChargeAvailable(Tool));
    Test->TestTrue(TEXT("Same door can fault again"),Fault->DebugForceBreak());Aim(Host,Door,-1);break;
   case 3:PressInteraction(Host);break;
   case 4:
    Check(Door);BlackFault=Black->FindComponentByClass<UMD_DoorFaultComponent>();Test->TestNotNull(TEXT("Black door adapter"),BlackFault);if(!BlackFault)return true;
    Test->TestTrue(TEXT("Black door can break"),BlackFault->DebugForceBreak());
    Aim(Host,Black,1);
    if(Remote){AActor* CBlack=Find(Client,Black->GetFName());if(!CBlack){Test->AddError(TEXT("Black door not replicated"));return true;}Aim(ServerRemote,Black,1);Aim(Remote,CBlack,1);}
    break;
   case 5:PressInteraction(Remote?Remote:Host);break;
   case 6:
    Check(Black);Test->TestEqual(TEXT("Second tool unchanged"),(ServerRemote?ServerRemote:Host)->GetHotbarComponent()->GetItemCount(Tool),1);
    Test->TestTrue(TEXT("Fresh fault can test rejection"),Fault->DebugForceBreak());Aim(Host,Door,1);Host->GetHotbarComponent()->SetItemAtSlot(0,nullptr,0);break;
   case 7:
    Test->TestFalse(TEXT("No tool cannot repair"),Fault->TryRepairWithCrowbar(Host,Fault->GetSnapshot().FaultId));
    Test->TestTrue(TEXT("Re-equip reusable tool"),Host->GetHotbarComponent()->SetItemAtSlot(0,Tool,1));
    Host->FindComponentByClass<UMD_InteractionComponent>()->SetInteractionBlocked(true);
    Test->TestFalse(TEXT("Death/input block cannot repair"),Fault->TryRepairWithCrowbar(Host,Fault->GetSnapshot().FaultId));
    Host->FindComponentByClass<UMD_InteractionComponent>()->SetInteractionBlocked(false);
    Puzzle=World->SpawnActor<AMD_RepairInstructionController>();Puzzle->ConfigureBlackoutSchedule(false,600);Puzzle->InstructionDoor=Door;Puzzle->State.bDoorLocked=true;
    Test->TestFalse(TEXT("Puzzle-owned lock cannot be bypassed"),Fault->TryRepairWithCrowbar(Host,Fault->GetSnapshot().FaultId));Puzzle->State.bDoorLocked=false;Puzzle->InstructionDoor=nullptr;
    Host->AddActorWorldOffset(FVector(1000,0,0));Test->TestFalse(TEXT("Out of reach cannot repair"),Fault->TryRepairWithCrowbar(Host,Fault->GetSnapshot().FaultId));
    ExternalCamera=World->SpawnActor<ACameraActor>();ExternalCamera->SetOwner(Host);
    Cast<APlayerController>(Host->GetController())->SetViewTarget(ExternalCamera);break;
   case 8:
    CheckViewHidden(Host,true);if(ServerRemote)CheckViewHidden(ServerRemote,true);
    Cast<APlayerController>(Host->GetController())->SetViewTarget(Host);break;
   case 9:
    CheckViewHidden(Host,false);CheckGrip(Host);
    Host->SetLocalThirdPersonViewEnabled(true);break;
   case 10:
    CheckViewHidden(Host,true);Host->SetLocalThirdPersonViewEnabled(false);break;
   case 11:
    CheckViewHidden(Host,false);CheckGrip(Host);GEditor->RequestEndPlayMap();return true;
  }
  Wait();return false;
 }
private:
 void PressInteraction(AMD_Character* P) {
  const auto* Property=FindFProperty<FObjectPropertyBase>(P->GetClass(),TEXT("InputConfig"));
  const auto* Config=Property?Cast<UMD_InputConfig>(Property->GetObjectPropertyValue_InContainer(P)):nullptr;
  auto* Input=Cast<UEnhancedPlayerInput>(Cast<APlayerController>(P->GetController())->PlayerInput);
  if(!Test->TestNotNull(TEXT("Character interaction input config"),Config)||!Test->TestNotNull(TEXT("Enhanced interaction input"),Input))return;
  Input->InjectInputForAction(Config->InteractAction,FInputActionValue(true));
 }
 void CheckViewHidden(AMD_Character* P,bool Hidden) {
  TArray<UActorComponent*> Parts;P->GetComponents(Parts);
  for(auto* C:Parts)if(C->GetFName()==TEXT("FirstPersonMesh")||C->GetFName()==TEXT("FirstPersonToolMesh")||C->GetFName()==TEXT("FirstPersonCutterBlade"))
   Test->TestEqual(TEXT("Viewmodel hidden only outside its local first person view"),Cast<UPrimitiveComponent>(C)->bHiddenInGame,Hidden);
 }
 void CheckGrip(AMD_Character* P) {
  USkeletalMeshComponent* Arms=nullptr;UStaticMeshComponent* First=nullptr;UStaticMeshComponent* Third=nullptr;
  TArray<UActorComponent*> Parts;P->GetComponents(Parts);
  for(auto* C:Parts){if(C->GetFName()==TEXT("FirstPersonMesh"))Arms=Cast<USkeletalMeshComponent>(C);if(C->GetFName()==TEXT("FirstPersonToolMesh"))First=Cast<UStaticMeshComponent>(C);if(C->GetFName()==TEXT("ThirdPersonToolMesh"))Third=Cast<UStaticMeshComponent>(C);}
  if(!Test->TestNotNull(TEXT("First person arms"),Arms)||!Test->TestNotNull(TEXT("First person crowbar"),First)||!Test->TestNotNull(TEXT("Third person crowbar"),Third))return;
  const FVector Grip(0,0,40);const FName Socket=Third->GetAttachSocketName();const auto* Camera=P->FindComponentByClass<UCameraComponent>();
  const FVector Expected=(P->GetMesh()->GetSocketTransform(Socket,RTS_Component)*Arms->GetComponentTransform()).GetLocation();
  Test->TestTrue(TEXT("First person grip stays on the visible hand"),First->GetComponentTransform().TransformPosition(Grip).Equals(Expected,.1));
  Test->TestTrue(TEXT("Third person grip stays on the original hand"),Third->GetComponentTransform().TransformPosition(Grip).Equals(P->GetMesh()->GetSocketLocation(Socket),.1));
  Test->TestTrue(TEXT("First person arms copy the third person animation"),Arms->LeaderPoseComponent==P->GetMesh());
  Test->TestTrue(TEXT("First person arms copy the third person frame without rotation offsets"),Arms->GetComponentTransform().Equals(P->GetMesh()->GetComponentTransform(),.01));
  Test->TestTrue(TEXT("First person crowbar copies the complete third person grip and direction"),First->GetComponentTransform().Equals(Third->GetComponentTransform(),.01));
 }
 AActor* Find(UWorld* W,FName N){for(TActorIterator<AActor>It(W);It;++It)if(It->GetFName()==N)return *It;return nullptr;}
 void Aim(AMD_Character* P,AActor* Target,int Side){FVector Origin,Extent;Target->GetActorBounds(true,Origin,Extent);const FVector Eye=Origin+Target->GetActorForwardVector()*Side*140;P->SetActorLocation(Eye);P->GetController()->SetControlRotation((Origin-Eye).Rotation());if(auto* Camera=P->FindComponentByClass<UCameraComponent>())P->AddActorWorldOffset(Eye-Camera->GetComponentLocation());}
 void Check(AActor* D){for(UWorld* W:{World,Client})if(W){auto* Copy=Find(W,D->GetFName());Test->TestNotNull(TEXT("Door exists on peer"),Copy);if(Copy){Test->TestFalse(TEXT("Fault fully repaired on every peer"),Copy->FindComponentByClass<UMD_DoorFaultComponent>()->IsFaultActive());Test->TestTrue(TEXT("Door opened on every peer"),FindFProperty<FBoolProperty>(Copy->GetClass(),TEXT("bDoorOpen"))->GetPropertyValue_InContainer(Copy));}}}
 void Wait(){At=World->GetTimeSeconds();}
 FAutomationTestBase* Test;bool bSolo;double Start=0,At=0;int Phase=0,Id=0;TStrongObjectPtr<ULevelEditorPlaySettings>Settings;
 UWorld* World=nullptr;UWorld* Client=nullptr;AMD_Character* Host=nullptr;AMD_Character* ServerRemote=nullptr;AMD_Character* Remote=nullptr;
 AActor* Door=nullptr;AActor* Black=nullptr;ACameraActor* ExternalCamera=nullptr;UMD_DoorFaultComponent* Fault=nullptr;UMD_DoorFaultComponent* BlackFault=nullptr;
 UMD_ToolDefinition* Tool=nullptr;UMD_ShopCatalog* Catalog=nullptr;AMD_Merchant* Merchant=nullptr;UMD_MerchantSubsystem* Economy=nullptr;
 AMD_RepairInstructionController* Puzzle=nullptr;TSharedPtr<FStreamableHandle>LoadHandle;
}; }
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrowbarSolo,"MarineDemolition.Crowbar.SoloPIE",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCrowbarSolo::RunTest(const FString&){AddExpectedError(TEXT("Online Subsystem의 External UIExternal UI 모듈이 유효하지 않습니다"),EAutomationExpectedErrorFlags::Contains,1);ADD_LATENT_AUTOMATION_COMMAND(FCrowbarPIE(this,true));return true;}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrowbarPeer,"MarineDemolition.Crowbar.TwoPlayerPIE",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCrowbarPeer::RunTest(const FString&){AddExpectedError(TEXT("Online Subsystem의 External UIExternal UI 모듈이 유효하지 않습니다"),EAutomationExpectedErrorFlags::Contains,2);ADD_LATENT_AUTOMATION_COMMAND(FCrowbarPIE(this,false));return true;}
#endif
