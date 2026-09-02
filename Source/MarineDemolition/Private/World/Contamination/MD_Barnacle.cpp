#include "World/Contamination/MD_Barnacle.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Gameplay/Collision/MD_CollisionChannels.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "Types/Material/MD_MaterialParameterNames.h"
#include "World/Wall/MD_CuttableWall.h"
#include "Util/MD_MaterialTintUtility.h"
#include "World/Contamination/MD_BarnacleItem.h"
#include "World/Contamination/MD_BarnacleSpawner.h"

#if WITH_EDITOR
#include "Misc/MapErrors.h"
#include "Misc/UObjectToken.h"
#include "Logging/MessageLog.h"
#endif

AMD_Barnacle::AMD_Barnacle()
{
	// 평소에는 돌지 않는다. 맞아서 흔들리는 동안에만 켜고 잦아들면 다시 끈다.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	bReplicates = true;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);
	// 벽에 붙어 있는 부착물이라 물리로 떨어지지 않는다. 떼어낸 산출물만 물리다.
	Mesh->SetCollisionProfileName(TEXT("BlockAll"));
	Mesh->SetCollisionResponseToChannel(MDCollisionChannels::InteractTrace, ECR_Block);
	Mesh->SetSimulatePhysics(false);
}

void AMD_Barnacle::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION(AMD_Barnacle, RemovalWorkRequired, COND_InitialOnly);
	DOREPLIFETIME(AMD_Barnacle, AccumulatedRemovalWork);
}

void AMD_Barnacle::BeginPlay()
{
	Super::BeginPlay();

	ApplyRemovalProgressVisual();
}

float AMD_Barnacle::GetRemovalProgress() const
{
	const float SafeWorkRequired = GetMaximumDurability();

	return FMath::Clamp(AccumulatedRemovalWork / SafeWorkRequired, 0.0f, 1.0f);
}

float AMD_Barnacle::GetMaximumDurability() const
{
	return FMath::IsFinite(RemovalWorkRequired)
		? FMath::Max(1.0f, RemovalWorkRequired)
		: 1.0f;
}

float AMD_Barnacle::GetCurrentDurability() const
{
	return FMath::Max(0.0f, GetMaximumDurability() - AccumulatedRemovalWork);
}

bool AMD_Barnacle::IsFullyRemoved() const
{
	return GetRemovalProgress() >= 1.0f;
}

bool AMD_Barnacle::ApplyScrapeClickDamage(const float DurabilityDamage)
{
	if (!HasAuthority() || IsFullyRemoved())
	{
		return false;
	}

	// NaN/무한대나 음수가 들어오면 진행도가 무너지거나 되돌아가므로 거부한다.
	const float SafeDamage =
		(FMath::IsFinite(DurabilityDamage) && DurabilityDamage > 0.0f)
		? DurabilityDamage
		: 0.0f;
	if (SafeDamage <= 0.0f)
	{
		return false;
	}

	// 헤라 종류, 제거 진행도와 플레이어 방향에 관계없이 같은 피드백을 재생한다.
	MulticastPlayImpactWobble();

	AccumulatedRemovalWork = FMath::Min(
		AccumulatedRemovalWork + SafeDamage,
		GetMaximumDurability());
	ApplyRemovalProgressVisual();

	if (!IsFullyRemoved())
	{
		return false;
	}

	BreakOff();
	return true;
}

void AMD_Barnacle::OnRep_AccumulatedRemovalWork()
{
	ApplyRemovalProgressVisual();
}

void AMD_Barnacle::OnRep_RemovalWorkRequired()
{
	ApplyRemovalProgressVisual();
}

void AMD_Barnacle::MulticastPlayImpactWobble_Implementation()
{
	StartImpactWobble();
}

void AMD_Barnacle::StartImpactWobble()
{
	if (!IsValid(Mesh)
		|| !FMath::IsFinite(ImpactWobbleDurationSeconds)
		|| ImpactWobbleDurationSeconds <= 0.0f
		|| !FMath::IsFinite(ImpactWobbleFrequency)
		|| ImpactWobbleFrequency <= 0.0f
		|| !FMath::IsFinite(ImpactWobbleMaximumTiltDegrees)
		|| ImpactWobbleMaximumTiltDegrees <= 0.0f)
	{
		return;
	}

	if (WobbleElapsedSeconds < 0.0f)
	{
		// 스포너는 FinishSpawning 뒤 표면에 부착한다. 첫 타격 시점에 최종 부착 회전을
		// 잡아야 움직이는 부모나 회전된 가구에서도 정확한 자세로 돌아온다.
		MeshRestRelativeRotation = Mesh->GetRelativeRotation();
	}
	else
	{
		// 연속 클릭도 항상 같은 기준 자세와 같은 곡선에서 다시 시작한다.
		Mesh->SetRelativeRotation(MeshRestRelativeRotation);
	}

	WobbleElapsedSeconds = 0.0f;
	SetActorTickEnabled(true);
}

void AMD_Barnacle::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (WobbleElapsedSeconds < 0.0f || !IsValid(Mesh))
	{
		SetActorTickEnabled(false);
		return;
	}

	WobbleElapsedSeconds += DeltaSeconds;

	if (WobbleElapsedSeconds >= ImpactWobbleDurationSeconds)
	{
		// 감쇠가 0에 수렴하는 것에 기대지 않고 기준 회전으로 정확히 되돌린다.
		// 매 타격마다 미세한 잔차가 남으면 여러 번 맞은 따개비가 비뚤어진다.
		Mesh->SetRelativeRotation(MeshRestRelativeRotation);
		WobbleElapsedSeconds = -1.0f;
		SetActorTickEnabled(false);
		return;
	}

	const float NormalizedTime =
		FMath::Clamp(WobbleElapsedSeconds / ImpactWobbleDurationSeconds, 0.0f, 1.0f);

	// 감쇠 진동. 처음이 가장 크고 끝에서 0이 된다.
	const float Decay = 1.0f - NormalizedTime;
	const float Oscillation = FMath::Sin(
		2.0f * UE_PI * ImpactWobbleFrequency * WobbleElapsedSeconds);

	const float TiltDegrees =
		ImpactWobbleMaximumTiltDegrees * Decay * Decay * Oscillation;

	// 메시의 휴식 자세 기준 로컬 오른쪽 축을 사용해 타격 방향과 무관하게 흔든다.
	const FQuat RestRotation = MeshRestRelativeRotation.Quaternion();
	const FVector WobbleAxis = RestRotation.RotateVector(FVector::RightVector);
	const FQuat WobbleQuat(WobbleAxis, FMath::DegreesToRadians(TiltDegrees));
	Mesh->SetRelativeRotation(WobbleQuat * MeshRestRelativeRotation.Quaternion());
}

void AMD_Barnacle::ApplyRemovalProgressVisual()
{
	if (!IsValid(Mesh))
	{
		return;
	}

	if (!MeshMID)
	{
		MeshMID = Mesh->CreateAndSetMaterialInstanceDynamic(0);
	}

	if (MeshMID)
	{
		// 코드는 0~1 숫자 하나만 넘긴다. 어떻게 벗겨 보일지는 머티리얼이 정한다.
		MDMaterialTint::SetScalarParameterChecked(
			MeshMID,
			MDMaterialParameterNames::Barnacle::RemovalProgress,
			GetRemovalProgress());
	}
}

bool AMD_Barnacle::InitializeFromSpawner(
	AMD_BarnacleSpawner* const InSpawner,
	const int32 InSpawnIndex,
	const int32 InMaximumDurability)
{
	if (!HasAuthority()
		|| HasActorBegunPlay()
		|| bInitializedFromSpawner
		|| !IsValid(InSpawner)
		|| InSpawnIndex < 0
		|| InMaximumDurability < 1)
	{
		return false;
	}

	OwningSpawner = InSpawner;
	SpawnIndex = InSpawnIndex;
	RemovalWorkRequired = static_cast<float>(InMaximumDurability);
	AccumulatedRemovalWork = 0.0f;
	bInitializedFromSpawner = true;
	return true;
}

void AMD_Barnacle::BreakOff()
{
	UWorld* const World = GetWorld();
	if (!World)
	{
		return;
	}

	// 배치기에 "이 순번은 떨어졌다"고 알린다. Day 스냅샷이 이 목록을 저장해야
	// 내일 아침 같은 자리에 되살아나지 않는다.
	if (AMD_BarnacleSpawner* const Spawner = OwningSpawner.Get())
	{
		Spawner->NotifyBarnacleRemoved(SpawnIndex);
	}

	if (BarnacleItemClass)
	{
		const float SafeOffsetCm = FMath::IsFinite(ItemSpawnOffsetCm)
			? FMath::Max(0.0f, ItemSpawnOffsetCm)
			: 0.0f;

		// 붙어 있던 자리에서 바깥으로 밀어낸다. 메시 안에서 스폰하면 물리가 튕겨 낸다.
		const FTransform SpawnTransform(
			GetActorRotation(),
			GetActorLocation() + GetActorUpVector() * SafeOffsetCm);

		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

		World->SpawnActor<AMD_BarnacleItem>(
			BarnacleItemClass,
			SpawnTransform,
			SpawnParams);
	}

	Destroy();
}

#if WITH_EDITOR
void AMD_Barnacle::CheckForErrors()
{
	Super::CheckForErrors();

	UWorld* const World = GetWorld();
	if (!World || !IsValid(Mesh))
	{
		return;
	}

	TArray<AActor*> OverlappingActors;
	Mesh->GetOverlappingActors(OverlappingActors, AMD_CuttableWall::StaticClass());
	if (OverlappingActors.IsEmpty())
	{
		return;
	}

	FMessageLog("MapCheck").Warning()
		->AddToken(FUObjectToken::Create(this))
		->AddToken(FTextToken::Create(NSLOCTEXT(
			"MD_Barnacle",
			"BarnacleOnCuttableWall",
			"따개비가 철거 대상 격벽과 겹쳐 있습니다. 기획 02 §1에 따라 따개비는 "
			"보통벽에만 배치합니다 (내력·칸막이 세그먼트 제외).")));
}
#endif
