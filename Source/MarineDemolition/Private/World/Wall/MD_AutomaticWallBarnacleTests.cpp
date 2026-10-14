#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "World/Wall/MD_CuttableWall.h"
#include "World/Contamination/MD_Barnacle.h"
#include "World/Contamination/MD_BarnacleSpawner.h"
#include "../../Component/Facility/MD_FacilityPlayTestGameMode.h"
#include "Components/StaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "Editor.h"
#include "Misc/AutomationTest.h"
#include "PlayInEditorDataTypes.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"
namespace {
class FWallBarnaclePIE final : public IAutomationLatentCommand {
public:
 FWallBarnaclePIE(FAutomationTestBase* T,bool Solo):Test(T),bSolo(Solo) {
  Settings.Reset(DuplicateObject<ULevelEditorPlaySettings>(GetDefault<ULevelEditorPlaySettings>(),GetTransientPackage()));
  Settings->SetPlayNetMode(PIE_ListenServer);Settings->SetRunUnderOneProcess(true);Settings->SetPlayNumberOfClients(Solo?1:2);
  FRequestPlaySessionParams P;P.EditorPlaySettings=Settings.Get();P.GameModeOverride=AMD_FacilityPlayTestGameMode::StaticClass();P.GlobalMapOverride=TEXT("/Engine/Maps/Entry");P.bAllowOnlineSubsystem=false;Start=FPlatformTime::Seconds();GEditor->RequestPlaySession(P);
 }
 ~FWallBarnaclePIE() override {if(GEditor && GEditor->PlayWorld) GEditor->RequestEndPlayMap();}
 bool Update() override {
  if(FPlatformTime::Seconds()-Start>90){Test->AddError(TEXT("Automatic wall barnacle PIE timeout"));return true;}
  if(!World) {
   for(const auto& C:GEngine->GetWorldContexts()) if(C.WorldType==EWorldType::PIE && C.World() && C.World()->HasBegunPlay()){if(C.World()->GetNetMode()==NM_Client)Client=C.World();else World=C.World();}
   if(!World || (!bSolo && !Client)){World=nullptr;return false;}
   UClass* BarnacleClass=LoadClass<AMD_Barnacle>(nullptr,TEXT("/Game/Blueprint/Resource/Barnacle/BP_MD_WallBarnacle.BP_MD_WallBarnacle_C"));
   if(!Test->TestNotNull(TEXT("Wall-specific shell BP exists"),BarnacleClass))return true;
   auto* Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
   Wall=World->SpawnActor<AMD_CuttableWall>(FVector(2000,0,300),FRotator::ZeroRotator);
   Wall->GetScanMesh()->SetStaticMesh(Cube);Wall->SetActorScale3D(FVector(.2,5,3));
   FindFProperty<FClassProperty>(Wall->GetClass(),TEXT("PieceClass"))->SetObjectPropertyValue_InContainer(Wall,LoadClass<AActor>(nullptr,TEXT("/Game/Blueprint/Wall/BP_MD_WallPiece.BP_MD_WallPiece_C")));
   Other=World->SpawnActor<AMD_CuttableWall>(FVector(2050,0,300),FRotator::ZeroRotator);
   Other->GetScanMesh()->SetStaticMesh(Cube);Other->SetActorScale3D(FVector(.2,1,1));
   const FTransform Transform(FRotator::ZeroRotator,FVector(2050,0,300));
   Source=World->SpawnActorDeferred<AMD_BarnacleSpawner>(AMD_BarnacleSpawner::StaticClass(),Transform,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
   auto Cl=Source->GetClass();
   FindFProperty<FClassProperty>(Cl,TEXT("BarnacleClass"))->SetObjectPropertyValue_InContainer(Source,BarnacleClass);
   for(const FName Name:{FName(TEXT("BarnacleCount")),FName(TEXT("MaximumAliveBarnacleCount"))})FindFProperty<FIntProperty>(Cl,Name)->SetPropertyValue_InContainer(Source,1);
   for(const FName Name:{FName(TEXT("MinimumBarnacleDurability")),FName(TEXT("MaximumBarnacleDurability"))})FindFProperty<FIntProperty>(Cl,Name)->SetPropertyValue_InContainer(Source,2);
   FindFProperty<FBoolProperty>(Cl,TEXT("bAllowCuttableWallSurfaces"))->SetPropertyValue_InContainer(Source,true);
   FindFProperty<FEnumProperty>(Cl,TEXT("PlacementSeedMode"))->GetUnderlyingProperty()->SetIntPropertyValue(FindFProperty<FEnumProperty>(Cl,TEXT("PlacementSeedMode"))->ContainerPtrToValuePtr<void>(Source),(int64)EMD_BarnaclePlacementSeedMode::RandomPerNewGame);
   *FindFProperty<FStructProperty>(Cl,TEXT("ScaleRange"))->ContainerPtrToValuePtr<FVector2D>(Source)=FVector2D(.2,.2);
   FindFProperty<FFloatProperty>(Cl,TEXT("SurfaceEmbedCm"))->SetPropertyValue_InContainer(Source,.6f);
   Source->FindComponentByClass<UBoxComponent>()->SetBoxExtent(FVector(45,200,100));
   Source->AttachToActor(Wall,FAttachmentTransformRules::KeepWorldTransform);Source->FinishSpawning(Transform);
   Wait();return false;
  }
  if(World->GetTimeSeconds()-At<.8f)return false;
  switch(Phase++) {
   case 0: {
    Test->TestEqual(TEXT("Automatic BeginPlay produces exactly one owned shell"),Count(World),1);
    for(TActorIterator<AMD_Barnacle>It(World);It;++It) Barnacle=*It;
    if(!Test->TestNotNull(TEXT("Actual generated barnacle"),Barnacle))return true;
    Test->TestTrue(TEXT("Generated asset retains authored shell mesh"),Barnacle->GetScanMesh()->GetStaticMesh()==LoadObject<UStaticMesh>(nullptr,TEXT("/Game/Assets/X7/Interior/Objects/StaticMeshes/Shell_Low.Shell_Low")));
    Test->TestTrue(TEXT("Only source wall receives attachment despite nearby foreign wall"),Barnacle->GetAttachParentActor()==Wall);
    Test->TestEqual(TEXT("Configured shell scale retained"),Barnacle->GetActorScale3D(),FVector(.2));
    Test->TestEqual(TEXT("Spawner applies durability"),Barnacle->GetMaximumDurability(),2.f);
    Test->TestTrue(TEXT("Automatic attachment blocks demolition"),!Wall->CanCut());
    Test->TestEqual(TEXT("Repeated spawn cannot duplicate initial population"),Source->SpawnBarnacles(),0);
    if(Client) {Test->TestEqual(TEXT("Server population replicated once to peer"),Count(Client),1);for(TActorIterator<AMD_Barnacle>It(Client);It;++It){Test->TestNotNull(TEXT("Peer resolves attached wall"),It->GetAttachParentActor());Test->TestTrue(TEXT("Peer wall also gates demolition"),!CastChecked<AMD_CuttableWall>(It->GetAttachParentActor())->CanCut());Test->TestEqual(TEXT("Peer durability matches"),It->GetMaximumDurability(),2.f);}}
    Test->TestTrue(TEXT("Runtime seed available for persistence"),Source->TryGetRuntimePlacementSeed(Seed));Position=Barnacle->GetActorLocation();
    Test->TestTrue(TEXT("Removed index restore accepted"),Source->RestorePersistentState({0},true,Seed));
    Test->TestTrue(TEXT("Restored removal unlocks wall"),Wall->CanCut());
    Test->TestTrue(TEXT("Restore identical seed repopulates only unremoved indices"),Source->RestorePersistentState({},true,Seed));break;
   }
   case 1:
    Test->TestEqual(TEXT("Rebuild leaves no duplicated barnacles"),Count(World),1);for(TActorIterator<AMD_Barnacle>It(World);It;++It)Barnacle=*It;
    Test->TestTrue(TEXT("Saved seed regenerates same surface position"),Barnacle->GetActorLocation().Equals(Position,.01));
    Barnacle->ApplyScrapeClickDamage(1);Test->TestFalse(TEXT("Partial scraping retains demolition gate"),Wall->CanCut());
    Barnacle->ApplyScrapeClickDamage(1);Test->TestTrue(TEXT("Complete removal unlocks demolition"),Wall->CanCut());
    Test->TestTrue(TEXT("Source records removed index"),Source->GetRemovedSpawnIndices().Contains(0));
    Wall->Cut(nullptr);Test->TestTrue(TEXT("Demolition removes its source volume"),Source->IsActorBeingDestroyed());break;
   case 2:
    Test->TestEqual(TEXT("Removed population absent on server"),Count(World),0);if(Client)Test->TestEqual(TEXT("Removal reaches peer"),Count(Client),0);
    GEditor->RequestEndPlayMap();return true;
  }
  Wait();return false;
 }
private:
 int32 Count(UWorld* W){int32 N=0;for(TActorIterator<AMD_Barnacle>It(W);It;++It)++N;return N;}
 void Wait(){At=World->GetTimeSeconds();}
 FAutomationTestBase* Test;bool bSolo;TStrongObjectPtr<ULevelEditorPlaySettings>Settings;double Start=0,At=0;int32 Phase=0,Seed=0;UWorld* World=nullptr;UWorld* Client=nullptr;AMD_CuttableWall* Wall=nullptr;AMD_CuttableWall* Other=nullptr;AMD_BarnacleSpawner* Source=nullptr;AMD_Barnacle* Barnacle=nullptr;FVector Position;
}; }
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWallBarnacleSolo,"MarineDemolition.DemolitionSurface.AutomaticSoloPIE",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FWallBarnacleSolo::RunTest(const FString&){AddExpectedError(TEXT("Online Subsystem의 External UIExternal UI 모듈이 유효하지 않습니다"),EAutomationExpectedErrorFlags::Contains,1);ADD_LATENT_AUTOMATION_COMMAND(FWallBarnaclePIE(this,true));return true;}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWallBarnaclePeer,"MarineDemolition.DemolitionSurface.AutomaticTwoPlayerPIE",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FWallBarnaclePeer::RunTest(const FString&){AddExpectedError(TEXT("Online Subsystem의 External UIExternal UI 모듈이 유효하지 않습니다"),EAutomationExpectedErrorFlags::Contains,2);ADD_LATENT_AUTOMATION_COMMAND(FWallBarnaclePIE(this,false));return true;}
#endif
