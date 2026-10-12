#include "Component/Equipment/MD_FoamGunComponent.h"

#include "Audio/MD_AudioRelayComponent.h"
#include "Character/MD_Character.h"
#include "Component/Equipment/MD_EquipHotbarComponent.h"
#include "Component/Interaction/MD_InteractionComponent.h"
#include "Data/Equipment/MD_ToolDefinition.h"
#include "GameFramework/PlayerController.h"
#include "GameplayTags/MD_GameplayTags.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "World/Foam/MD_FoamProjectile.h"

UMD_FoamGunComponent::UMD_FoamGunComponent()
{
	// 프리뷰 갱신용. 소유 클라이언트가 아니면 틱 초입에서 빠진다
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);
}

void UMD_FoamGunComponent::BeginPlay()
{
	// 상위 컴포넌트 시작 처리
	Super::BeginPlay();
	// 1인칭 도구 메시 배치는 캐릭터 Tick에서 갱신되므로, 총구 소켓을 읽는 프리뷰 틱은 그 뒤에 돈다
	AddTickPrerequisiteActor(GetOwner());
	// 서버 변경과 클라이언트 복제를 함께 수신할 Hotbar 조회
	UMD_EquipHotbarComponent* Hotbar = GetHotbar();
	// 유효한 보관 컴포넌트의 공통 변경 알림에 연결
	if (IsValid(Hotbar) == true)
	{
		// 기존 폼건 UI에 동일한 충전 상태 변경 전달
		Hotbar->OnHotbarChanged.AddUniqueDynamic(this, &UMD_FoamGunComponent::HandleHotbarChanged);
	}
}

UMD_EquipHotbarComponent* UMD_FoamGunComponent::GetHotbar() const
{
	// 유효한 소유자의 공통 충전 저장소 조회
	return IsValid(GetOwner()) == true ? GetOwner()->FindComponentByClass<UMD_EquipHotbarComponent>() : nullptr;
}

UMD_ToolDefinition* UMD_FoamGunComponent::GetEquippedFoamTool() const
{
	// 현재 장착한 도구를 조회할 Hotbar 확인
	UMD_EquipHotbarComponent* Hotbar = GetHotbar();
	// 선택 슬롯의 도구 정의 조회
	UMD_ToolDefinition* Tool = IsValid(Hotbar) == true ? Hotbar->GetCurrentTool() : nullptr;
	// 폼건 사용 능력과 정확히 일치하는 도구만 허용
	return IsValid(Tool) == true && Tool->ToolAbilityTag.MatchesTagExact(MDGameplayTags::Ability_UtilityRepair_Foam) == true ? Tool : nullptr;
}

float UMD_FoamGunComponent::GetFoamCharge() const
{
	// 복제되는 실제 보관 잔량의 저장소 조회
	UMD_EquipHotbarComponent* Hotbar = GetHotbar();
	// 장착 폼건의 공통 잔량 또는 미보유 상태의 영 반환
	return IsValid(Hotbar) == true ? Hotbar->GetToolCharge(GetEquippedFoamTool()) : 0.0f;
}

float UMD_FoamGunComponent::GetMaxCharge()
{
	// 폼건 정의에 지정된 최대 충전량 조회
	UMD_ToolDefinition* Tool = GetEquippedFoamTool();
	// 장착 폼건이 없으면 충전 기능 미사용 처리
	return IsValid(Tool) == true ? Tool->MaxCharge : 0.0f;
}

float UMD_FoamGunComponent::GetShotCost() const
{
	// 구매 시 확정한 비용을 보관하는 Hotbar 조회
	UMD_EquipHotbarComponent* Hotbar = GetHotbar();
	// 현재 정의의 변경과 무관하게 지급된 사용 비용 반환
	return IsValid(Hotbar) == true ? Hotbar->GetToolCostPerOnce(GetEquippedFoamTool()) : 0.0f;
}

bool UMD_FoamGunComponent::IsChargeRequired() const
{
	// 충전 사용 여부를 결정하는 장착 폼건 정의 조회
	UMD_ToolDefinition* Tool = GetEquippedFoamTool();
	// 양의 최대 충전량이 설정된 폼건만 충전 요구
	return IsValid(Tool) == true && Tool->MaxCharge > 0.0f;
}

bool UMD_FoamGunComponent::HasChargeForShot() const
{
	// 실제 보유 도구와 구매 비용을 함께 검사할 Hotbar 조회
	UMD_EquipHotbarComponent* Hotbar = GetHotbar();
	// 최대값 영의 무차감 사용을 포함한 공통 조건 검사
	return IsValid(Hotbar) == true && Hotbar->IsToolChargeAvailable(GetEquippedFoamTool()) == true;
}

bool UMD_FoamGunComponent::IsChargeFull() const
{
	// 최대 충전량이 정의된 장착 폼건 조회
	UMD_ToolDefinition* Tool = GetEquippedFoamTool();
	// 미장착 또는 비충전 도구와 만충 상태의 추가 보충 차단
	return IsValid(Tool) == false || Tool->MaxCharge <= 0.0f || GetFoamCharge() >= Tool->MaxCharge;
}

bool UMD_FoamGunComponent::TryConsumeShotCharge()
{
	// 서버 차감 및 UI 변경 알림을 처리할 공통 Hotbar 조회
	UMD_EquipHotbarComponent* Hotbar = GetHotbar();
	// 장착 폼건의 구매 비용을 공통 잔량에서 차감
	return IsValid(Hotbar) == true && Hotbar->ConsumeToolCharge(GetEquippedFoamTool()) == true;
}

void UMD_FoamGunComponent::AddCharge(float Amount)
{
	// BP의 보충 요청을 전달할 보유 슬롯 저장소 조회
	UMD_EquipHotbarComponent* Hotbar = GetHotbar();
	// Hotbar가 있는 대상만 서버 충전 경로로 전달
	if (IsValid(Hotbar) == true)
	{
		// 최대 충전량 상한 및 보유 여부와 권한을 공통 API에서 검사
		Hotbar->AddToolCharge(GetEquippedFoamTool(), Amount);
	}
}

void UMD_FoamGunComponent::HandleHotbarChanged()
{
	// 선택 및 잔량 변경을 기존 폼건 UI 이벤트에 전달
	OnFoamChargeChanged.Broadcast(GetFoamCharge());
}

void UMD_FoamGunComponent::GetViewPoint(FVector& OutLocation, FVector& OutDirection) const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	if (!Pawn)
	{
		OutLocation = GetOwner() ? GetOwner()->GetActorLocation() : FVector::ZeroVector;
		OutDirection = FVector::ForwardVector;
		return;
	}

	// 로컬 플레이어는 실제 카메라 시점을 써야 화면 중앙 조준선과 정확히 맞는다
	if (const APlayerController* PlayerController = Cast<APlayerController>(Pawn->GetController());
		PlayerController && PlayerController->IsLocalController())
	{
		FRotator ViewRotation;
		PlayerController->GetPlayerViewPoint(OutLocation, ViewRotation);
		OutDirection = ViewRotation.Vector();
		return;
	}

	// 서버·타인: BaseAimRotation은 복제되므로 같은 조준 방향이 나온다
	OutLocation = Pawn->GetPawnViewLocation();
	OutDirection = Pawn->GetBaseAimRotation().Vector();
}

FVector UMD_FoamGunComponent::ComputeAimTarget(const FVector& ViewLocation, const FVector& ViewDirection) const
{
	const FVector TraceEnd = ViewLocation + ViewDirection * AimTraceRange;
	const UWorld* World = GetWorld();
	if (!World)
	{
		return TraceEnd;
	}

	FCollisionQueryParams Params(SCENE_QUERY_STAT(MD_FoamGunAim), /*bTraceComplex=*/false, GetOwner());
	FHitResult Hit;
	if (World->LineTraceSingleByChannel(Hit, ViewLocation, TraceEnd, ECC_Visibility, Params))
	{
		return Hit.ImpactPoint;
	}
	return TraceEnd;
}

void UMD_FoamGunComponent::ComputeLaunch(FVector& OutOrigin, FVector& OutDirection) const
{
	FVector ViewLocation;
	FVector ViewDirection;
	GetViewPoint(ViewLocation, ViewDirection);

	// 원점 = 보이는 도구 메시의 총구 소켓. 소켓이 없으면 옛 시점 기준으로 폴백
	const AMD_Character* Character = Cast<AMD_Character>(GetOwner());
	if (!Character || !Character->TryGetVisibleToolSocketLocation(MuzzleSocketName, OutOrigin))
	{
		OutOrigin = ViewLocation + ViewDirection * MuzzleForwardOffset;
		OutDirection = ViewDirection;
		return;
	}

	// 방향 = 총구 → 카메라 조준점. 조준점이 총구 뒤(벽에 바짝 붙은 경우)면 시점 방향 그대로
	const FVector AimTarget = ComputeAimTarget(ViewLocation, ViewDirection);
	const FVector ToTarget = AimTarget - OutOrigin;
	if (FVector::DotProduct(ToTarget, ViewDirection) <= UE_KINDA_SMALL_NUMBER)
	{
		OutDirection = ViewDirection;
		return;
	}
	OutDirection = ToTarget.GetSafeNormal(UE_KINDA_SMALL_NUMBER, ViewDirection);
}

void UMD_FoamGunComponent::ServerFire_Implementation(FVector_NetQuantize Origin, FVector_NetQuantizeNormal Direction,
                                                     FGameplayTag FireSoundTag)
{
	// 발사 요청을 보낸 소유 캐릭터와 서버 월드 조회
	AMD_Character* Character = Cast<AMD_Character>(GetOwner());
	// 요청 도착 시점의 권한과 게임 진행 상태를 확인할 월드 조회
	UWorld* World = GetWorld();
	// 사망과 빙의 해제 및 운반 상태를 재검증할 상호작용 저장소 조회
	UMD_InteractionComponent* Interaction = IsValid(Character) == true ? Character->FindComponentByClass<UMD_InteractionComponent>() : nullptr;
	// 유효한 캐릭터와 월드의 서버 권한 검사
	if (IsValid(Character) == false || IsValid(World) == false || Character->HasAuthority() == false)
	{
		// 무효한 소유자와 비권한 요청의 충전 차감 및 생성 차단
		return;
	}

	// 서버 게임 월드와 발사체 설정의 실행 조건 검사
	if (World->IsGameWorld() == false || World->GetNetMode() == NM_Client || IsValid(ProjectileClass.Get()) == false)
	{
		// 게임 진행 외 호출과 발사체 설정 누락의 실행 차단
		return;
	}

	// 요청 도착 시점의 상호작용 가능 여부와 운반 상태 검사
	if (IsValid(Interaction) == false || Interaction->CanOwnerInteract() == false || Interaction->IsHolding() == true)
	{
		// 도착 전에 무효화된 요청의 충전 차감과 발사 효과 차단
		return;
	}

	// 클라이언트 값은 서버 시점 기준 허용 범위 안일 때만 신뢰. 벗어나면 서버 계산값(3인칭 소켓)으로 대체
	FVector ViewLocation;
	FVector ViewDirection;
	GetViewPoint(ViewLocation, ViewDirection);
	const FVector ClientDirection = FVector(Direction).GetSafeNormal(UE_KINDA_SMALL_NUMBER, ViewDirection);
	const bool bOriginTrusted = FVector::DistSquared(Origin, ViewLocation) <= FMath::Square(MaxClientOriginDistance);
	const bool bDirectionTrusted = FVector::DotProduct(ClientDirection, ViewDirection)
		>= FMath::Cos(FMath::DegreesToRadians(MaxClientAimAngle));

	FVector LaunchOrigin = Origin;
	FVector LaunchDirection = ClientDirection;
	if (!bOriginTrusted || !bDirectionTrusted)
	{
		ComputeLaunch(LaunchOrigin, LaunchDirection);
	}

	// 클라이언트 총구와 서버 대체 총구에 동일하게 적용할 차폐 판정 결과
	FHitResult OriginObstruction;
	// 소유 캐릭터를 제외한 실제 표면으로 시점과 발사 원점 사이의 가림 검사
	FCollisionQueryParams OriginQuery(SCENE_QUERY_STAT(MDFoamLaunchOrigin), true, Character);
	// 허용 거리 안에서도 벽 뒤에 지정된 발사 원점의 요청 차단
	if (World->LineTraceSingleByChannel(OriginObstruction, ViewLocation, LaunchOrigin, ECC_Visibility, OriginQuery) == true)
	{
		// 차폐된 원점에서의 발사체 생성과 충전 차감 방지
		return;
	}

	// 충전 소모는 서버 권위. 부족하면 스폰·발사음 없음 (클라이언트는 이미 헛방 피드백을 냈다)
	if (!TryConsumeShotCharge())
	{
		return;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = Character;
	SpawnParams.Instigator = Character;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	if (AMD_FoamProjectile* Projectile = World->SpawnActor<AMD_FoamProjectile>(
		ProjectileClass, LaunchOrigin, LaunchDirection.Rotation(), SpawnParams))
	{
		// 탄도 정본 주입 — 궤적 프리뷰와 같은 수치라 예측선대로 날아간다
		Projectile->InitLaunch(LaunchDirection * LaunchSpeed, ProjectileGravityScale);
	}

	if (FireSoundTag.IsValid())
	{
		if (UMD_AudioRelayComponent* Relay = Character->GetAudioRelayComponent())
		{
			Relay->PlayAtOwnerLocationForAll(FireSoundTag);
		}
	}
}

bool UMD_FoamGunComponent::ShouldShowTrajectory() const
{
	// RMB 조준 중에만 궤적을 보여준다
	if (!bAimHeld)
	{
		return false;
	}

	const AMD_Character* Character = Cast<AMD_Character>(GetOwner());
	if (!Character || !Character->IsLocallyControlled() || !TrajectorySystem)
	{
		return false;
	}

	const UMD_EquipHotbarComponent* Hotbar = Character->GetHotbarComponent();
	const UMD_ToolDefinition* Tool = Hotbar ? Hotbar->GetCurrentTool() : nullptr;
	return Tool && Tool->ToolAbilityTag.MatchesTagExact(MDGameplayTags::Ability_UtilityRepair_Foam);
}

void UMD_FoamGunComponent::TickComponent(float DeltaTime, ELevelTick TickType,
                                         FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!ShouldShowTrajectory())
	{
		DestroyTrajectoryPreview();
		return;
	}

	UpdateTrajectoryPreview();
}

void UMD_FoamGunComponent::UpdateTrajectoryPreview()
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	if (!TrajectoryPreview)
	{
		TrajectoryPreview = UNiagaraFunctionLibrary::SpawnSystemAttached(
			TrajectorySystem, Owner->GetRootComponent(), NAME_None,
			FVector::ZeroVector, FRotator::ZeroRotator,
			EAttachLocation::KeepRelativeOffset, /*bAutoDestroy=*/false);
		if (!TrajectoryPreview)
		{
			return;
		}
		// 조준 UI는 본인 화면에만 보인다
		TrajectoryPreview->SetOnlyOwnerSee(true);
	}

	FVector Origin;
	FVector AimDirection;
	ComputeLaunch(Origin, AimDirection);
	const FVector LaunchVelocity = AimDirection * LaunchSpeed;
	const float GravityZ = GetWorld()->GetGravityZ() * ProjectileGravityScale;

	// 실발사체와 같은 수치로 예측해야 예측선 = 실제 탄도가 성립한다
	FPredictProjectilePathParams Params(ProjectileRadius, Origin, LaunchVelocity, PredictMaxSimTime);
	Params.bTraceWithCollision = true;
	Params.TraceChannel = ECC_Visibility;
	Params.SimFrequency = 24.f;
	Params.OverrideGravityZ = GravityZ;
	Params.ActorsToIgnore.Add(Owner);

	FPredictProjectilePathResult Result;
	const bool bHit = UGameplayStatics::PredictProjectilePath(this, Params, Result);

	const float FlightTime = Result.PathData.Num() > 0 ? Result.PathData.Last().Time : PredictMaxSimTime;
	// 착탄 표식은 표면 법선 방향으로 띄워 벽 파묻힘을 막는다
	const FVector ImpactPoint = bHit
		? Result.HitResult.ImpactPoint + Result.HitResult.ImpactNormal * ImpactMarkerOffset
		: Result.LastTraceDestination.Location;

	// NS_Foam_Trajectory는 Engine.Owner.Position을 발사 원점으로 읽는다
	TrajectoryPreview->SetWorldLocation(Origin);
	TrajectoryPreview->SetVariableVec3(TEXT("LaunchVelocity"), LaunchVelocity);
	TrajectoryPreview->SetVariableVec3(TEXT("ImpactPoint"), ImpactPoint);
	TrajectoryPreview->SetVariableFloat(TEXT("FlightTime"), FlightTime);
	TrajectoryPreview->SetVariableFloat(TEXT("GravityZ"), GravityZ);
}

void UMD_FoamGunComponent::DestroyTrajectoryPreview()
{
	if (TrajectoryPreview)
	{
		TrajectoryPreview->DestroyComponent();
		TrajectoryPreview = nullptr;
	}
}

void UMD_FoamGunComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 종료 중인 컴포넌트의 공통 Hotbar 구독 정리
	UMD_EquipHotbarComponent* Hotbar = GetHotbar();
	// 유효한 저장소에 등록된 변경 콜백 해제
	if (IsValid(Hotbar) == true)
	{
		// 컴포넌트 종료 이후 충전 UI 콜백 호출 방지
		Hotbar->OnHotbarChanged.RemoveDynamic(this, &UMD_FoamGunComponent::HandleHotbarChanged);
	}
	// 기존 조준 궤적 정리
	DestroyTrajectoryPreview();
	Super::EndPlay(EndPlayReason);
}
