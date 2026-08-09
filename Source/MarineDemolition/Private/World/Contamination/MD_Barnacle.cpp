#include "World/Contamination/MD_Barnacle.h"

#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "Component/Scrape/MD_ScrapeSurfaceComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "Gameplay/Collision/MD_CollisionChannels.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
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

	DOREPLIFETIME(AMD_Barnacle, AccumulatedRemovalWork);
}

void AMD_Barnacle::BeginPlay()
{
	Super::BeginPlay();

	if (IsValid(Mesh))
	{
		// 흔들림은 이 회전을 기준으로 오간다. 배치기가 법선 축으로 돌려 붙이므로
		// 액터 회전이 아니라 메시의 부착 회전을 기준으로 잡아야 한다.
		MeshRestRelativeRotation = Mesh->GetRelativeRotation();
	}

	ApplyRemovalProgressVisual();
	RegisterWithScrapeSurface();
}

void AMD_Barnacle::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 떨어져 나갔든 레벨이 내려갔든 표면에 남은 등록은 지운다.
	// 남겨 두면 없는 따개비가 헤라를 계속 막는다.
	UnregisterFromScrapeSurface();

	Super::EndPlay(EndPlayReason);
}

bool AMD_Barnacle::RegisterWithScrapeSurface(
	UMD_ScrapeSurfaceComponent* const PreferredSurface)
{
	UMD_ScrapeSurfaceComponent* const ExistingSurface =
		RegisteredScrapeSurface.Get();
	if (IsValid(ExistingSurface)
		&& (!IsValid(PreferredSurface)
			|| ExistingSurface == PreferredSurface))
	{
		return true;
	}

	UMD_ScrapeSurfaceComponent* Surface = PreferredSurface;
	if (!IsValid(Surface))
	{
		// 배치기가 맞은 컴포넌트에 붙이므로 부착 부모가 가장 확실한 단서다.
		const USceneComponent* const AttachParentComponent = GetRootComponent()
			? GetRootComponent()->GetAttachParent()
			: nullptr;
		Surface = UMD_ScrapeSurfaceComponent::FindForPrimitiveComponent(
			Cast<UPrimitiveComponent>(AttachParentComponent));
	}

	if (!Surface && IsValid(Mesh) && GetWorld())
	{
		// 손으로 놓은 따개비는 붙어 있지 않다. 밑동이 닿아 있을 만한 범위만 훑는다.
		const float SearchRadius = FMath::Max(
			1.0f,
			static_cast<float>(Mesh->Bounds.SphereRadius));

		TArray<FOverlapResult> Overlaps;
		FCollisionObjectQueryParams ObjectQueryParams;
		ObjectQueryParams.AddObjectTypesToQuery(ECC_WorldStatic);
		ObjectQueryParams.AddObjectTypesToQuery(ECC_WorldDynamic);

		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MD_BarnacleSurfaceSearch), false);
		QueryParams.AddIgnoredActor(this);

		if (GetWorld()->OverlapMultiByObjectType(
				Overlaps,
				GetActorLocation(),
				FQuat::Identity,
				ObjectQueryParams,
				FCollisionShape::MakeSphere(SearchRadius),
				QueryParams))
		{
			for (const FOverlapResult& Overlap : Overlaps)
			{
				if (UMD_ScrapeSurfaceComponent* const OverlappedSurface =
						UMD_ScrapeSurfaceComponent::FindForPrimitiveComponent(
							Overlap.GetComponent()))
				{
					Surface = OverlappedSurface;
					break;
				}
			}
		}
	}

	if (Surface && Surface->RegisterScrapeObstacle(this))
	{
		if (IsValid(ExistingSurface) && ExistingSurface != Surface)
		{
			ExistingSurface->UnregisterScrapeObstacle(this);
		}
		RegisteredScrapeSurface = Surface;
		return true;
	}

	return false;
}

void AMD_Barnacle::UnregisterFromScrapeSurface()
{
	if (UMD_ScrapeSurfaceComponent* const Surface =
		RegisteredScrapeSurface.Get())
	{
		Surface->UnregisterScrapeObstacle(this);
	}

	RegisteredScrapeSurface.Reset();
}

float AMD_Barnacle::GetScrapeBlockRadiusCm() const
{
	if (!IsValid(Mesh)
		|| !FMath::IsFinite(ScrapeBlockRadiusScale)
		|| ScrapeBlockRadiusScale <= 0.0f)
	{
		return 0.0f;
	}

	// 벽면에 드리우는 크기는 법선 축을 뺀 나머지가 정한다. 구 반경을 그대로 쓰면
	// 벽 밖으로 튀어나온 높이까지 막는 폭에 들어가 실제보다 넓게 막힌다.
	const FVector BoxExtent = Mesh->Bounds.BoxExtent;
	const double AlongNormal = FVector::DotProduct(
		BoxExtent,
		GetActorUpVector().GetAbs());

	const double PlanarRadius = FMath::Sqrt(FMath::Max(
		BoxExtent.SizeSquared() - AlongNormal * AlongNormal,
		0.0));

	return static_cast<float>(PlanarRadius) * ScrapeBlockRadiusScale;
}

bool AMD_Barnacle::IsImpactOnCooldown() const
{
	const UWorld* const World = GetWorld();
	if (!World || MinimumImpactIntervalSeconds <= 0.0f)
	{
		return false;
	}

	return World->GetTimeSeconds() - LastImpactWorldSeconds
		< MinimumImpactIntervalSeconds;
}

float AMD_Barnacle::GetRemovalProgress() const
{
	const float SafeWorkRequired = FMath::IsFinite(RemovalWorkRequired)
		? FMath::Max(UE_SMALL_NUMBER, RemovalWorkRequired)
		: UE_SMALL_NUMBER;

	return FMath::Clamp(AccumulatedRemovalWork / SafeWorkRequired, 0.0f, 1.0f);
}

bool AMD_Barnacle::IsFullyRemoved() const
{
	return GetRemovalProgress() >= 1.0f;
}

bool AMD_Barnacle::ApplyScrapeImpact(
	const float ImpactWork,
	const FVector& ImpactWorldDirection)
{
	UWorld* const World = GetWorld();
	if (!HasAuthority() || !World || IsFullyRemoved())
	{
		return false;
	}

	// NaN/무한대가 들어오면 진행도가 통째로 무너진다. 계산 전에 막는다.
	// 음수 작업량은 진행도를 되돌려 버리므로 0과 함께 걸러 낸다.
	const float SafeImpactWork =
		(FMath::IsFinite(ImpactWork) && ImpactWork > 0.0f) ? ImpactWork : 0.0f;

	if (IsImpactOnCooldown())
	{
		return false;
	}

	LastImpactWorldSeconds = World->GetTimeSeconds();

	// 거의 다 떨어진 따개비가 더 크게 흔들려야 "이제 곧 떨어진다"가 읽힌다.
	// 힘이 모자란 접촉(SafeImpactWork == 0)도 최소한의 흔들림은 남긴다.
	const float StrengthFromWork = FMath::Clamp(SafeImpactWork * 4.0f, 0.0f, 1.0f);
	const float NewWobbleScale = FMath::Clamp(
		0.25f + 0.75f * StrengthFromWork * (0.4f + 0.6f * GetRemovalProgress()),
		0.0f,
		1.0f);

	MulticastPlayImpactWobble(
		ImpactWorldDirection.GetSafeNormal(),
		NewWobbleScale);

	if (SafeImpactWork <= 0.0f)
	{
		return false;
	}

	AccumulatedRemovalWork += SafeImpactWork;
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

void AMD_Barnacle::MulticastPlayImpactWobble_Implementation(
	const FVector_NetQuantizeNormal ImpactWorldDirection,
	const float InWobbleScale)
{
	StartImpactWobble(ImpactWorldDirection, InWobbleScale);
}

void AMD_Barnacle::StartImpactWobble(
	const FVector& ImpactWorldDirection,
	const float InWobbleScale)
{
	if (!IsValid(Mesh)
		|| !FMath::IsFinite(InWobbleScale)
		|| InWobbleScale <= 0.0f
		|| ImpactWobbleDurationSeconds <= 0.0f)
	{
		return;
	}

	// 밑동이 벽에 붙어 있으므로 밀린 방향으로 미끄러지는 것이 아니라 기울어진다.
	// 회전축은 부착 법선과 밀린 방향 둘 다에 수직인 축이다.
	const FVector UpVector = GetActorUpVector();
	const FVector PushDirection = ImpactWorldDirection.GetSafeNormal();
	const FVector WorldAxis = FVector::CrossProduct(UpVector, PushDirection);

	if (WorldAxis.IsNearlyZero())
	{
		// 정면에서 밀면 기울 방향이 정해지지 않는다. 흔들 이유가 없다.
		return;
	}

	// 상대 회전과 같은 프레임(부착 부모)으로 옮겨 둔다. 액터 자신의 현재 회전을
	// 기준으로 잡으면 이미 기울어져 있는 동안 맞았을 때 축이 조금씩 어긋난다.
	const USceneComponent* const AttachParent = Mesh->GetAttachParent();
	const FQuat ParentWorldRotation = AttachParent
		? AttachParent->GetSocketQuaternion(Mesh->GetAttachSocketName())
		: FQuat::Identity;

	WobbleLocalAxis =
		ParentWorldRotation.Inverse().RotateVector(WorldAxis).GetSafeNormal();
	WobbleScale = FMath::Clamp(InWobbleScale, 0.0f, 1.0f);
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
		ImpactWobbleMaximumTiltDegrees * WobbleScale * Decay * Decay * Oscillation;

	const FQuat WobbleQuat(WobbleLocalAxis, FMath::DegreesToRadians(TiltDegrees));
	Mesh->SetRelativeRotation(WobbleQuat * MeshRestRelativeRotation.Quaternion());
}

void AMD_Barnacle::ApplyRemovalProgressVisual()
{
	if (!IsValid(Mesh) || RemovalProgressParameterName.IsNone())
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
			RemovalProgressParameterName,
			GetRemovalProgress());
	}
}

void AMD_Barnacle::AssignSpawnerIdentity(
	AMD_BarnacleSpawner* const InSpawner,
	const int32 InSpawnIndex)
{
	OwningSpawner = InSpawner;
	SpawnIndex = InSpawnIndex;
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

	// 기획 02 §1: 따개비는 보통벽에만 붙는다. 내력·칸막이 세그먼트에는 두지 않는다.
	// 배치를 잘못해도 게임은 정상 동작하므로 눈으로는 잡히지 않는다.
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
