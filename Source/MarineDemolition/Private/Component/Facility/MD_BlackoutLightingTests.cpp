#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Component/Facility/MD_FacilityFaultManagerComponent.h"
#include "Component/Facility/MD_FacilityRepairComponent.h"
#include "MD_FacilityPlayTestGameMode.h"
#include "World/Wall/MD_PowerConduit.h"
#include "World/Wall/MD_CuttableWall.h"
#include "World/Facility/MD_RepairInstruction.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SpotLightComponent.h"
#include "Engine/PointLight.h"
#include "Engine/TriggerBox.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "GameFramework/GameStateBase.h"
#include "EngineUtils.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstance.h"
#include "Editor.h"
#include "Misc/AutomationTest.h"
#include "PlayInEditorDataTypes.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "UObject/StrongObjectPtr.h"
namespace {
class FBlackoutLightingPIE final : public IAutomationLatentCommand {
public:
 FBlackoutLightingPIE(FAutomationTestBase* T,bool Solo):Test(T),bSolo(Solo) {
  Settings.Reset(DuplicateObject<ULevelEditorPlaySettings>(GetDefault<ULevelEditorPlaySettings>(),GetTransientPackage()));Settings->SetPlayNetMode(PIE_ListenServer);Settings->SetRunUnderOneProcess(true);Settings->SetPlayNumberOfClients(Solo?1:2);
  FRequestPlaySessionParams P;P.EditorPlaySettings=Settings.Get();P.GameModeOverride=AMD_FacilityPlayTestGameMode::StaticClass();P.GlobalMapOverride=TEXT("/Engine/Maps/Entry");P.bAllowOnlineSubsystem=false;Start=FPlatformTime::Seconds();GEditor->RequestPlaySession(P);
 }
 ~FBlackoutLightingPIE() override {if(GEditor && GEditor->PlayWorld)GEditor->RequestEndPlayMap();}
 bool Update() override {
  if(FPlatformTime::Seconds()-Start>90){Test->AddError(TEXT("Blackout lighting PIE timeout"));return true;}
  if(!World) {
   for(const auto& C:GEngine->GetWorldContexts())if(C.WorldType==EWorldType::PIE && C.World() && C.World()->HasBegunPlay()){if(C.World()->GetNetMode()==NM_Client)Client=C.World();else World=C.World();}
   if(!World || (!bSolo && !Client)){World=nullptr;return false;}
   Manager=World->GetGameState()->FindComponentByClass<UMD_FacilityFaultManagerComponent>();
   auto* C=LoadClass<AMD_CuttableWall>(nullptr,TEXT("/Game/Blueprint/Wall/BP_MD_CuttableWall_A.BP_MD_CuttableWall_A_C"));HostMeshActor=World->SpawnActor<AMD_CuttableWall>(C,FVector(3000,0,0),FRotator::ZeroRotator);
   if(!Test->TestNotNull(TEXT("Actual replicated mesh BP"),HostMeshActor))return true;
   Normal=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Assets/X7/Interior/Material/MI_Light_A.MI_Light_A"));Normal2=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Assets/X7/Interior/Material/MI_Light_A3.MI_Light_A3"));Dark=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Materials/Facility/MI_PowerLight_Blackout.MI_PowerLight_Blackout"));
   const auto* DarkInstance=Cast<UMaterialInstance>(Dark);const auto* Emissive=DarkInstance?DarkInstance->ScalarParameterValues.FindByPredicate([](const FScalarParameterValue& V){return V.ParameterInfo.Name==TEXT("EmissiveIntensity");}):nullptr;Test->TestTrue(TEXT("Blackout material explicitly emits no light"),Emissive && Emissive->ParameterValue==0.f);
   HostMeshActor->GetScanMesh()->SetMaterial(0,Normal);HostMeshActor->GetScanMesh()->SetMaterial(2,Normal2);
   WireA=World->SpawnActor<AMD_PowerConduit>();WireB=World->SpawnActor<AMD_PowerConduit>();
   for(auto* Wire:{WireA,WireB}){FMD_PowerLightTarget A;A.MeshComponent=HostMeshActor->GetScanMesh();A.MaterialIndex=0;A.NormalMaterial=Normal;A.BlackoutMaterial=Dark;FMD_PowerLightTarget B=A;B.MaterialIndex=2;B.NormalMaterial=Normal2;Wire->PowerLightTargets={A,B};Wire->ApplyPoweredLights();}
   for(UWorld* W:{World,Client})if(W){auto* M=W->GetGameState()->FindComponentByClass<UMD_FacilityFaultManagerComponent>();auto* On=W->SpawnActor<APointLight>();On->Tags.Add(UMD_FacilityFaultManagerComponent::BaseLightActorTag);M->RegisterBlackoutLights(On);Lamps.Add(On);auto* Off=W->SpawnActor<APointLight>();Off->GetLightComponent()->SetVisibility(false);Off->Tags.Add(UMD_FacilityFaultManagerComponent::BaseLightActorTag);M->RegisterBlackoutLights(Off);OriginallyOff.Add(Off);auto* Outside=W->SpawnActor<APointLight>();Outdoor.Add(Outside);auto* Guide=W->SpawnActor<AMD_RepairInstructionControl>();Guides.Add(Guide);}
   // A configured breaker owns a separate repeating incident, not either cut wire.
   Timed=World->SpawnActorDeferred<AMD_RepairInstructionController>(AMD_RepairInstructionController::StaticClass(),FTransform(FVector(20000,20000,500)));
   auto* DoorClass=LoadClass<AActor>(nullptr,TEXT("/Game/Assets/X7/Interior/Objects/Blueprints/Door/BP_Door.BP_Door_C"));
   auto* Door=World->SpawnActor<AActor>(DoorClass,FVector(20000,20500,500),FRotator::ZeroRotator);
   auto* Room=World->SpawnActor<ATriggerBox>(FVector(20000,21000,500),FRotator::ZeroRotator);Room->SetActorEnableCollision(false);
   Timed->InstructionDoor=Door;Timed->InstructionRoom=Room;Timed->Guide=Guides[0];Guides[0]->Controller=Timed;
   auto* Restart=World->SpawnActor<AMD_RepairInstructionControl>(FVector(20000,21000,500),FRotator::ZeroRotator);
   Restart->ControlKind=EMD_InstructionControlKind::Restart;Restart->Controller=Timed;Timed->Restart=Restart;Timed->PowerConduits={WireA};
   Timed->FinishSpawning(FTransform(FVector(20000,20000,500)));
   Test->TestTrue(TEXT("Periodic breaker fixture is configured"),Timed->HasValidConfiguration());
   Test->TestEqual(TEXT("Production cadence is ten minutes"),Timed->BlackoutIntervalSeconds,600.f);
   Test->TestTrue(TEXT("First periodic event is scheduled after ten minutes, not immediately"),Timed->GetSecondsUntilTimedBlackout()>590.f);
   Wait();return false;
  }
  if(World->GetTimeSeconds()-At<.6f)return false;
  switch(Phase++) {
   case 0:Check(false,0);Test->TestTrue(TEXT("First wire fault starts blackout"),WireA->FacilityRepair->TryBreak());break;
   case 1:Check(true,1);Test->TestFalse(TEXT("Cut wire does not start the breaker"),Timed->State.bActive);Test->TestTrue(TEXT("Second wire fault overlaps"),WireB->FacilityRepair->TryBreak());break;
   case 2:Check(true,2);WireA->DebugForceRepair(nullptr);break;
   case 3:
    Check(true,1);
    for(UWorld* W:{World,Client})if(W){auto* Late=W->SpawnActor<APointLight>();Late->Tags.Add(UMD_FacilityFaultManagerComponent::BaseLightActorTag);auto* M=W->GetGameState()->FindComponentByClass<UMD_FacilityFaultManagerComponent>();M->RegisterBlackoutLights(Late);M->RegisterBlackoutLights(Late);Test->TestFalse(TEXT("Late registration starts dark and duplicate registration retains baseline"),Late->GetLightComponent()->IsVisible());Lamps.Add(Late);}
    WireB->DebugForceRepair(nullptr);break;
   case 4:Check(false,0);Test->TestTrue(TEXT("A source can break again"),WireB->FacilityRepair->TryBreak());break;
   case 5:Check(true,1);WireB->Destroy();break;
   case 6:Check(false,0);Timed->ConfigureBlackoutSchedule(true,.4f);break;
   case 7:Check(true,1);Test->TestTrue(TEXT("Configured interval starts the breaker event"),Timed->State.bActive);TimedId=Timed->Fault->GetSnapshot().FaultId;Revision=Timed->State.Revision;break;
   case 8:Check(true,1);Test->TestEqual(TEXT("Repeated timer preserves an unrepaired incident"),Timed->Fault->GetSnapshot().FaultId,TimedId);Test->TestEqual(TEXT("Repeated timer preserves the answer"),Timed->State.Revision,Revision);Test->TestTrue(TEXT("Wire can overlap timed outage"),WireA->FacilityRepair->TryBreak());break;
   case 9:Check(true,2);Timed->ConfigureBlackoutSchedule(false,600.f);Timed->Fault->CompleteRepairFromValidatedSource(TimedId);Test->TestTrue(TEXT("Breaker repair does not repair cut wire"),WireA->FacilityRepair->IsFaultActive());break;
   case 10:Check(true,1);WireA->DebugForceRepair(nullptr);break;
   case 11:Check(false,0);WireA->FacilityRepair->TryBreak();Timed->ConfigureBlackoutSchedule(true,.4f);break;
   case 12:Check(true,2);WireA->DebugForceRepair(nullptr);Test->TestTrue(TEXT("Wire repair does not repair breaker outage"),Timed->State.bActive);break;
   case 13:Check(true,1);Timed->ConfigureBlackoutSchedule(false,600.f);Timed->Fault->CompleteRepairFromValidatedSource(Timed->Fault->GetSnapshot().FaultId);break;
   case 14:Check(false,0);GEditor->RequestEndPlayMap();return true;
  }
  Wait();return false;
 }
private:
 TObjectPtr<AMD_RepairInstructionController> Timed=nullptr;int32 TimedId=0,Revision=0;
 void Check(bool Broken,int32 Count) {
  for(auto* L:Lamps)Test->TestEqual(TEXT("Tagged interior lamp follows aggregate blackout on every peer"),L->GetLightComponent()->IsVisible(),!Broken);
  for(auto* L:OriginallyOff)Test->TestFalse(TEXT("Initially disabled lamp is never accidentally enabled"),L->GetLightComponent()->IsVisible());
  for(auto* L:Outdoor)Test->TestTrue(TEXT("Outdoor lighting remains independent"),L->GetLightComponent()->IsVisible());
  for(auto* G:Guides)Test->TestEqual(TEXT("Guide backup lamp remains available while any wire is broken"),G->AuxiliaryLight->IsVisible(),Broken);
  for(UWorld* W:{World,Client})if(W){auto* M=W->GetGameState()->FindComponentByClass<UMD_FacilityFaultManagerComponent>();Test->TestEqual(TEXT("Aggregate blackout replicated"),M->IsBlackoutActive(),Broken);int32 Matched=0;
   for(TActorIterator<AMD_CuttableWall>It(W);It;++It){++Matched;auto* Mesh=It->GetScanMesh();Test->TestEqual(TEXT("Shared slot count reflects only remaining incidents"),M->GetPowerOutageCount(Mesh,0),Count);Test->TestTrue(TEXT("Slot zero transitions and restores"),Mesh->GetMaterial(0)==(Broken?Dark:Normal));Test->TestTrue(TEXT("Different slot preserves its own normal material"),Mesh->GetMaterial(2)==(Broken?Dark:Normal2));}
   Test->TestEqual(TEXT("Each peer resolves exactly one test mesh"),Matched,1);
  }
 }
 void Wait(){At=World->GetTimeSeconds();}
 FAutomationTestBase* Test;bool bSolo;TStrongObjectPtr<ULevelEditorPlaySettings>Settings;double Start=0,At=0;int32 Phase=0;UWorld*World=nullptr;UWorld*Client=nullptr;UMD_FacilityFaultManagerComponent*Manager=nullptr;AMD_CuttableWall*HostMeshActor=nullptr;AMD_PowerConduit*WireA=nullptr;AMD_PowerConduit*WireB=nullptr;UMaterialInterface*Normal=nullptr;UMaterialInterface*Normal2=nullptr;UMaterialInterface*Dark=nullptr;TArray<APointLight*>Lamps,OriginallyOff,Outdoor;TArray<AMD_RepairInstructionControl*>Guides;
}; }
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBlackoutLightingSolo,"MarineDemolition.BlackoutLighting.SoloPIE",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBlackoutLightingSolo::RunTest(const FString&){AddExpectedError(TEXT("Online Subsystem의 External UIExternal UI 모듈이 유효하지 않습니다"),EAutomationExpectedErrorFlags::Contains,1);ADD_LATENT_AUTOMATION_COMMAND(FBlackoutLightingPIE(this,true));return true;}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBlackoutLightingPeer,"MarineDemolition.BlackoutLighting.TwoPlayerPIE",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBlackoutLightingPeer::RunTest(const FString&){AddExpectedError(TEXT("Online Subsystem의 External UIExternal UI 모듈이 유효하지 않습니다"),EAutomationExpectedErrorFlags::Contains,2);ADD_LATENT_AUTOMATION_COMMAND(FBlackoutLightingPIE(this,false));return true;}
#endif
