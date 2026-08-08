#include "World/Wall/MD_WallUtility.h"

#include "CollisionQueryParams.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/OverlapResult.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "World/Wall/MD_CuttableWall.h"

AMD_WallUtility::AMD_WallUtility()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetReplicateMovement(false);

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Root);
	Mesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);

	LinkTrigger = CreateDefaultSubobject<UBoxComponent>(TEXT("LinkTrigger"));
	LinkTrigger->SetupAttachment(Root);
	LinkTrigger->SetBoxExtent(FVector(50.f, 50.f, 50.f));
	LinkTrigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	LinkTrigger->SetCollisionObjectType(ECC_WorldDynamic);
	LinkTrigger->SetCollisionResponseToAllChannels(ECR_Ignore);
	// 벽 감지 — CuttableWall Mesh는 Movable(WorldDynamic)이라 두 채널 모두 오버랩
	LinkTrigger->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Overlap);
	LinkTrigger->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Overlap);
	LinkTrigger->SetGenerateOverlapEvents(true);
	LinkTrigger->SetCanEverAffectNavigation(false);
}

void AMD_WallUtility::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AMD_WallUtility, UtilityState);
}

void AMD_WallUtility::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority())
	{
		LinkOverlappingWalls();
	}
}

void AMD_WallUtility::LinkOverlappingWalls()
{
	// 벽 Mesh는 GenerateOverlapEvents=false라 오버랩 추적에 안 잡힌다.
	// LinkTrigger 형상으로 능동 박스 쿼리를 돌려 그 자리의 CuttableWall을 직접 찾는다.
	TArray<FOverlapResult> Overlaps;

	FCollisionObjectQueryParams ObjectParams;
	ObjectParams.AddObjectTypesToQuery(ECC_WorldStatic);
	ObjectParams.AddObjectTypesToQuery(ECC_WorldDynamic);

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(LinkOverlappingWalls), false, this);

	GetWorld()->OverlapMultiByObjectType(
		Overlaps,
		LinkTrigger->GetComponentLocation(),
		LinkTrigger->GetComponentQuat(),
		ObjectParams,
		FCollisionShape::MakeBox(LinkTrigger->GetScaledBoxExtent()),
		QueryParams);

	for (const FOverlapResult& Result : Overlaps)
	{
		AMD_CuttableWall* Wall = Cast<AMD_CuttableWall>(Result.GetActor());
		if (!IsValid(Wall) || LinkedWalls.Contains(Wall))
		{
			continue;
		}

		Wall->OnWallDemolished.AddDynamic(this, &AMD_WallUtility::HandleHostWallCut);
		Wall->OnStrutCollapsed.AddDynamic(this, &AMD_WallUtility::HandleHostWallCut);
		Wall->OnWallScanned.AddDynamic(this, &AMD_WallUtility::HandleHostWallScanned);
		LinkedWalls.Add(Wall);
	}

	// [DIAG] 겹친 벽 감지 결과 — 링크 0개면 배치/콜리전 문제
	UE_LOG(LogTemp, Warning, TEXT("[MD_Utility] %s LinkOverlappingWalls: hits=%d, linked=%d"),
		*GetName(), Overlaps.Num(), LinkedWalls.Num());
}

void AMD_WallUtility::HandleHostWallCut(AMD_CuttableWall* Wall, AActor* Cutter)
{
	// 조준선이 이 인프라 메시를 실제로 관통했을 때만 파손한다.
	// 파이프를 피해서 자르면 벽만 사라지고 인프라는 살아남는다.
	if (!WasCutRayThroughMesh(Cutter))
	{
		UE_LOG(LogTemp, Warning, TEXT("[MD_Utility] %s HandleHostWallCut: 조준선이 메시를 빗나감 → 생존"),
			*GetName());
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("[MD_Utility] %s HandleHostWallCut: 메시 관통 → 파손"), *GetName());
	SetBroken(Cutter);
}

bool AMD_WallUtility::WasCutRayThroughMesh(AActor* Cutter) const
{
	if (!IsValid(Mesh) || !IsValid(Cutter))
	{
		return false;
	}

	// 절단자 시점에서 조준 레이 재구성 (컷과 같은 프레임이라 시점 동일)
	const APawn* Pawn = Cast<APawn>(Cutter);
	const AController* Controller = Pawn ? Pawn->GetController() : nullptr;
	if (!Controller)
	{
		// 시점을 못 구하면 보수적으로 파손 (판정 불가 시 기존 동작 유지)
		return true;
	}

	FVector ViewLoc;
	FRotator ViewRot;
	Controller->GetPlayerViewPoint(ViewLoc, ViewRot);
	const FVector Start = ViewLoc;
	const FVector End = ViewLoc + ViewRot.Vector() * CutRayDistance;

	// 파이프 실제 형상으로 판정 (복합 콜리전 = 렌더 지오메트리)
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PipeCutRay), /*bTraceComplex=*/true);
	return Mesh->LineTraceComponent(Hit, Start, End, Params);
}

void AMD_WallUtility::HandleHostWallScanned(AMD_CuttableWall* Wall, float Duration)
{
	// 스캔한 벽에 겹친 인프라 → 벽 너머 실루엣 노출 (렌더링 상태라 각 머신 로컬)
	SetScanSilhouette(true);

	// 지속시간 후 자동 해제 (재스캔 시 타이머 갱신 = 연장)
	if (Duration > 0.f && GetWorld())
	{
		GetWorld()->GetTimerManager().SetTimer(
			ScanSilhouetteTimer, this, &AMD_WallUtility::EndScanSilhouette, Duration, false);
	}
}

void AMD_WallUtility::SetScanSilhouette(bool bVisible)
{
	if (!IsValid(Mesh))
	{
		return;
	}

	if (bVisible)
	{
		// 상호작용 하이라이트(250)와 구분되는 스캔 전용 스텐실 → PP가 엑스레이로 렌더
		Mesh->SetCustomDepthStencilValue(ScanSilhouetteStencil);
	}
	Mesh->SetRenderCustomDepth(bVisible);
}

void AMD_WallUtility::EndScanSilhouette()
{
	SetScanSilhouette(false);
}

void AMD_WallUtility::SetBroken(AActor* Cutter)
{
	if (!HasAuthority() || UtilityState == EMD_UtilityState::Broken)
	{
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("[MD_Utility] %s SetBroken -> Broken"), *GetName());

	UtilityState = EMD_UtilityState::Broken;

	OnBroken(Cutter);

	// 리슨 서버 호스트는 OnRep이 안 오므로 직접 연출 반영
	OnRep_UtilityState();
}

void AMD_WallUtility::SetRepaired(AActor* Repairer)
{
	if (!HasAuthority() || UtilityState != EMD_UtilityState::Broken)
	{
		return;
	}

	UtilityState = EMD_UtilityState::Repaired;

	OnRepaired(Repairer);

	OnRep_UtilityState();
}

void AMD_WallUtility::OnRep_UtilityState()
{
	OnUtilityStateChangedVisual(UtilityState);
}

bool AMD_WallUtility::CanRepairWith(AActor* Interactor) const
{
	// TODO: Interactor의 장착 도구가 RequiredRepairToolTag와 일치하는지 확인
	return true;
}

void AMD_WallUtility::Interact_Implementation(AActor* Interactor)
{
	if (!HasAuthority())
	{
		return;
	}

	// 파손 상태에서 올바른 수리 도구를 든 경우에만 수리
	if (UtilityState == EMD_UtilityState::Broken && CanRepairWith(Interactor))
	{
		SetRepaired(Interactor);
	}
}

void AMD_WallUtility::SetInteractionHighlighted_Implementation(bool bHighlighted)
{
	if (!IsValid(Mesh))
	{
		return;
	}

	Mesh->SetCustomDepthStencilValue(IMD_Interactable::HighlightStencilValue);
	Mesh->SetRenderCustomDepth(bHighlighted);
}
