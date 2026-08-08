#include "Component/Hazard/MD_HazardVolumeComponent.h"

#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

UMD_HazardVolumeComponent::UMD_HazardVolumeComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

	// 쿼리 전용 오버랩. 폰만 감지한다 (물리 밀어냄 없음)
	SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	SetCollisionObjectType(ECC_WorldDynamic);
	SetCollisionResponseToAllChannels(ECR_Ignore);
	SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	SetGenerateOverlapEvents(true);
	SetCanEverAffectNavigation(false);

	// 위험 반경 기본값 ~250cm
	InitSphereRadius(250.f);
}

void UMD_HazardVolumeComponent::BeginPlay()
{
	Super::BeginPlay();
}

void UMD_HazardVolumeComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	SetHazardActive(false);

	Super::EndPlay(EndPlayReason);
}

void UMD_HazardVolumeComponent::SetHazardActive(bool bNewActive)
{
	// 피해 적용은 서버에서만
	const AActor* Owner = GetOwner();
	if (!IsValid(Owner) || !Owner->HasAuthority())
	{
		return;
	}

	if (bHazardActive == bNewActive)
	{
		return;
	}

	bHazardActive = bNewActive;

	if (bHazardActive)
	{
		GetWorld()->GetTimerManager().SetTimer(
			HazardTimerHandle, this, &UMD_HazardVolumeComponent::ApplyHazardTick,
			DamageInterval, true);
	}
	else
	{
		GetWorld()->GetTimerManager().ClearTimer(HazardTimerHandle);
	}
}

void UMD_HazardVolumeComponent::ApplyHazardTick()
{
	TArray<AActor*> Overlapping;
	GetOverlappingActors(Overlapping, APawn::StaticClass());

	// 한 주기분 = 초당 피해 × 주기. 브리지가 없으면 UE 데미지만 발생하고 HP는 안 깎임(브리지 필요)
	const float TickDamage = DamagePerSecond * DamageInterval;

	for (AActor* Actor : Overlapping)
	{
		UGameplayStatics::ApplyDamage(Actor, TickDamage, nullptr, GetOwner(), HazardDamageType);
	}
}
