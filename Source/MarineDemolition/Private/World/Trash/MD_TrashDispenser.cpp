#include "World/Trash/MD_TrashDispenser.h"

#include "Components/ArrowComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Gameplay/Collision/MD_CollisionChannels.h"
#include "Util/MD_LogUtility.h"
#include "World/Trash/MD_TrashBin.h"

AMD_TrashDispenser::AMD_TrashDispenser()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetReplicateMovement(false);

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Root);
	Mesh->SetCollisionProfileName(TEXT("BlockAll"));
	Mesh->SetSimulatePhysics(false);

	InteractionVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("InteractionVolume"));
	InteractionVolume->SetupAttachment(Root);
	InteractionVolume->SetBoxExtent(FVector(75.f, 75.f, 100.f));
	InteractionVolume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InteractionVolume->SetCollisionObjectType(ECC_WorldDynamic);
	InteractionVolume->SetCollisionResponseToAllChannels(ECR_Ignore);
	InteractionVolume->SetCollisionResponseToChannel(MDCollisionChannels::InteractTrace, ECR_Block);
	InteractionVolume->SetGenerateOverlapEvents(false);
	InteractionVolume->SetCanEverAffectNavigation(false);

	SpawnPoint = CreateDefaultSubobject<UArrowComponent>(TEXT("SpawnPoint"));
	SpawnPoint->SetupAttachment(Root);
	// SM_TrashDispenser 앞쪽에 두고, 통 바닥이 지면과 겹치지 않게 2cm만 띄운다.
	SpawnPoint->SetRelativeLocation(FVector(100.f, 0.f, 2.f));
	SpawnPoint->SetArrowColor(FColor::Cyan);
	SpawnPoint->ArrowSize = 1.5f;
	SpawnPoint->bHiddenInGame = true;

	SpawnClearance = CreateDefaultSubobject<UBoxComponent>(TEXT("SpawnClearance"));
	SpawnClearance->SetupAttachment(SpawnPoint);
	// BP_MD_TrashBin의 0.75 스케일 경계(약 36×32×37cm 반경)에 여유를 더한다.
	SpawnClearance->SetRelativeLocation(FVector(0.f, 0.f, 38.f));
	SpawnClearance->SetBoxExtent(FVector(40.f, 36.f, 38.f));
	// 출구를 막고 있는 통을 상시 감지해야 하므로 물리에는 관여하지 않는 Query 전용으로 둔다.
	SpawnClearance->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	SpawnClearance->SetCollisionObjectType(ECC_WorldDynamic);
	SpawnClearance->SetCollisionResponseToAllChannels(ECR_Ignore);
	SpawnClearance->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Overlap);
	SpawnClearance->SetGenerateOverlapEvents(true);
	SpawnClearance->SetCanEverAffectNavigation(false);
	SpawnClearance->ShapeColor = FColor::Green;
}

void AMD_TrashDispenser::BeginPlay()
{
	Super::BeginPlay();

	if (IsValid(Mesh))
	{
		DefaultMeshInteractResponse =
			Mesh->GetCollisionResponseToChannel(MDCollisionChannels::InteractTrace);
	}

	if (IsValid(InteractionVolume))
	{
		DefaultVolumeInteractResponse =
			InteractionVolume->GetCollisionResponseToChannel(MDCollisionChannels::InteractTrace);
	}

	// 상호작용 Trace는 각 머신에서 로컬로 쏘므로 서버·클라이언트 모두에서 갱신해야 한다.
	if (IsValid(SpawnClearance))
	{
		SpawnClearance->OnComponentBeginOverlap.AddDynamic(
			this, &AMD_TrashDispenser::HandleClearanceBeginOverlap);
		SpawnClearance->OnComponentEndOverlap.AddDynamic(
			this, &AMD_TrashDispenser::HandleClearanceEndOverlap);
	}

	// 델리게이트를 붙이기 전부터 출구에 놓여 있던 통도 최초 상태에 포함한다.
	RefreshInteractTraceResponse();
}

void AMD_TrashDispenser::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (IsValid(SpawnClearance))
	{
		SpawnClearance->OnComponentBeginOverlap.RemoveDynamic(
			this, &AMD_TrashDispenser::HandleClearanceBeginOverlap);
		SpawnClearance->OnComponentEndOverlap.RemoveDynamic(
			this, &AMD_TrashDispenser::HandleClearanceEndOverlap);
	}

	for (const TWeakObjectPtr<AMD_TrashBin>& BinPtr : LiveDispensedBins)
	{
		if (AMD_TrashBin* Bin = BinPtr.Get())
		{
			Bin->OnDestroyed.RemoveDynamic(this, &AMD_TrashDispenser::HandleDispensedBinDestroyed);
		}
	}
	LiveDispensedBins.Reset();

	Super::EndPlay(EndPlayReason);
}

void AMD_TrashDispenser::SetInteractionHighlighted_Implementation(bool bHighlighted)
{
	if (!IsValid(Mesh)) return;

	Mesh->SetCustomDepthStencilValue(IMD_Interactable::HighlightStencilValue);
	Mesh->SetRenderCustomDepth(bHighlighted);
}

bool AMD_TrashDispenser::RequiresServerInteraction_Implementation() const
{
	return true;
}

void AMD_TrashDispenser::Interact_Implementation(AActor* Interactor)
{
	if (!HasAuthority() || !GetWorld()) return;

	const double CurrentTime = GetWorld()->GetTimeSeconds();
	if (CurrentTime < NextAllowedInteractionTime)
	{
		// 결과까지 multicast하면 공격자의 요청 하나가 모든 client의 Reliable RPC로 증폭된다.
		return;
	}
	NextAllowedInteractionTime = CurrentTime + FMath::Max(0.05f, InteractionCooldown);

	if (!IsValid(SpawnPoint) || !IsValid(SpawnClearance) || !TrashBinClass)
	{
		UE_LOG(MarineDemolition, Warning,
			TEXT("'%s' 보급기의 SpawnPoint, SpawnClearance 또는 TrashBinClass가 설정되지 않았다."),
			*GetName());
		MulticastDispenseResult(EMD_TrashDispenseResult::NotConfigured);
		return;
	}

	CompactLiveDispensedBins();
	if (LiveDispensedBins.Num() >= FMath::Max(1, MaxLiveDispensedBins))
	{
		MulticastDispenseResult(EMD_TrashDispenseResult::LiveLimitReached);
		return;
	}

	if (!IsSpawnAreaClear())
	{
		MulticastDispenseResult(EMD_TrashDispenseResult::Blocked);
		return;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;
	SpawnParams.Instigator = Cast<APawn>(Interactor);
	SpawnParams.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButDontSpawnIfColliding;

	FTransform SpawnTransform = SpawnPoint->GetComponentTransform();
	SpawnTransform.SetScale3D(SpawnTransform.GetScale3D() / GetActorScale3D());

	AMD_TrashBin* SpawnedTrashBin = GetWorld()->SpawnActor<AMD_TrashBin>(
		TrashBinClass,
		SpawnTransform,
		SpawnParams);

	if (IsValid(SpawnedTrashBin))
	{
		if (RegisterTrackedBin(SpawnedTrashBin))
		{
			MulticastDispenseResult(EMD_TrashDispenseResult::Success);
		}
		else
		{
			SpawnedTrashBin->Destroy();
			MulticastDispenseResult(EMD_TrashDispenseResult::SpawnFailed);
		}
	}
	else
	{
		MulticastDispenseResult(EMD_TrashDispenseResult::SpawnFailed);
	}
}

bool AMD_TrashDispenser::RegisterTrackedBin(AMD_TrashBin* Bin)
{
	if (!HasAuthority() || !IsValid(Bin) || Bin->IsActorBeingDestroyed())
	{
		return false;
	}

	CompactLiveDispensedBins();
	const TWeakObjectPtr<AMD_TrashBin> BinPtr(Bin);
	if (!LiveDispensedBins.Contains(BinPtr))
	{
		LiveDispensedBins.Add(BinPtr);
		Bin->OnDestroyed.AddUniqueDynamic(
			this, &AMD_TrashDispenser::HandleDispensedBinDestroyed);
	}
	return true;
}

void AMD_TrashDispenser::HandleDispensedBinDestroyed(AActor* DestroyedActor)
{
	if (!DestroyedActor) return;

	LiveDispensedBins.Remove(TWeakObjectPtr<AMD_TrashBin>(Cast<AMD_TrashBin>(DestroyedActor)));
}

void AMD_TrashDispenser::CompactLiveDispensedBins()
{
	LiveDispensedBins.RemoveAll(
		[](const TWeakObjectPtr<AMD_TrashBin>& BinPtr)
		{
			return !BinPtr.IsValid();
		});
}

bool AMD_TrashDispenser::IsSpawnAreaClear() const
{
	if (!GetWorld() || !IsValid(SpawnClearance)) return false;

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(TrashDispenserClearance), false, this);
	QueryParams.AddIgnoredActor(this);

	FCollisionResponseParams ResponseParams(ECR_Block);
	ResponseParams.CollisionResponse.SetResponse(ECC_Pawn, ECR_Ignore);

	const FCollisionShape ClearanceShape =
		FCollisionShape::MakeBox(SpawnClearance->GetScaledBoxExtent());

	TArray<FOverlapResult> Overlaps;
	GetWorld()->OverlapMultiByChannel(
		Overlaps,
		SpawnClearance->GetComponentLocation(),
		SpawnClearance->GetComponentQuat(),
		ECC_PhysicsBody,
		ClearanceShape,
		QueryParams,
		ResponseParams);

	for (const FOverlapResult& Overlap : Overlaps)
	{
		if (!Overlap.bBlockingHit) continue;

		UE_LOG(MarineDemolition, Log,
			TEXT("'%s' 보급기 출구를 '%s'의 '%s' 컴포넌트가 막고 있다."),
			*GetName(),
			*GetNameSafe(Overlap.GetActor()),
			*GetNameSafe(Overlap.GetComponent()));
		return false;
	}

	return true;
}

bool AMD_TrashDispenser::HasTrashBinInClearance() const
{
	if (!IsValid(SpawnClearance)) return false;

	TArray<AActor*> OverlappingBins;
	SpawnClearance->GetOverlappingActors(OverlappingBins, AMD_TrashBin::StaticClass());

	for (const AActor* Bin : OverlappingBins)
	{
		if (IsValid(Bin)) return true;
	}

	return false;
}

void AMD_TrashDispenser::RefreshInteractTraceResponse()
{
	const bool bOccupied = HasTrashBinInClearance();
	if (bOccupied == bClearanceOccupied) return;

	bClearanceOccupied = bOccupied;

	// 통이 남아 있는 동안에는 보급기가 Trace를 가로채지 않아야 그 통을 집어 치울 수 있다.
	if (IsValid(Mesh))
	{
		Mesh->SetCollisionResponseToChannel(
			MDCollisionChannels::InteractTrace,
			bOccupied ? ECR_Ignore : DefaultMeshInteractResponse);
	}

	if (IsValid(InteractionVolume))
	{
		InteractionVolume->SetCollisionResponseToChannel(
			MDCollisionChannels::InteractTrace,
			bOccupied ? ECR_Ignore : DefaultVolumeInteractResponse);
	}
}

void AMD_TrashDispenser::HandleClearanceBeginOverlap(
	UPrimitiveComponent* /*OverlappedComponent*/,
	AActor* OtherActor,
	UPrimitiveComponent* /*OtherComp*/,
	int32 /*OtherBodyIndex*/,
	bool /*bFromSweep*/,
	const FHitResult& /*SweepResult*/)
{
	if (!IsValid(OtherActor) || !OtherActor->IsA<AMD_TrashBin>()) return;

	RefreshInteractTraceResponse();
}

void AMD_TrashDispenser::HandleClearanceEndOverlap(
	UPrimitiveComponent* /*OverlappedComponent*/,
	AActor* OtherActor,
	UPrimitiveComponent* /*OtherComp*/,
	int32 /*OtherBodyIndex*/)
{
	// 통이 파괴되며 겹침이 풀리는 경우도 복구 대상이므로 IsValid 대신 널 검사만 한다.
	if (!OtherActor || !OtherActor->IsA<AMD_TrashBin>()) return;

	RefreshInteractTraceResponse();
}

void AMD_TrashDispenser::MulticastDispenseResult_Implementation(
	EMD_TrashDispenseResult Result)
{
	OnDispenseResult(Result);
}
