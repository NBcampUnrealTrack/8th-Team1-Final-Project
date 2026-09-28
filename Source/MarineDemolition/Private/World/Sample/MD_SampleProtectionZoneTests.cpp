#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "World/Sample/MD_SampleItem.h"
#include "World/Sample/MD_SampleProtectionZone.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDSampleProtectionZoneTest, "MarineDemolition.Sample.ProtectionZone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMDSampleProtectionZoneTest::RunTest(const FString& Parameters)
{
	const UWorld::InitializationValues Init = UWorld::InitializationValues().AllowAudioPlayback(false)
		.CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!World) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	World->SetGameInstance(NewObject<UGameInstance>(GEngine));
	FURL URL; URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
	if (!World->SetGameMode(URL)) return false;
	World->InitializeActorsForPlay(URL); World->BeginPlay();
	AMD_SampleProtectionZone* Zone = World->SpawnActor<AMD_SampleProtectionZone>();
	AMD_SampleItem* Sample = World->SpawnActor<AMD_SampleItem>();
	if (!Zone || !Sample) return false;
	Sample->SpawnProtectionDuration = 0.f;
	UStaticMeshComponent* Mesh = Sample->FindComponentByClass<UStaticMeshComponent>();
	Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
	Mesh->SetSimulatePhysics(true);
	TestTrue(TEXT("Protected without waiting for overlap/tick"), AMD_SampleProtectionZone::ProtectsActor(Sample));
	UGameplayStatics::ApplyDamage(Sample, 10.f, nullptr, nullptr, nullptr);
	TestFalse(TEXT("Zone suppresses damage"), Sample->bDamaged);
	const FVector Impulse(Mesh->GetMass() * 1000.f, 0, 0);
	TestTrue(TEXT("Valid physics test mass"), Mesh->GetMass() > 0);
	Mesh->OnComponentHit.Broadcast(Mesh, Zone, Zone->ProtectionBounds, Impulse, FHitResult());
	TestFalse(TEXT("Lethal collision inside zone does not destroy sample"), Sample->IsActorBeingDestroyed());
	TestFalse(TEXT("Lethal collision inside zone does not damage sample"), Sample->bDamaged);
	Zone->SetActorRotation(FRotator(0,90,0));
	Zone->SetActorScale3D(FVector(2,1,1));
	TestTrue(TEXT("Rotated scaled zone inside"), Zone->ContainsPosition(FVector(0,590,0)));
	TestFalse(TEXT("Rotated scaled zone outside"), Zone->ContainsPosition(FVector(210,0,0)));
	Zone->bProtectionEnabled = false;
	TestFalse(TEXT("Disabled zone offers no protection"), AMD_SampleProtectionZone::ProtectsActor(Sample));
	Zone->bProtectionEnabled = true;
	Sample->SetActorLocation(FVector(2000,0,0), false, nullptr, ETeleportType::TeleportPhysics);
	TestFalse(TEXT("Protection ends immediately outside"), AMD_SampleProtectionZone::ProtectsActor(Sample));
	Mesh->OnComponentHit.Broadcast(Mesh, Zone, Zone->ProtectionBounds, Impulse, FHitResult());
	TestTrue(TEXT("Lethal collision outside retains destruction"), Sample->IsActorBeingDestroyed());
	return true;
}
#endif
