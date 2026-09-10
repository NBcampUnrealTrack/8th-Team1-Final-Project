#include "Component/Equipment/MD_FoamGunComponent.h"

#include "Character/MD_Character.h"
#include "Component/Equipment/MD_EquipHotbarComponent.h"
#include "Data/Equipment/MD_ToolDefinition.h"
#include "GameplayTags/MD_GameplayTags.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"

UMD_FoamGunComponent::UMD_FoamGunComponent()
{
	// 프리뷰 갱신용. 소유 클라이언트가 아니면 틱 초입에서 빠진다
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);
}

void UMD_FoamGunComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UMD_FoamGunComponent, FoamCharge);
}

bool UMD_FoamGunComponent::TryConsumeShotCharge()
{
	if (!HasChargeForShot())
	{
		return false;
	}

	FoamCharge -= ShotCost;
	// 리슨 서버는 OnRep이 안 오므로 직접 방송
	OnRep_FoamCharge();
	return true;
}

void UMD_FoamGunComponent::AddCharge(int32 Amount)
{
	if (Amount <= 0)
	{
		return;
	}

	FoamCharge = FMath::Min(FoamCharge + Amount, MaxCharge);
	OnRep_FoamCharge();
}

void UMD_FoamGunComponent::OnRep_FoamCharge()
{
	OnFoamChargeChanged.Broadcast(FoamCharge);
}

FVector UMD_FoamGunComponent::GetLaunchOrigin() const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	if (!Pawn)
	{
		return GetOwner() ? GetOwner()->GetActorLocation() : FVector::ZeroVector;
	}
	return Pawn->GetPawnViewLocation() + GetAimDirection() * MuzzleForwardOffset;
}

FVector UMD_FoamGunComponent::GetAimDirection() const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	return Pawn ? Pawn->GetBaseAimRotation().Vector() : FVector::ForwardVector;
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

	const FVector Origin = GetLaunchOrigin();
	const FVector LaunchVelocity = GetAimDirection() * LaunchSpeed;
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
	DestroyTrajectoryPreview();
	Super::EndPlay(EndPlayReason);
}
