#include "World/Trash/MD_TrashBin.h"
#include "Audio/MD_GameplayAudioFeedback.h"

#include "Character/MD_Character.h"
#include "Components/BoxComponent.h"
#include "EngineUtils.h"
#include "Gameplay/Collision/MD_CollisionChannels.h"
#include "Gameplay/Trash/MD_TrashContentClaimSubsystem.h"
#include "GameplayTags/MD_GameplayTags.h"
#include "NiagaraFunctionLibrary.h"
#include "Net/UnrealNetwork.h"
#include "World/Trash/MD_TrashDispenser.h"
#include "World/Trash/MD_TrashBinLiquidSloshVisualComponent.h"

bool FMD_TrashBinLiquidState::IsValid(
	const double MaximumLiquidUnits) const
{
	return FMath::IsFinite(MaximumLiquidUnits)
		&& MaximumLiquidUnits > 0.0
		&& FMath::IsFinite(LiquidUnits)
		&& LiquidUnits >= 0.0
		&& LiquidUnits <= MaximumLiquidUnits
		&& FMath::IsFinite(RewardUnits)
		&& RewardUnits >= 0.0
		&& RewardUnits <= LiquidUnits + 1.0e-9
		&& Revision >= 0;
}

bool FMD_TrashBinSpillCosmeticResult::IsValid() const
{
	return TransactionId.IsValid()
		&& CommittedLiquidRevision >= 0
		&& FMath::IsFinite(RemainingLiquidUnits)
		&& RemainingLiquidUnits >= 0.0
		&& RemainingLiquidUnits <= 5.0
		&& !SourceWorldLocation.ContainsNaN()
		&& !ContactWorldLocation.ContainsNaN()
		&& !ContactWorldNormal.ContainsNaN()
		&& !ContactWorldNormal.IsNearlyZero()
		&& FMath::IsFinite(SpillUnits)
		&& SpillUnits > 0.0f
		&& Trigger == EMD_TrashBinSpillTrigger::SustainedTilt;
}

AMD_TrashBin::AMD_TrashBin()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetReplicateMovement(true);

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);
	Mesh->SetCollisionProfileName(TEXT("PhysicsActor"));
	Mesh->SetCollisionObjectType(ECC_PhysicsBody);
	Mesh->SetCollisionResponseToChannel(MDCollisionChannels::InteractTrace, ECR_Block);
	Mesh->SetSimulatePhysics(true);
	Mesh->BodyInstance.bGenerateWakeEvents = true;
	Mesh->SetNotifyRigidBodyCollision(true);

	// 메시 교체로 충돌 반응이 바뀌지 않도록 자동 계산 대신 기획 수치로 고정
	Mesh->BodyInstance.SetMassOverride(40.f, true);

	// 손에서 놓은 뒤의 잔여 운동을 정리. 엔진 기본 AngularDamping 0은 계속 회전
	Mesh->SetLinearDamping(0.5f);
	Mesh->SetAngularDamping(1.f);

	GrabPoint = CreateDefaultSubobject<USceneComponent>(TEXT("GrabPoint"));
	GrabPoint->SetupAttachment(Mesh);
	// 임시 메시 기준 상단 중앙
	GrabPoint->SetRelativeLocation(FVector(0.f, 0.f, 80.f));

	SpillMouthOrigin = CreateDefaultSubobject<USceneComponent>(TEXT("SpillMouthOrigin"));
	SpillMouthOrigin->SetupAttachment(Mesh);
	SpillMouthOrigin->SetRelativeLocation(FVector(0.f, 0.f, 50.f));

	LiquidVisualRoot = CreateDefaultSubobject<USceneComponent>(
		TEXT("LiquidVisualRoot"));
	LiquidVisualRoot->SetupAttachment(Mesh);

	LiquidFillMesh = CreateDefaultSubobject<UStaticMeshComponent>(
		TEXT("LiquidFillMesh"));
	LiquidFillMesh->SetupAttachment(LiquidVisualRoot);
	LiquidFillMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	LiquidFillMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	LiquidFillMesh->SetGenerateOverlapEvents(false);
	LiquidFillMesh->SetCanEverAffectNavigation(false);
	LiquidFillMesh->SetCastShadow(false);
	LiquidFillMesh->SetReceivesDecals(false);
	LiquidFillMesh->SetHiddenInGame(true);
	LiquidFillMesh->SetRelativeLocation(FVector(0.0f, 0.0f, 0.75f));
	LiquidFillMesh->SetRelativeScale3D(FVector(0.68f, 0.68f, 0.015f));

	// 채움 메시·재질, spill Niagara·sound tag와 Wet class는 제품 Blueprint가
	// 명시적으로 소유한다. C++ CDO는 콘텐츠 에셋을 직접 찾지 않는다.

	// 회전만 캐릭터 기본값(1500/500)보다 완화해 큰 통을 든 무게감을 준다.
	HoldProfile.AngularStiffness = 500.f;
	HoldProfile.AngularDamping = 200.f;

	ContentsVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("ContentsVolume"));
	ContentsVolume->SetupAttachment(Mesh);
	ContentsVolume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	ContentsVolume->SetCollisionObjectType(ECC_WorldDynamic);
	ContentsVolume->SetCollisionResponseToAllChannels(ECR_Ignore);
	ContentsVolume->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Overlap);
	ContentsVolume->SetGenerateOverlapEvents(true);
	ContentsVolume->SetCanEverAffectNavigation(false);
}

FText AMD_TrashBin::GetInteractionPrompt_Implementation(AActor* Interactor) const
{
	static_cast<void>(Interactor);
	return NSLOCTEXT("MDInteraction", "TrashBinCarryPrompt", "[E] 들기");
}

void AMD_TrashBin::BeginPlay()
{
	Super::BeginPlay();
	if (UMD_TrashBinLiquidSloshVisualComponent::ShouldCreateForNetMode(
			GetNetMode()))
	{
		LiquidSloshVisualComponent =
			NewObject<UMD_TrashBinLiquidSloshVisualComponent>(
				this,
				TEXT("LiquidSloshVisualComponent"),
				RF_Transient);
		if (IsValid(LiquidSloshVisualComponent))
		{
			AddInstanceComponent(LiquidSloshVisualComponent);
			LiquidSloshVisualComponent->RegisterComponent();
			LiquidSloshVisualComponent->InitializeVisual(
				LiquidFillMesh,
				Mesh);
		}
	}
	RefreshLiquidVisual();
	Mesh->OnComponentWake.AddDynamic(this, &AMD_TrashBin::HandleSpillMeshWake);
	Mesh->OnComponentSleep.AddDynamic(this, &AMD_TrashBin::HandleSpillMeshSleep);

	if (!HasAuthority())
	{
		ContentsVolume->SetGenerateOverlapEvents(false);
		return;
	}

	EnsureStableBinId();
	RefreshSpillSamplingAfterLiquidMutation();

	ContentsVolume->OnComponentBeginOverlap.AddDynamic(this, &AMD_TrashBin::HandleBeginOverlap);
	ContentsVolume->OnComponentEndOverlap.AddDynamic(this, &AMD_TrashBin::HandleEndOverlap);

	// BeginPlay 이전부터 겹쳐 있던 액터도 최초 상태에 포함한다.
	RefreshContentsFromOverlaps();
}

void AMD_TrashBin::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopSpillSampleTimer();
	if (IsValid(Mesh))
	{
		Mesh->OnComponentWake.RemoveDynamic(this, &AMD_TrashBin::HandleSpillMeshWake);
		Mesh->OnComponentSleep.RemoveDynamic(this, &AMD_TrashBin::HandleSpillMeshSleep);
	}
	if (IsValid(LiquidSloshVisualComponent))
	{
		LiquidSloshVisualComponent->DestroyComponent();
		LiquidSloshVisualComponent = nullptr;
	}
	if (UWorld* const World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(
			SpillCosmeticRestoreTimerHandle);
	}

	if (IsValid(ContentsVolume))
	{
		ContentsVolume->OnComponentBeginOverlap.RemoveDynamic(this, &AMD_TrashBin::HandleBeginOverlap);
		ContentsVolume->OnComponentEndOverlap.RemoveDynamic(this, &AMD_TrashBin::HandleEndOverlap);
	}

	// 운반 중에 통이 사라지면 내용물이 물리가 꺼진 채로 남는다
	ReleaseContentsAfterCarry(/*bRefreshContents=*/false);

	if (UMD_TrashContentClaimSubsystem* ClaimSubsystem = GetContentClaimSubsystem())
	{
		TSet<TWeakObjectPtr<AActor>> RegisteredContents;
		for (const TPair<TWeakObjectPtr<AActor>, int32>& Pair : ContentOverlapCounts)
		{
			RegisteredContents.Add(Pair.Key);
		}
		RegisteredContents.Append(Contents);
		for (const TWeakObjectPtr<AActor>& ContentPtr : RegisteredContents)
		{
			if (AActor* Content = ContentPtr.Get())
			{
				ClaimSubsystem->UnregisterOverlap(Content, this);
			}
		}
	}

	for (const TWeakObjectPtr<AActor>& ContentPtr : Contents)
	{
		if (AActor* Content = ContentPtr.Get())
		{
			Content->OnDestroyed.RemoveDynamic(this, &AMD_TrashBin::HandleContentDestroyed);
		}
	}
	Contents.Reset();
	ContentOverlapCounts.Reset();

	for (const TPair<TWeakObjectPtr<AActor>, TEnumAsByte<ECollisionEnabled::Type>>& Pair : ClientCarryCollisionOverrides)
	{
		if (AActor* Content = Pair.Key.Get())
		{
			if (UPrimitiveComponent* ContentRoot = Cast<UPrimitiveComponent>(Content->GetRootComponent()))
			{
				ContentRoot->SetCollisionEnabled(Pair.Value);
			}
		}
	}
	ClientCarryCollisionOverrides.Reset();

	Super::EndPlay(EndPlayReason);
}

void AMD_TrashBin::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AMD_TrashBin, ContentCount);
	DOREPLIFETIME(AMD_TrashBin, LiquidState);
	DOREPLIFETIME(AMD_TrashBin, SpillCosmeticEpoch);
	DOREPLIFETIME(AMD_TrashBin, ReplicatedCarryFrozenContents);
}

float AMD_TrashBin::GetLiquidFillRatio() const
{
	if (!LiquidState.IsValid(MaximumLiquidUnits))
	{
		return 0.0f;
	}
	return static_cast<float>(FMath::Clamp(
		LiquidState.LiquidUnits / MaximumLiquidUnits,
		0.0,
		1.0));
}

void AMD_TrashBin::EnsureStableBinId()
{
	if (!HasAuthority() || StableBinId.IsValid())
	{
		return;
	}

	UWorld* const World = GetWorld();
	bool bIdAlreadyUsed = false;
	do
	{
		StableBinId = FGuid::NewGuid();
		bIdAlreadyUsed = !StableBinId.IsValid();
		if (IsValid(World) && !bIdAlreadyUsed)
		{
			for (TActorIterator<AMD_TrashBin> It(World); It; ++It)
			{
				if (*It != this && It->StableBinId == StableBinId)
				{
					bIdAlreadyUsed = true;
					break;
				}
			}
		}
	}
	while (bIdAlreadyUsed);
}

bool AMD_TrashBin::TryPreviewNextSpillSequence(
	uint64& OutSequence) const
{
	OutSequence = 0;
	if (!HasAuthority() || NextSpillSequence == MAX_uint64)
	{
		return false;
	}

	OutSequence = NextSpillSequence + 1;
	return true;
}

bool AMD_TrashBin::CommitNextSpillSequence(
	const uint64 PreparedSequence)
{
	uint64 ExpectedSequence = 0;
	if (!TryPreviewNextSpillSequence(ExpectedSequence)
		|| PreparedSequence != ExpectedSequence)
	{
		return false;
	}

	NextSpillSequence = PreparedSequence;
	return true;
}

void AMD_TrashBin::SetSourceTrashDispenser(AMD_TrashDispenser* SourceDispenser)
{
	// 서버의 유효한 보급기에 한정한 귀속 기록
	if (HasAuthority() == false || IsValid(SourceDispenser) == false)
	{
		// 해제되거나 비권위인 보급기 연결 차단
		return;
	}
	// 기존 귀속과 비교할 보급기 실제 이름
	FName NewSourceName = SourceDispenser->GetFName();
	// 재실행에서도 동일한 구역을 연결할 PIE 접두사 제거
	FName NewSourceLevel(UWorld::RemovePIEPrefix(SourceDispenser->GetLevel()->GetOutermost()->GetName()));
	// 다른 이름이나 다른 구역 보급기로의 임의 재귀속 방지
	if ((SourceTrashDispenserName.IsNone() == false && SourceTrashDispenserName != NewSourceName)
		|| (SourceTrashDispenserLevelPackage.IsNone() == false && SourceTrashDispenserLevelPackage != NewSourceLevel))
	{
		// 원래 소유권 보존
		return;
	}
	// 기존 이름 형식과 신규 기록의 공통 귀속 정보 설정
	SourceTrashDispenserName = NewSourceName;
	// 기존 파일의 미지정 구역도 확인된 보급기로 보완
	SourceTrashDispenserLevelPackage = NewSourceLevel;
}


void AMD_TrashBin::GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const
{
	TagContainer.Reset();

	if (PickupMethodTag.IsValid()
		&& PickupMethodTag.MatchesTag(MDGameplayTags::Interaction_Pickup_Physics))
	{
		TagContainer.AddTag(PickupMethodTag);
	}
}

UPrimitiveComponent* AMD_TrashBin::GetGrabComponent_Implementation() const
{
	return Mesh;
}

USceneComponent* AMD_TrashBin::GetGrabPoint_Implementation() const
{
	return GrabPoint;
}

FVector AMD_TrashBin::GetHoldOffset_Implementation() const
{
	return HoldOffset;
}

FMD_HoldProfile AMD_TrashBin::GetHoldProfile_Implementation() const
{
	return HoldProfile;
}

TArray<AActor*> AMD_TrashBin::GetContainedActors_Implementation() const
{
	TArray<AActor*> Snapshot;
	GetContentsSnapshot(Snapshot);
	return Snapshot;
}

bool AMD_TrashBin::TryDrainContentsForSorting_Implementation(TArray<AActor*>& OutContents)
{
	OutContents.Reset();
	if (!HasAuthority() || bDrainInProgress || bSortingTransactionActive || IsActorBeingDestroyed())
	{
		return false;
	}

	// Refresh가 overlap notify를 발생시켜도 중첩 drain은 먼저 세운 guard에서 거부한다.
	TGuardValue<bool> DrainGuard(bDrainInProgress, true);
	RefreshContentsFromOverlaps();
	UMD_TrashContentClaimSubsystem* ClaimSubsystem = GetContentClaimSubsystem();
	if (!ClaimSubsystem)
	{
		return false;
	}

	bSortingTransactionActive = true;

	if (IsValid(ContentsVolume))
	{
		ContentsVolume->SetGenerateOverlapEvents(false);
		ContentsVolume->OnComponentBeginOverlap.RemoveDynamic(this, &AMD_TrashBin::HandleBeginOverlap);
		ContentsVolume->OnComponentEndOverlap.RemoveDynamic(this, &AMD_TrashBin::HandleEndOverlap);
	}

	TArray<AActor*> CandidateContents;
	GetContentsSnapshot(CandidateContents);
	TSet<TWeakObjectPtr<AActor>> DrainedContentSet;
	for (AActor* Content : CandidateContents)
	{
		if (!IsValid(Content))
		{
			continue;
		}

		if (ClaimSubsystem->ConsumeForSorting(Content, this))
		{
			DrainedContentSet.Add(TWeakObjectPtr<AActor>(Content));
			OutContents.Add(Content);
		}
		else
		{
			ensureMsgf(false,
				TEXT("TrashBin '%s'의 내용물 '%s' 소유권이 drain 직전에 일치하지 않습니다."),
				*GetNameSafe(this), *GetNameSafe(Content));
		}

		// Sorter가 곧 파괴한다. 파괴 callback이 빈 컨테이너를 다시 채우지 않게 연결을 먼저 끊는다.
		Content->OnDestroyed.RemoveDynamic(this, &AMD_TrashBin::HandleContentDestroyed);
	}
	for (const TPair<TWeakObjectPtr<AActor>, int32>& Pair : ContentOverlapCounts)
	{
		if (!DrainedContentSet.Contains(Pair.Key))
		{
			if (AActor* Content = Pair.Key.Get())
			{
				ClaimSubsystem->UnregisterOverlap(Content, this);
			}
		}
	}

	Contents.Reset();
	ContentOverlapCounts.Reset();
	// Sorter가 같은 호출 스택에서 통을 파괴하므로 빈 통 알림/복제는 발생시키지 않는다.
	ContentCount = 0;
	return true;
}

void AMD_TrashBin::GetContentsSnapshot(TArray<AActor*>& OutContents) const
{
	OutContents.Reset();
	OutContents.Reserve(Contents.Num());
	for (const TWeakObjectPtr<AActor>& ContentPtr : Contents)
	{
		if (AActor* Content = ContentPtr.Get(); IsValid(Content))
		{
			OutContents.Add(Content);
		}
	}
}

void AMD_TrashBin::RefreshContentsFromOverlaps()
{
	if (!HasAuthority() || bSortingTransactionActive || !IsValid(ContentsVolume))
	{
		return;
	}

	// 이벤트 순서와 무관하게 현재 위치 기준 overlap cache를 먼저 갱신한다.
	ContentsVolume->UpdateOverlaps();
	RebuildContentsFromOverlaps();
}

void AMD_TrashBin::OnCarryStarted_Implementation()
{
	bCarryAccelerationActive = true;
	FreezeContentsForCarry();
	if (HasAuthority())
	{
		EnsureSpillSampleTimer();
	}
}

void AMD_TrashBin::OnCarrySettled_Implementation()
{
	ReleaseContentsAfterCarry();
}

void AMD_TrashBin::OnCarryEnded_Implementation()
{
	ReleaseContentsAfterCarry();
	if (HasAuthority())
	{
		EnsureSpillSampleTimer();
	}
}

void AMD_TrashBin::FreezeContentsForCarry()
{
	if (!HasAuthority() || bSortingTransactionActive || !IsValid(Mesh)) return;

	// 중복 lifecycle 통지가 와도 같은 액터를 두 번 attach하지 않는다.
	for (const TWeakObjectPtr<AActor>& ContentPtr : Contents)
	{
		FreezeContentForCarry(ContentPtr.Get());
	}
}

bool AMD_TrashBin::FreezeContentForCarry(AActor* Content)
{
	if (!HasAuthority() || !bCarryAccelerationActive || bSortingTransactionActive
		|| !IsValid(Content) || !IsValid(Mesh))
	{
		return false;
	}

	const TWeakObjectPtr<AActor> ContentPtr(Content);
	if (CarryFrozenContents.ContainsByPredicate([ContentPtr](const FMD_CarryFrozenContent& Frozen)
		{
			return Frozen.Actor == ContentPtr;
		}))
	{
		return true;
	}

	UPrimitiveComponent* ContentRoot = Cast<UPrimitiveComponent>(Content->GetRootComponent());
	if (!IsValid(ContentRoot) || !ContentRoot->IsSimulatingPhysics())
	{
		return false;
	}

	FMD_CarryFrozenContent& Frozen = CarryFrozenContents.AddDefaulted_GetRef();
	Frozen.Actor = ContentPtr;
	Frozen.PreviousCollisionEnabled = ContentRoot->GetCollisionEnabled();

	ContentRoot->SetSimulatePhysics(false);
	ContentRoot->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Content->AttachToComponent(Mesh, FAttachmentTransformRules::KeepWorldTransform);

	ReplicatedCarryFrozenContents.AddUnique(Content);
	ForceNetUpdate();
	return true;
}

void AMD_TrashBin::ReleaseContentsAfterCarry(const bool bRefreshContents)
{
	bCarryAccelerationActive = false;
	if (!HasAuthority())
	{
		ReplicatedCarryFrozenContents.Reset();
		ApplyReplicatedCarryCollisionState();
		return;
	}

	for (const FMD_CarryFrozenContent& Frozen : CarryFrozenContents)
	{
		AActor* Content = Frozen.Actor.Get();
		if (!IsValid(Content)) continue;

		Content->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);

		if (UPrimitiveComponent* ContentRoot = Cast<UPrimitiveComponent>(Content->GetRootComponent()))
		{
			// 충돌이 먼저다. 물리가 먼저 켜지면 그 프레임에 통 바닥을 뚫는다
			ContentRoot->SetCollisionEnabled(Frozen.PreviousCollisionEnabled);
			ContentRoot->SetSimulatePhysics(true);
		}
	}

	CarryFrozenContents.Reset();
	ReplicatedCarryFrozenContents.Reset();
	ForceNetUpdate();

	// 붙어 있는 동안 오버랩이 흔들렸을 수 있다. drain 중이면 새 입고를 재구성하지 않는다.
	if (bRefreshContents && !bSortingTransactionActive)
	{
		RefreshContentsFromOverlaps();
	}
}

void AMD_TrashBin::UnfreezeContentForCarry(AActor* Content)
{
	if (!HasAuthority() || !Content)
	{
		return;
	}

	const TWeakObjectPtr<AActor> ContentPtr(Content);
	const int32 FrozenIndex = CarryFrozenContents.IndexOfByPredicate([ContentPtr](const FMD_CarryFrozenContent& Frozen)
		{
			return Frozen.Actor == ContentPtr;
		});
	if (FrozenIndex == INDEX_NONE)
	{
		return;
	}

	const FMD_CarryFrozenContent Frozen = CarryFrozenContents[FrozenIndex];
	if (IsValid(Content))
	{
		Content->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		if (UPrimitiveComponent* ContentRoot = Cast<UPrimitiveComponent>(Content->GetRootComponent()))
		{
			ContentRoot->SetCollisionEnabled(Frozen.PreviousCollisionEnabled);
			ContentRoot->SetSimulatePhysics(true);
		}
	}

	CarryFrozenContents.RemoveAtSwap(FrozenIndex);
	ReplicatedCarryFrozenContents.Remove(Content);
	ForceNetUpdate();
}

void AMD_TrashBin::ApplyReplicatedCarryCollisionState()
{
	if (HasAuthority())
	{
		return;
	}

	TSet<TWeakObjectPtr<AActor>> DesiredContents;
	DesiredContents.Reserve(ReplicatedCarryFrozenContents.Num());
	for (AActor* Content : ReplicatedCarryFrozenContents)
	{
		if (IsValid(Content))
		{
			DesiredContents.Add(TWeakObjectPtr<AActor>(Content));
		}
	}

	for (auto OverrideIt = ClientCarryCollisionOverrides.CreateIterator(); OverrideIt; ++OverrideIt)
	{
		if (DesiredContents.Contains(OverrideIt.Key()))
		{
			continue;
		}

		if (AActor* Content = OverrideIt.Key().Get())
		{
			if (UPrimitiveComponent* ContentRoot =
				Cast<UPrimitiveComponent>(Content->GetRootComponent()))
			{
				ContentRoot->SetCollisionEnabled(OverrideIt.Value());
			}
		}
		OverrideIt.RemoveCurrent();
	}

	for (const TWeakObjectPtr<AActor>& ContentPtr : DesiredContents)
	{
		if (ClientCarryCollisionOverrides.Contains(ContentPtr))
		{
			continue;
		}

		AActor* Content = ContentPtr.Get();
		UPrimitiveComponent* ContentRoot = IsValid(Content) ? Cast<UPrimitiveComponent>(Content->GetRootComponent()): nullptr;
		if (!IsValid(ContentRoot))
		{
			continue;
		}

		ClientCarryCollisionOverrides.Add(ContentPtr, ContentRoot->GetCollisionEnabled());
		ContentRoot->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	}
}

void AMD_TrashBin::RemoveFrozenContentReferences(AActor* Content)
{
	if (!Content)
	{
		return;
	}

	const TWeakObjectPtr<AActor> ContentPtr(Content);
	CarryFrozenContents.RemoveAll(
		[ContentPtr](const FMD_CarryFrozenContent& Frozen)
		{
			return Frozen.Actor == ContentPtr;
		});
	if (ReplicatedCarryFrozenContents.Remove(Content) > 0)
	{
		ForceNetUpdate();
	}
}

void AMD_TrashBin::SetInteractionHighlighted_Implementation(bool bHighlighted)
{
	if (IsValid(Mesh))
	{
		Mesh->SetCustomDepthStencilValue(IMD_Interactable::HighlightStencilValue);
		Mesh->SetRenderCustomDepth(bHighlighted);
	}
}

void AMD_TrashBin::HandleBeginOverlap(UPrimitiveComponent* /*OverlappedComponent*/,	AActor* OtherActor,	UPrimitiveComponent* /*OtherComponent*/, int32 /*OtherBodyIndex*/, bool /*bFromSweep*/,	const FHitResult& /*SweepResult*/)
{
	if (!HasAuthority() || bSortingTransactionActive || !IsAcceptedContent(OtherActor))
	{
		return;
	}

	const TWeakObjectPtr<AActor> ContentPtr(OtherActor);
	int32& OverlapCount = ContentOverlapCounts.FindOrAdd(ContentPtr);
	const bool bFirstComponentOverlap = OverlapCount == 0;
	++OverlapCount;

	UMD_TrashContentClaimSubsystem* ClaimSubsystem = GetContentClaimSubsystem();
	const bool bOwnsClaim = bFirstComponentOverlap? ClaimSubsystem && ClaimSubsystem->RegisterOverlap(OtherActor, this)	: ClaimSubsystem && ClaimSubsystem->IsOwnedBy(OtherActor, this);
	if (bOwnsClaim && TryAdmitContent(OtherActor))
	{
		if (bCarryAccelerationActive)
		{
			FreezeContentForCarry(OtherActor);
		}
		SyncContentCount();
	}
}

void AMD_TrashBin::HandleEndOverlap(UPrimitiveComponent* /*OverlappedComponent*/, AActor* OtherActor, UPrimitiveComponent* /*OtherComponent*/, int32 /*OtherBodyIndex*/)
{
	if (!HasAuthority() || bSortingTransactionActive || !OtherActor)
	{
		return;
	}

	const TWeakObjectPtr<AActor> ContentPtr(OtherActor);
	int32* OverlapCount = ContentOverlapCounts.Find(ContentPtr);
	if (!OverlapCount)
	{
		return;
	}

	--(*OverlapCount);
	if (*OverlapCount > 0)
	{
		return;
	}

	ContentOverlapCounts.Remove(ContentPtr);
	const bool bRemoved = RemoveAdmittedContent(OtherActor);
	if (UMD_TrashContentClaimSubsystem* ClaimSubsystem = GetContentClaimSubsystem())
	{
		ClaimSubsystem->UnregisterOverlap(OtherActor, this);
	}
	if (bRemoved)
	{
		SyncContentCount();
	}
}

void AMD_TrashBin::HandleContentDestroyed(AActor* DestroyedActor)
{
	if (!DestroyedActor) return;

	const TWeakObjectPtr<AActor> ContentPtr(DestroyedActor);
	ContentOverlapCounts.Remove(ContentPtr);
	RemoveFrozenContentReferences(DestroyedActor);
	if (Contents.Remove(ContentPtr) > 0)
	{
		SyncContentCount();
	}
}

bool AMD_TrashBin::IsAcceptedContent(const AActor* Actor) const
{
	// 해당 액터가 쓰레기에 해당하는 지 검토

	if (!IsValid(Actor) || AcceptedContentTags.IsEmpty())
	{
		return false;
	}

	const IGameplayTagAssetInterface* TagAsset = Cast<IGameplayTagAssetInterface>(Actor);
	if (!TagAsset)
	{
		return false;
	}

	FGameplayTagContainer ActorTags;
	TagAsset->GetOwnedGameplayTags(ActorTags);

	for (const FGameplayTag& AcceptedTag : AcceptedContentTags)
	{
		if (AcceptedTag.MatchesTag(MDGameplayTags::Resource_Storage_Content)	&& ActorTags.HasTag(AcceptedTag))
		{
			return true;
		}
	}

	return false;
}

bool AMD_TrashBin::TryAdmitContent(AActor* Actor)
{
	UMD_TrashContentClaimSubsystem* ClaimSubsystem = GetContentClaimSubsystem();
	if (!IsAcceptedContent(Actor) || Contents.Contains(TWeakObjectPtr<AActor>(Actor)) || !ClaimSubsystem	|| !ClaimSubsystem->IsOwnedBy(Actor, this))
	{
		return false;
	}

	const TWeakObjectPtr<AActor> ContentPtr(Actor);
	Contents.Add(ContentPtr);
	Actor->OnDestroyed.AddUniqueDynamic(this, &AMD_TrashBin::HandleContentDestroyed);
	return true;
}

bool AMD_TrashBin::RemoveAdmittedContent(AActor* Actor)
{
	if (!Actor || Contents.Remove(TWeakObjectPtr<AActor>(Actor)) == 0)
	{
		return false;
	}

	UnfreezeContentForCarry(Actor);
	Actor->OnDestroyed.RemoveDynamic(this, &AMD_TrashBin::HandleContentDestroyed);
	return true;
}

void AMD_TrashBin::RebuildContentsFromOverlaps()
{
	if (!HasAuthority() || bSortingTransactionActive || !IsValid(ContentsVolume))
	{
		return;
	}

	TArray<UPrimitiveComponent*> OverlappingComponents;
	ContentsVolume->GetOverlappingComponents(OverlappingComponents);

	TMap<TWeakObjectPtr<AActor>, int32> NewOverlapCounts;
	for (UPrimitiveComponent* OverlappingComponent : OverlappingComponents)
	{
		AActor* OverlappingActor = IsValid(OverlappingComponent) ? OverlappingComponent->GetOwner()	: nullptr;
		if (OverlappingActor != this && IsAcceptedContent(OverlappingActor))
		{
			++NewOverlapCounts.FindOrAdd(TWeakObjectPtr<AActor>(OverlappingActor));
		}
	}

	TMap<TWeakObjectPtr<AActor>, int32> PreviousOverlapCounts = MoveTemp(ContentOverlapCounts);
	ContentOverlapCounts = MoveTemp(NewOverlapCounts);
	UMD_TrashContentClaimSubsystem* ClaimSubsystem = GetContentClaimSubsystem();
	if (ClaimSubsystem)
	{
		for (const TPair<TWeakObjectPtr<AActor>, int32>& Pair : PreviousOverlapCounts)
		{
			if (!ContentOverlapCounts.Contains(Pair.Key))
			{
				if (AActor* Content = Pair.Key.Get())
				{
					RemoveAdmittedContent(Content);
					ClaimSubsystem->UnregisterOverlap(Content, this);
				}
			}
		}
		for (const TPair<TWeakObjectPtr<AActor>, int32>& Pair : ContentOverlapCounts)
		{
			if (AActor* Content = Pair.Key.Get())
			{
				ClaimSubsystem->RegisterOverlap(Content, this);
			}
		}
	}

	TSet<TWeakObjectPtr<AActor>> DesiredContents;
	DesiredContents.Reserve(ContentOverlapCounts.Num());
	for (const TPair<TWeakObjectPtr<AActor>, int32>& Pair : ContentOverlapCounts)
	{
		if (AActor* Content = Pair.Key.Get(); ClaimSubsystem && IsValid(Content) && ClaimSubsystem->IsOwnedBy(Content, this))
		{
			DesiredContents.Add(Pair.Key);
		}
	}

	for (auto ContentIt = Contents.CreateIterator(); ContentIt; ++ContentIt)
	{
		if (DesiredContents.Contains(*ContentIt))
		{
			continue;
		}
		if (AActor* Content = ContentIt->Get())
		{
			UnfreezeContentForCarry(Content);
			Content->OnDestroyed.RemoveDynamic(this, &AMD_TrashBin::HandleContentDestroyed);
		}
		ContentIt.RemoveCurrent();
	}

	for (const TWeakObjectPtr<AActor>& ContentPtr : DesiredContents)
	{
		if (Contents.Contains(ContentPtr)) continue;
		if (AActor* Content = ContentPtr.Get())
		{
			if (TryAdmitContent(Content) && bCarryAccelerationActive)
			{
				FreezeContentForCarry(Content);
			}
		}
	}

	SyncContentCount();
}

void AMD_TrashBin::SyncContentCount()
{
	const int32 NewCount = Contents.Num();
	if (ContentCount == NewCount) return;
	if (NewCount > ContentCount) MDAudioFeedback::World(this, TEXT("SFX.Carry.Bin.Insert"));
	ContentCount = NewCount;
	OnContentsChanged.Broadcast(ContentCount);
	ForceNetUpdate();
}

void AMD_TrashBin::OnRep_ContentCount(int32 PreviousCount)
{
	if (HasActorBegunPlay() && ContentCount > PreviousCount) MDAudioFeedback::World(this, TEXT("SFX.Carry.Bin.Insert"));
	OnContentsChanged.Broadcast(ContentCount);
}

void AMD_TrashBin::OnRep_LiquidState()
{
	RefreshLiquidVisual();
	OnLiquidChanged.Broadcast(
		LiquidState.LiquidUnits,
		LiquidState.RewardUnits);
}

void AMD_TrashBin::OnRep_SpillCosmeticEpoch()
{
	if (HasAuthority())
	{
		return;
	}
	if (UWorld* const World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(
			SpillCosmeticRestoreTimerHandle);
		SpillCosmeticRestoreTimerHandle =
			World->GetTimerManager().SetTimerForNextTick(
				this,
				&AMD_TrashBin::ResetSpillCosmeticAfterRestoreEpoch);
	}
}

void AMD_TrashBin::ResetSpillCosmeticAfterRestoreEpoch()
{
	LastConsumedSpillCosmeticStableBinId.Invalidate();
	LastConsumedSpillCosmeticSequence = 0;
	LastRenderedLiquidRevision = INDEX_NONE;
	RefreshLiquidVisual();
}

void AMD_TrashBin::OnRep_ReplicatedMovement()
{
	Super::OnRep_ReplicatedMovement();
	if (!IsValid(LiquidSloshVisualComponent))
	{
		return;
	}

	const FRepMovement& Movement = GetReplicatedMovement();
	LiquidSloshVisualComponent->NotifyReplicatedMotion(
		Movement.LinearVelocity,
		ConvertReplicatedAngularVelocityToRadians(Movement.AngularVelocity),
		!Movement.bSimulatedPhysicSleep);
}

FVector AMD_TrashBin::ConvertReplicatedAngularVelocityToRadians(
	const FVector& AngularVelocityDegreesPerSecond)
{
	// FRepMovement serializes degrees per second; Chaos and the slosh driver use radians.
	return AngularVelocityDegreesPerSecond * (UE_PI / 180.0);
}

void AMD_TrashBin::NotifyLiquidStateChanged()
{
	RefreshLiquidVisual();
	RefreshSpillSamplingAfterLiquidMutation();
	OnLiquidChanged.Broadcast(
		LiquidState.LiquidUnits,
		LiquidState.RewardUnits);
	ForceNetUpdate();
}

void AMD_TrashBin::RefreshLiquidVisual()
{
	if (!IsValid(LiquidFillMesh))
	{
		return;
	}

	if (LastRenderedLiquidRevision != INDEX_NONE
		&& LiquidState.Revision < LastRenderedLiquidRevision)
	{
		LastConsumedSpillCosmeticStableBinId.Invalidate();
		LastConsumedSpillCosmeticSequence = 0;
	}
	LastRenderedLiquidRevision = LiquidState.Revision;

	const float FillRatio = GetLiquidFillRatio();
	const float SafeFullHeightCm = FMath::Max(1.0f, LiquidVisualFullHeightCm);
	if (FillRatio <= 0.0f)
	{
		LiquidFillMesh->SetHiddenInGame(true);
		if (IsValid(LiquidSloshVisualComponent))
		{
			LiquidSloshVisualComponent->SetLiquidVisualState(
				/*InFillRatio=*/0.0f,
				/*InVisibleHeightCm=*/0.0f,
				SafeFullHeightCm);
		}
		return;
	}

	const float SafeRadiusCm = FMath::Max(1.0f, LiquidVisualRadiusCm);
	const float SafeMinimumHeightCm = FMath::Clamp(
		LiquidVisualMinimumHeightCm,
		0.1f,
		SafeFullHeightCm);
	const float HeightCm = FMath::Max(
		SafeMinimumHeightCm,
		SafeFullHeightCm * FillRatio);
	const float DiameterScale = (SafeRadiusCm * 2.0f) / 100.0f;

	FVector RelativeLocation = LiquidFillMesh->GetRelativeLocation();
	RelativeLocation.Z = HeightCm * 0.5f;
	LiquidFillMesh->SetRelativeLocation(RelativeLocation);
	LiquidFillMesh->SetRelativeScale3D(FVector(
		DiameterScale,
		DiameterScale,
		HeightCm / 100.0f));
	LiquidFillMesh->SetHiddenInGame(false);
	if (IsValid(LiquidSloshVisualComponent))
	{
		LiquidSloshVisualComponent->SetLiquidVisualState(
			FillRatio,
			HeightCm,
			SafeFullHeightCm);
	}
}

void AMD_TrashBin::OnRep_CarryFrozenContents()
{
	ApplyReplicatedCarryCollisionState();
}

UMD_TrashContentClaimSubsystem* AMD_TrashBin::GetContentClaimSubsystem() const
{
	UWorld* World = GetWorld();
	return IsValid(World) ? World->GetSubsystem<UMD_TrashContentClaimSubsystem>() : nullptr;
}

bool AMD_TrashBin::CanAcceptContentClaim(AActor* Content) const
{
	if (!HasAuthority() || bSortingTransactionActive || IsActorBeingDestroyed()
		|| !IsAcceptedContent(Content))
	{
		return false;
	}

	const int32* OverlapCount = ContentOverlapCounts.Find(TWeakObjectPtr<AActor>(Content));
	return OverlapCount && *OverlapCount > 0;
}

bool AMD_TrashBin::TryApplyGrantedContentClaim(AActor* Content)
{
	UMD_TrashContentClaimSubsystem* ClaimSubsystem = GetContentClaimSubsystem();
	if (!CanAcceptContentClaim(Content)	|| !ClaimSubsystem || !ClaimSubsystem->IsOwnedBy(Content, this))
	{
		return false;
	}

	const TWeakObjectPtr<AActor> ContentPtr(Content);
	if (Contents.Contains(ContentPtr))
	{
		return true;
	}
	if (!TryAdmitContent(Content))
	{
		return false;
	}

	if (bCarryAccelerationActive)
	{
		FreezeContentForCarry(Content);
	}
	SyncContentCount();
	return true;
}
