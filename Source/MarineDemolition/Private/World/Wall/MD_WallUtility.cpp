#include "World/Wall/MD_WallUtility.h"

#include "Character/MD_Character.h"
#include "CollisionQueryParams.h"
#include "Component/Equipment/MD_EquipHotbarComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/Equipment/MD_ToolDefinition.h"
#include "Engine/OverlapResult.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "Gameplay/Collision/MD_CollisionChannels.h"
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
	Mesh->SetCollisionResponseToChannel(MDCollisionChannels::InteractTrace, ECR_Block);

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
	DOREPLIFETIME(AMD_WallUtility, BreakLocation);
	DOREPLIFETIME(AMD_WallUtility, BreakNormal);
}

void AMD_WallUtility::BeginPlay()
{
	Super::BeginPlay();

	// 모든 머신에서 링크한다. 컷 델리게이트는 서버에서만 방송되어 클라 구독은 무해하고,
	// 스캔 실루엣은 스캔한 클라 로컬에서 떠야 하므로 클라 파이프도 구독해야 한다.
	LinkOverlappingWalls();
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
	FHitResult CutHit;
	if (!WasCutRayThroughMesh(Cutter, CutHit))
	{
		UE_LOG(LogTemp, Warning, TEXT("[MD_Utility] %s HandleHostWallCut: 조준선이 메시를 빗나감 → 생존"),
			*GetName());
		return;
	}

	// 파손 VFX 스폰 지점 기록. SetBroken보다 먼저 채워야 같은 복제 번치로 클라에 도착한다
	if (CutHit.bBlockingHit)
	{
		BreakLocation = CutHit.ImpactPoint;
		// 분출 방향 = 절단자 쪽 수평 방향. 눈높이가 관통 지점보다 높아 그대로 쓰면 위로 솟고,
		// 표면 법선은 파이프 윗면 절단 시 위를 향하므로 둘 다 Z를 버리고 수평 성분만 쓴다
		FVector ToCutter = CutHit.TraceStart - CutHit.ImpactPoint;
		ToCutter.Z = 0.f;
		ToCutter = ToCutter.GetSafeNormal();
		BreakNormal = ToCutter.IsNearlyZero() ? FVector(CutHit.ImpactNormal) : ToCutter;
	}
	else
	{
		// 시점 재구성 실패로 보수적 파손된 경우 — 메시 중심으로 폴백
		BreakLocation = IsValid(Mesh) ? Mesh->Bounds.Origin : GetActorLocation();
		BreakNormal = GetActorUpVector();
	}

	UE_LOG(LogTemp, Warning, TEXT("[MD_Utility] %s HandleHostWallCut: 메시 관통 → 파손 (지점=%s, 법선=%s, 트레이스히트=%d)"),
		*GetName(), *BreakLocation.ToCompactString(), *BreakNormal.ToCompactString(), CutHit.bBlockingHit ? 1 : 0);
	SetBroken(Cutter);
}

bool AMD_WallUtility::WasCutRayThroughMesh(AActor* Cutter, FHitResult& OutHit) const
{
	OutHit = FHitResult();

	if (!IsValid(Mesh) || !IsValid(Cutter))
	{
		return false;
	}

	// 절단자 시점에서 조준 레이 재구성 (컷과 같은 프레임이라 시점 동일)
	const APawn* Pawn = Cast<APawn>(Cutter);
	const AController* Controller = Pawn ? Pawn->GetController() : nullptr;
	if (!Controller)
	{
		// 시점을 못 구하면 보수적으로 파손 (판정 불가 시 기존 동작 유지, OutHit은 비워 둔다)
		return true;
	}

	FVector ViewLoc;
	FRotator ViewRot;
	Controller->GetPlayerViewPoint(ViewLoc, ViewRot);
	const FVector Start = ViewLoc;
	const FVector End = ViewLoc + ViewRot.Vector() * CutRayDistance;

	// 파이프 실제 형상으로 판정 (복합 콜리전 = 렌더 지오메트리)
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PipeCutRay), /*bTraceComplex=*/true);
	const bool bHit = Mesh->LineTraceComponent(OutHit, Start, End, Params);
	// LineTraceComponent가 OutHit.bBlockingHit을 안 채우는 경우가 있어 반환값으로 보정
	OutHit.bBlockingHit = bHit;
	return bHit;
}

void AMD_WallUtility::HandleHostWallScanned(AMD_CuttableWall* Wall, float Duration, AActor* Scanner)
{
	// 실루엣은 CustomDepth 렌더 상태라 머신 로컬. 스캔한 플레이어가 로컬 조종 중인
	// 머신에서만 켜야 한다 (다른 클라·서버 호스트 화면엔 안 뜨도록).
	const APawn* ScannerPawn = Cast<APawn>(Scanner);
	if (!ScannerPawn || !ScannerPawn->IsLocallyControlled())
	{
		return;
	}

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

void AMD_WallUtility::DebugForceRepair(AActor* Repairer)
{
	SetRepaired(Repairer);
}

bool AMD_WallUtility::TryExternalRepair(AActor* Repairer, FGameplayTag RepairToolTag)
{
	if (!HasAuthority() || UtilityState != EMD_UtilityState::Broken)
	{
		return false;
	}

	// Interact 경로의 CanRepairWith와 같은 기준 — 정확 일치만 인정
	if (!RepairToolTag.MatchesTagExact(RequiredRepairToolTag))
	{
		return false;
	}

	SetRepaired(Repairer);
	return true;
}

void AMD_WallUtility::SetRepaired(AActor* Repairer)
{
	if (!HasAuthority() || UtilityState != EMD_UtilityState::Broken)
	{
		return;
	}

	UtilityState = EMD_UtilityState::Repaired;
	UE_LOG(LogTemp, Display, TEXT("[MD_Utility] %s SetRepaired: UtilityState=Repaired, Repairer=%s"),
		*GetName(), *GetNameSafe(Repairer));

	OnRepaired(Repairer);

	OnRep_UtilityState();
}

void AMD_WallUtility::OnRep_UtilityState()
{
	OnUtilityStateChangedVisual(UtilityState);
	OnUtilityStateChangedLocal(UtilityState);
}

bool AMD_WallUtility::CanRepairWith(AActor* Interactor) const
{
	if (!RequiredRepairToolTag.IsValid())
	{
		return false;
	}

	const AMD_Character* Character = Cast<AMD_Character>(Interactor);
	if (!IsValid(Character))
	{
		return false;
	}

	const UMD_EquipHotbarComponent* Hotbar = Character->GetHotbarComponent();
	if (!IsValid(Hotbar))
	{
		return false;
	}

	const UMD_ToolDefinition* CurrentTool = Hotbar->GetCurrentTool();
	return IsValid(CurrentTool)
		&& CurrentTool->ToolAbilityTag.MatchesTagExact(RequiredRepairToolTag);
}

bool AMD_WallUtility::CanInteract_Implementation(AActor* Interactor) const
{
	return UtilityState == EMD_UtilityState::Broken
		&& CanRepairWith(Interactor);
}

void AMD_WallUtility::Interact_Implementation(AActor* Interactor)
{
	if (!HasAuthority())
	{
		return;
	}

	// 조준 이후 상태·장비가 바뀔 수 있으므로 실제 실행 직전에도 같은 규칙을 다시 확인한다.
	if (CanInteract_Implementation(Interactor))
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
