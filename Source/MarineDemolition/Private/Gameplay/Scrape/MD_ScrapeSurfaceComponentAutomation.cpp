#include "Component/Scrape/MD_ScrapeSurfaceComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "World/Wall/MD_CuttableWall.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMD_GenericScrapeSurfaceComponentTest,
	"MarineDemolition.Scrape.SurfaceComponent.GenericActor",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FMD_GenericScrapeSurfaceComponentTest::RunTest(const FString& Parameters)
{
	if (!TestNotNull(TEXT("자동화 테스트에 GEngine이 존재한다."), GEngine))
	{
		return false;
	}

	const FName WorldName = MakeUniqueObjectName(
		nullptr,
		UWorld::StaticClass(),
		TEXT("MD_GenericScrapeSurfaceWorld"),
		EUniqueObjectNameOptions::GloballyUnique);
	UWorld* const TestWorld = UWorld::CreateWorld(
		EWorldType::Game,
		false,
		WorldName,
		GetTransientPackage());
	if (!TestNotNull(TEXT("일반 표면 테스트 World를 생성한다."), TestWorld))
	{
		return false;
	}

	FWorldContext& WorldContext =
		GEngine->CreateNewWorldContext(EWorldType::Game);
	TestWorld->AddToRoot();
	WorldContext.SetCurrentWorld(TestWorld);
	TestWorld->InitializeActorsForPlay(FURL());

	auto CleanupTestWorld = [&]()
	{
		GEngine->ShutdownWorldNetDriver(TestWorld);
		TestWorld->DestroyWorld(false);
		TestWorld->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(TestWorld);
		TestWorld->RemoveFromRoot();
	};

	AStaticMeshActor* const SurfaceActor =
		TestWorld->SpawnActor<AStaticMeshActor>();
	if (!TestNotNull(TEXT("철거벽이 아닌 일반 StaticMeshActor를 생성한다."), SurfaceActor))
	{
		CleanupTestWorld();
		return false;
	}
	TestFalse(TEXT("일반 표면 액터는 AMD_CuttableWall이 아니다."),
		SurfaceActor->IsA<AMD_CuttableWall>());
	SurfaceActor->SetReplicates(true);

	UStaticMesh* const CubeMesh = LoadObject<UStaticMesh>(
		nullptr,
		TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!TestNotNull(TEXT("일반 표면용 엔진 Cube 메시를 불러온다."), CubeMesh))
	{
		CleanupTestWorld();
		return false;
	}
	SurfaceActor->GetStaticMeshComponent()->SetStaticMesh(CubeMesh);

	UMD_ScrapeSurfaceComponent* const Surface =
		NewObject<UMD_ScrapeSurfaceComponent>(
			SurfaceActor,
			TEXT("ScrapeSurface"));
	SurfaceActor->AddInstanceComponent(Surface);
	Surface->RegisterComponent();
	Surface->InitializeSurface();

	TestTrue(TEXT("일반 표면 소유 액터가 네트워크 복제된다."),
		SurfaceActor->GetIsReplicated());
	TestTrue(TEXT("오염 셀을 가진 표면 컴포넌트도 복제 대상으로 등록된다."),
		Surface->GetIsReplicated());
	if (!TestTrue(TEXT("일반 액터의 표면 컴포넌트가 초기화된다."),
		Surface->IsSurfaceInitialized()))
	{
		CleanupTestWorld();
		return false;
	}
	TestEqual(
		TEXT("피격 메시에서 담당 표면 컴포넌트를 찾는다."),
		UMD_ScrapeSurfaceComponent::FindForPrimitiveComponent(
			SurfaceActor->GetStaticMeshComponent()),
		Surface);
	TestNotNull(TEXT("일반 표면도 앞면 Decal 컴포넌트를 자체 생성한다."),
		Surface->GetFrontContaminationDecal());
	TestNotNull(TEXT("일반 표면도 뒷면 Decal 컴포넌트를 자체 생성한다."),
		Surface->GetBackContaminationDecal());

	FMD_ScrapeToolSpec ToolSpec;
	ToolSpec.BladeWidthCm = 200.0f;
	ToolSpec.BladeDepthCm = 200.0f;
	ToolSpec.RemovalPerTravelCentimeter = 1.0f;

	FMD_ScrapeStroke Stroke;
	Stroke.SurfaceFace = EMD_ScrapeSurfaceFace::Front;
	Stroke.StartPositionCm = FVector2D(-40.0, 0.0);
	Stroke.EndPositionCm = FVector2D(40.0, 0.0);
	const FMD_ScrapeResult Result =
		Surface->ApplyLocalScrapeStrokeWithToolSpec(Stroke, ToolSpec);
	TestTrue(TEXT("일반 표면의 로컬 스트로크 입력이 유효하다."),
		Result.bIsInputValid);
	TestTrue(TEXT("일반 표면에서 실제 오염량이 줄어든다."),
		Result.RemovedContaminationAmount > 0.0f);

	const float CleanRatioBeforeSnapshot =
		Surface->GetContaminationCleanRatio();
	TArray<int32> CellIndices;
	TArray<float> InitialAmounts;
	TArray<float> RemainingAmounts;
	Surface->ExportContaminationDeltas(
		CellIndices,
		InitialAmounts,
		RemainingAmounts);
	TestTrue(TEXT("일반 표면의 변경된 셀이 Day 데이터로 내보내진다."),
		!CellIndices.IsEmpty());

	Surface->InitializeSurface();
	TestTrue(TEXT("재초기화하면 저장 전보다 오염이 다시 늘어난다."),
		Surface->GetContaminationCleanRatio() < CleanRatioBeforeSnapshot);
	Surface->RestoreContaminationCells(
		CellIndices,
		InitialAmounts,
		RemainingAmounts);
	TestTrue(TEXT("일반 표면의 Day 데이터가 같은 진행도로 복원된다."),
		FMath::IsNearlyEqual(
			Surface->GetContaminationCleanRatio(),
			CleanRatioBeforeSnapshot,
			0.001f));

	UStaticMeshComponent* const UnmanagedMesh =
		NewObject<UStaticMeshComponent>(SurfaceActor, TEXT("UnmanagedMesh"));
	SurfaceActor->AddInstanceComponent(UnmanagedMesh);
	UnmanagedMesh->RegisterComponent();
	TestNull(TEXT("같은 액터의 다른 메시를 표면으로 잘못 고르지 않는다."),
		UMD_ScrapeSurfaceComponent::FindForPrimitiveComponent(UnmanagedMesh));

	CleanupTestWorld();
	return true;
}

#endif
