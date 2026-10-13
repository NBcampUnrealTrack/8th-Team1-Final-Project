#include "World/Trash/MD_TrashDispenser.h"
#include "Gameplay/Interaction/MD_InteractionPresentationLibrary.h"
#include "Component/Interaction/MD_InteractionPromptComponent.h"

#include "Components/ArrowComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/OverlapResult.h"
#include "Gameplay/Collision/MD_CollisionChannels.h"
#include "Util/MD_LogUtility.h"
#include "World/Trash/MD_TrashBin.h"

AMD_TrashDispenser::AMD_TrashDispenser()
{
	// 인스턴스와 파생 BP가 행동 유형을 선택하는 기본 구성 생성
	InteractionPrompt = CreateDefaultSubobject<UMD_InteractionPromptComponent>(TEXT("InteractionPrompt"));
	// 기존 상호작용 동작에 대응하는 공통 문구 유형 지정
	InteractionPrompt->PromptType = EMD_InteractionPromptType::SpawnTrashBin;

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

	DefaultMeshInteractResponse =
		Mesh->GetCollisionResponseToChannel(MDCollisionChannels::InteractTrace);
	DefaultVolumeInteractResponse =
		InteractionVolume->GetCollisionResponseToChannel(MDCollisionChannels::InteractTrace);

	// 상호작용 Trace는 각 머신에서 로컬로 쏘므로 서버·클라이언트 모두에서 갱신해야 한다.
	SpawnClearance->OnComponentBeginOverlap.AddDynamic(
		this, &AMD_TrashDispenser::HandleClearanceBeginOverlap);
	SpawnClearance->OnComponentEndOverlap.AddDynamic(
		this, &AMD_TrashDispenser::HandleClearanceEndOverlap);

	// 델리게이트를 붙이기 전부터 출구에 놓여 있던 통도 최초 상태에 포함한다.
	RefreshInteractTraceResponse();
}

void AMD_TrashDispenser::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	SpawnClearance->OnComponentBeginOverlap.RemoveDynamic(
		this, &AMD_TrashDispenser::HandleClearanceBeginOverlap);
	SpawnClearance->OnComponentEndOverlap.RemoveDynamic(
		this, &AMD_TrashDispenser::HandleClearanceEndOverlap);

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
	Mesh->SetCustomDepthStencilValue(IMD_Interactable::HighlightStencilValue);
	Mesh->SetRenderCustomDepth(bHighlighted);
}

FText AMD_TrashDispenser::GetInteractionPrompt_Implementation(AActor* Interactor) const
{
	// 선택한 행동 유형의 설정 문구와 현재 입력 키 표시
	return UMD_InteractionPresentationLibrary::FindActorActionPrompt(this, Interactor);
}

void AMD_TrashDispenser::Interact_Implementation(AActor* Interactor)
{
	if (!HasAuthority()) return;

	APawn* RequestingPawn = Cast<APawn>(Interactor);

	const double CurrentTime = GetWorld()->GetTimeSeconds();
	if (CurrentTime < NextAllowedInteractionTime)
	{
		// 결과까지 multicast하면 공격자의 요청 하나가 모든 client의 RPC로 증폭된다.
		return;
	}
	NextAllowedInteractionTime = CurrentTime + InteractionCooldown;

	if (!TrashBinClass)
	{
		UE_LOG(MarineDemolition, Warning,
			TEXT("'%s' 보급기의 TrashBinClass가 설정되지 않았다."),
			*GetName());
		MulticastDispenseResult(EMD_TrashDispenseResult::NotConfigured, RequestingPawn);
		return;
	}

	CompactLiveDispensedBins();
	if (LiveDispensedBins.Num() >= MaxLiveDispensedBins)
	{
		MulticastDispenseResult(EMD_TrashDispenseResult::LiveLimitReached, RequestingPawn);
		return;
	}

	if (!CanSpawnBinHere())
	{
		MulticastDispenseResult(EMD_TrashDispenseResult::Blocked, RequestingPawn);
		return;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;
	SpawnParams.Instigator = RequestingPawn;
	SpawnParams.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButDontSpawnIfColliding;

	// SpawnActor의 기본 스케일 처리(MultiplyWithRoot)는 BP 루트 스케일에 이 트랜스폼의 스케일을 곱한다.
	// 보급기를 레벨에서 키워도 통은 BP에 저장된 크기로 나와야 하므로 액터 스케일만 상쇄해서 넘긴다.
	FTransform SpawnTransform = SpawnPoint->GetComponentTransform();
	FVector SpawnScale = SpawnTransform.GetScale3D();
	const FVector ActorScale = GetActorScale3D();
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		// 액터 스케일이 0인 배치에서 inf/NaN 스케일로 물리 바디가 생성되는 것을 막는다.
		SpawnScale[Axis] = FMath::IsNearlyZero(ActorScale[Axis])
			? 1.0
			: SpawnScale[Axis] / ActorScale[Axis];
	}
	SpawnTransform.SetScale3D(SpawnScale);

	AMD_TrashBin* SpawnedTrashBin = GetWorld()->SpawnActor<AMD_TrashBin>(
		TrashBinClass,
		SpawnTransform,
		SpawnParams);

	if (!IsValid(SpawnedTrashBin))
	{
		MulticastDispenseResult(EMD_TrashDispenseResult::SpawnFailed, RequestingPawn);
		return;
	}

	if (!RegisterDispensedBin(SpawnedTrashBin))
	{
		SpawnedTrashBin->Destroy();
		MulticastDispenseResult(EMD_TrashDispenseResult::SpawnFailed, RequestingPawn);
		return;
	}
	MulticastDispenseResult(EMD_TrashDispenseResult::Success, RequestingPawn);
}

bool AMD_TrashDispenser::RegisterDispensedBin(AMD_TrashBin* const Bin)
{
	if (!HasAuthority()
		|| !IsValid(Bin)
		|| Bin->GetWorld() != GetWorld()
		|| Bin->IsActorBeingDestroyed())
	{
		return false;
	}

	const FName ExistingSourceName = Bin->GetSourceTrashDispenserName();
	if (!ExistingSourceName.IsNone() && ExistingSourceName != GetFName())
	{
		return false;
	}

	Bin->SetSourceTrashDispenser(this);
	// 같은 이름이어도 다른 구역에서 지급한 통의 원장 혼합 차단
	if (Bin->GetSourceTrashDispenserName() != GetFName()
		|| Bin->GetSourceTrashDispenserLevelPackage() != FName(UWorld::RemovePIEPrefix(GetLevel()->GetOutermost()->GetName())))
	{
		// 원래 보급기와 일치하지 않는 등록 실패 반환
		return false;
	}

	Bin->SetOwner(this);
	CompactLiveDispensedBins();
	LiveDispensedBins.AddUnique(Bin);
	Bin->OnDestroyed.AddUniqueDynamic(
		this,
		&AMD_TrashDispenser::HandleDispensedBinDestroyed);
	return true;
}

void AMD_TrashDispenser::HandleDispensedBinDestroyed(AActor* DestroyedActor)
{
	// AMD_TrashBin의 OnDestroyed에만 바인딩하므로 다른 타입이 들어올 수 없다.
	LiveDispensedBins.Remove(TWeakObjectPtr<AMD_TrashBin>(CastChecked<AMD_TrashBin>(DestroyedActor)));
}

void AMD_TrashDispenser::CompactLiveDispensedBins()
{
	LiveDispensedBins.RemoveAll(
		[](const TWeakObjectPtr<AMD_TrashBin>& BinPtr)
		{
			return !BinPtr.IsValid();
		});
}

bool AMD_TrashDispenser::CanSpawnBinHere() const
{
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(TrashDispenserClearance), false, this);

	// 스폰 단계의 AdjustIfPossibleButDontSpawnIfColliding은 Pawn을 막는 통 충돌로 판정하므로
	// 여기서도 Pawn을 장애물로 본다. 두 판정이 어긋나면 사람이 서 있을 때 Blocked 대신
	// SpawnFailed가 나가고, 밀어내기가 성공하면 통이 캡슐 안에서 물리를 시작한다.
	FCollisionResponseParams ResponseParams(ECR_Block);

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

bool AMD_TrashDispenser::ShouldYieldInteractTraceToBin() const
{
	TArray<AActor*> OverlappingBins;
	SpawnClearance->GetOverlappingActors(OverlappingBins, AMD_TrashBin::StaticClass());

	// GetOverlappingActors가 파괴 중인 액터를 이미 걸러내므로 개수만 보면 된다.
	return OverlappingBins.Num() > 0;
}

void AMD_TrashDispenser::RefreshInteractTraceResponse()
{
	const bool bOccupied = ShouldYieldInteractTraceToBin();
	if (bOccupied == bClearanceOccupied) return;

	bClearanceOccupied = bOccupied;

	// 통이 남아 있는 동안에는 보급기가 Trace를 가로채지 않아야 그 통을 집어 치울 수 있다.
	Mesh->SetCollisionResponseToChannel(
		MDCollisionChannels::InteractTrace,
		bOccupied ? ECR_Ignore : DefaultMeshInteractResponse);

	InteractionVolume->SetCollisionResponseToChannel(
		MDCollisionChannels::InteractTrace,
		bOccupied ? ECR_Ignore : DefaultVolumeInteractResponse);
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
	EMD_TrashDispenseResult Result, APawn* RequestingPawn)
{
	// 실패 메시지처럼 요청자 화면에만 띄울 연출을 BP가 구분할 수 있게 로컬 여부를 계산해 넘긴다.
	const bool bIsLocalRequester = IsValid(RequestingPawn) && RequestingPawn->IsLocallyControlled();
	OnDispenseResult(Result, bIsLocalRequester);
}
