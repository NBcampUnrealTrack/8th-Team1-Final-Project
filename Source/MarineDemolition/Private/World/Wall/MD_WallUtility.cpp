#include "World/Wall/MD_WallUtility.h"

#include "Character/MD_Character.h"
#include "Component/Scanning/MD_ScannerComponent.h"
#include "Component/Equipment/MD_EquipHotbarComponent.h"
#include "Components/BoxComponent.h"
#include "Data/Equipment/MD_ItemDefinition.h"
#include "Engine/OverlapResult.h"
#include "Gameplay/Collision/MD_CollisionChannels.h"
#include "GameplayTags/MD_GameplayTags.h"
#include "Net/UnrealNetwork.h"
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

void AMD_WallUtility::GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const
{
	TagContainer.AddTag(MDGameplayTags::Interaction_Scannable);
}

void AMD_WallUtility::OnScanned_Implementation(float Duration, AActor* Scanner)
{
	if (!FMath::IsFinite(Duration) || Duration <= 0.f || IsHidden()
		|| !IsValid(Mesh) || !Mesh->IsVisible() || !Mesh->bRenderInMainPass) return;
	// Preserve local ownership, interaction priority and the existing cleanup timer.
	HandleHostWallScanned(nullptr, Duration, Scanner);
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
	// 독립 설비는 이 경로를 끄고 벽 철거 시스템과 분리한다.
	if (bLinkHostWalls)
	{
		LinkOverlappingWalls();
	}

	// 처음부터 고장 상태로 시작 (서버에서만 상태 전이, 클라는 복제로 따라온다)
	if (bStartBroken && HasAuthority())
	{
		// 파손 지점은 레벨 배치 시 지정. 미지정이면 메시 중심 (바운드 랜덤은 ㄴ·ㄱ 파이프 허공에 뜸)
		const FTransform ActorTM = GetActorTransform();
		if (StartBreakLocalOffset.IsNearlyZero())
		{
			BreakLocation = IsValid(Mesh) ? Mesh->Bounds.Origin : GetActorLocation();
		}
		else
		{
			BreakLocation = ActorTM.TransformPosition(StartBreakLocalOffset);
		}

		const FVector WorldNormal = ActorTM.TransformVectorNoScale(StartBreakLocalNormal).GetSafeNormal();
		BreakNormal = WorldNormal.IsNearlyZero() ? GetActorForwardVector() : WorldNormal;

		// Cutter 없음 = 시작 파손. 파생 클래스는 이 값으로 감점을 건너뛴다
		SetBroken(nullptr);
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
		Wall->OnWallScanned.AddDynamic(this, &AMD_WallUtility::HandleHostWallScanned);
		LinkedWalls.Add(Wall);
	}

	// [DIAG] 겹친 벽 감지 결과 — 링크 0개면 배치/콜리전 문제
	UE_LOG(LogTemp, Warning, TEXT("[MD_Utility] %s LinkOverlappingWalls: hits=%d, linked=%d"),
		*GetName(), Overlaps.Num(), LinkedWalls.Num());
}

void AMD_WallUtility::HandleHostWallCut(AMD_CuttableWall* Wall, AActor* Cutter)
{
	// 이미 파손된 인프라는 건드리지 않는다.
	// (SetBroken이 걸러주지만, 그 전에 BreakLocation/Normal을 덮어써 VFX 기준점만 바뀌는 것을 막는다)
	if (UtilityState == EMD_UtilityState::Broken)
	{
		return;
	}

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

	ActiveScanner = Scanner;
	if (const AMD_Character* Character = Cast<AMD_Character>(Scanner))
		if (UMD_ScannerComponent* Component = Character->GetScannerComponent())
			Component->TrackScannedUtility(this);
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
	bScanHighlighted = bVisible;
	RefreshHighlight();
}

void AMD_WallUtility::RefreshHighlight()
{
	// 일반 스캔 또는 조준 중인 설비만 강조
	if (bScanHighlighted == true || bInteractionHighlighted == true)
	{
		// 스캔 색상을 우선하고 조준만 남으면 상호작용 색상 적용
		HighlightState.Apply(Mesh, bScanHighlighted == true ? ScanSilhouetteStencil : IMD_Interactable::HighlightStencilValue);
	}
	else
	{
		// 스캔과 조준이 모두 끝난 메시의 원래 렌더 상태 복원
		HighlightState.Restore();
	}
}

void AMD_WallUtility::EndScanSilhouette()
{
	GetWorldTimerManager().ClearTimer(ScanSilhouetteTimer);
	ActiveScanner.Reset();
	SetScanSilhouette(false);
}

void AMD_WallUtility::StopScanFor(AActor* Scanner)
{
	if (ActiveScanner.Get() == Scanner) EndScanSilhouette();
}

void AMD_WallUtility::EndPlay(const EEndPlayReason::Type Reason)
{
	GetWorldTimerManager().ClearTimer(ScanSilhouetteTimer);
	HighlightState.Restore();
	Super::EndPlay(Reason);
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

	// 수리 완료 시 연결된 문의 잠금 해제
	UnlockLinkedGate();

	OnRep_UtilityState();
}

void AMD_WallUtility::UnlockLinkedGate()
{
	AActor* GateActor = LinkedGateActor.Get();
	if (!IsValid(GateActor))
	{
		return;
	}

	// BP_Door의 bLocked 변수를 FBoolProperty로 직접 접근해 false 세팅
	FBoolProperty* LockedProp = CastField<FBoolProperty>(
		GateActor->GetClass()->FindPropertyByName(TEXT("bLocked")));

	if (LockedProp != nullptr)
	{
		LockedProp->SetPropertyValue_InContainer(GateActor, false);
		GateActor->ForceNetUpdate();

		UE_LOG(LogTemp, Display, TEXT("[MD_Utility] %s UnlockLinkedGate: %s.bLocked = false"),
			*GetName(), *GateActor->GetName());
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[MD_Utility] %s UnlockLinkedGate: %s has no bLocked property"),
			*GetName(), *GateActor->GetName());
	}
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

	// 도구와 소모품에 공통인 사용 능력으로 수리 대상 호환성 판정
	UMD_ItemDefinition* CurrentItem = Hotbar->GetCurrentItem();
	// 요구된 수리 능력 태그와 정확히 일치하는 아이템만 허용
	return IsValid(CurrentItem) == true && CurrentItem->GetUseAbilityTag().MatchesTagExact(RequiredRepairToolTag) == true;
}

bool AMD_WallUtility::CanInteract_Implementation(AActor* Interactor) const
{
	// 도구 입력 전용 인프라(절연테이프)는 상호작용 대상이 아니다 — 프롬프트도 뜨지 않는다
	return !bRepairRequiresToolInput
		&& UtilityState == EMD_UtilityState::Broken
		&& CanRepairWith(Interactor);
}

bool AMD_WallUtility::TryRepairWithEquippedTool(AActor* Repairer)
{
	if (!HasAuthority() || UtilityState != EMD_UtilityState::Broken || !CanRepairWith(Repairer))
	{
		return false;
	}

	// 소모형 수리 도구는 차감이 먼저다. 차감 실패(수량 0)면 수리도 하지 않는다
	if (bConsumesRepairTool)
	{
		const AMD_Character* Character = Cast<AMD_Character>(Repairer);
		UMD_EquipHotbarComponent* Hotbar = IsValid(Character) ? Character->GetHotbarComponent() : nullptr;
		// 수리 도구의 공통 수량 차감 실패 시 수리 진행 차단
		if (IsValid(Hotbar) == false || Hotbar->TryConsumeItem(Hotbar->GetCurrentItem(), 1) == false)
		{
			return false;
		}
	}

	SetRepaired(Repairer);
	return true;
}

void AMD_WallUtility::Interact_Implementation(AActor* Interactor)
{
	if (!HasAuthority())
	{
		return;
	}

	// 조준 이후 상태·장비가 바뀔 수 있으므로 실제 실행 직전에도 같은 규칙을 다시 확인한다.
	if (!CanInteract_Implementation(Interactor))
	{
		return;
	}

	TryRepairWithEquippedTool(Interactor);
}

void AMD_WallUtility::SetInteractionHighlighted_Implementation(bool bHighlighted)
{
	bInteractionHighlighted = bHighlighted;
	RefreshHighlight();
}
