#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "World/Wall/MD_CuttableWall.h"
#include "World/Wall/MD_WallPiece.h"
#include "World/Contamination/MD_Barnacle.h"
#include "World/Contamination/MD_BarnacleItem.h"
#include "World/Contamination/MD_SurfaceStain.h"
#include "Gameplay/Contamination/MD_SurfaceStainDefinition.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "UObject/UnrealType.h"

namespace
{
struct FDemolitionSurfaceFixture
{
	UWorld* World = nullptr;
	AMD_CuttableWall* Wall = nullptr;
	FDemolitionSurfaceFixture()
	{
		const UWorld::InitializationValues IV = UWorld::InitializationValues().AllowAudioPlayback(false)
			.CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
		const FName Name(*FString::Printf(TEXT("DemolitionSurface_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits)));
		World = UWorld::CreateWorld(EWorldType::EditorPreview, false, Name, nullptr, true, ERHIFeatureLevel::Num, &IV);
		GEngine->CreateNewWorldContext(EWorldType::EditorPreview).SetCurrentWorld(World);
		Wall = World->SpawnActor<AMD_CuttableWall>();
		Wall->GetScanMesh()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
		UClass* PieceClass = LoadClass<AMD_WallPiece>(nullptr, TEXT("/Game/Blueprint/Wall/BP_MD_WallPiece.BP_MD_WallPiece_C"));
		FindFProperty<FClassProperty>(Wall->GetClass(), TEXT("PieceClass"))->SetObjectPropertyValue_InContainer(Wall, PieceClass);
	}
	~FDemolitionSurfaceFixture() { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); }
	AMD_SurfaceStain* AddStain(int32 Initial, int32 Remaining)
	{
		auto* Stain = World->SpawnActor<AMD_SurfaceStain>();
		auto* Definition = LoadObject<UMD_SurfaceStainDefinition>(nullptr, TEXT("/Game/Contamination/Slime/Definitions/DA_Slime_A.DA_Slime_A"));
		Stain->ConfigurePlacement(Definition, Initial);
		Stain->AttachToActor(Wall, FAttachmentTransformRules::KeepWorldTransform);
		Stain->RestoreSize(Remaining);
		return Stain;
	}
	int32 DirtyPieces(int32& Total) const
	{
		int32 Dirty = 0; Total = 0;
		for (TActorIterator<AMD_WallPiece> It(World); It; ++It) { ++Total; if (It->bContaminated) ++Dirty; }
		return Dirty;
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDDemolitionBarnacleGate, "MarineDemolition.DemolitionSurface.BarnacleGate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMDDemolitionBarnacleGate::RunTest(const FString&)
{
	FDemolitionSurfaceFixture F;
	TestTrue(TEXT("Clean host can be cut"), F.Wall->CanCut());
	auto* B1 = F.World->SpawnActor<AMD_Barnacle>();
	auto* B2 = F.World->SpawnActor<AMD_Barnacle>();
	UClass* Drop = LoadClass<AMD_BarnacleItem>(nullptr, TEXT("/Game/Blueprint/Resource/Barnacle/BP_MD_BarnacleItem.BP_MD_BarnacleItem_C"));
	for (AMD_Barnacle* Barnacle : {B1, B2})
		FindFProperty<FClassProperty>(Barnacle->GetClass(), TEXT("BarnacleItemClass"))->SetObjectPropertyValue_InContainer(Barnacle, Drop);
	B1->AttachToActor(F.Wall, FAttachmentTransformRules::KeepWorldTransform);
	B2->AttachToActor(F.Wall, FAttachmentTransformRules::KeepWorldTransform);
	TestFalse(TEXT("Attached barnacles block cutting"), F.Wall->CanCut());
	F.Wall->AddCutProgress(100.f, nullptr);
	TestEqual(TEXT("Blocked cutting does not accumulate progress"), F.Wall->GetCutProgressRatio(), 0.f);
	F.Wall->Cut(nullptr);
	TestFalse(TEXT("Direct Cut cannot bypass barnacles"), F.Wall->IsHidden());
	B1->ApplyScrapeClickDamage(1.f);
	TestFalse(TEXT("Removing only one barnacle is insufficient"), F.Wall->CanCut());
	B2->ApplyScrapeClickDamage(1.f);
	TestTrue(TEXT("All removed unlocks cutting"), F.Wall->CanCut());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDDemolitionSlimeRatio, "MarineDemolition.DemolitionSurface.SlimeRatio",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMDDemolitionSlimeRatio::RunTest(const FString&)
{
	FDemolitionSurfaceFixture F;
	auto* A = F.AddStain(300, 60); auto* B = F.AddStain(100, 100);
	TestTrue(TEXT("Slime alone does not block cutting"), F.Wall->CanCut());
	TestEqual(TEXT("Size-weighted remaining ratio"), F.Wall->GetRemainingContaminationRatio(), .4f);
	F.Wall->Cut(nullptr);
	int32 Total = 0;
	TestEqual(TEXT("40 percent dirty creates four dirty pieces"), F.DirtyPieces(Total), 4);
	TestEqual(TEXT("Original ten-piece yield retained"), Total, 10);
	TestTrue(TEXT("Transferred slime does not float after demolition"), A->IsActorBeingDestroyed() && B->IsActorBeingDestroyed());
	TestFalse(TEXT("Demolished wall cannot cut twice"), F.Wall->CanCut());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDDemolitionCleanAndFull, "MarineDemolition.DemolitionSurface.CleanAndFull",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMDDemolitionCleanAndFull::RunTest(const FString&)
{
	for (const int32 Remaining : {0, 1, 150, 300})
	{
		FDemolitionSurfaceFixture F; F.AddStain(300, Remaining); F.Wall->Cut(nullptr); int32 Total = 0;
		TestEqual(TEXT("Remaining slime maps to a bounded dirty-piece count"), F.DirtyPieces(Total), FMath::CeilToInt(Remaining / 300.f * 10));
		TestEqual(TEXT("All cases retain ten pieces"), Total, 10);
	}
	FDemolitionSurfaceFixture Clean; Clean.Wall->Cut(nullptr); int32 Total = 0;
	TestEqual(TEXT("Walls without slime remain clean"), Clean.DirtyPieces(Total), 0);
	return true;
}
#endif
