#include "World/Contamination/MD_WetContaminationPatch.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

namespace MD_WetContaminationPatchPrivate
{
	constexpr double LedgerTolerance = 1.0e-9;

	uint32 MixVisualHash(uint32 Value)
	{
		Value ^= Value >> 16;
		Value *= 0x7FEB352Du;
		Value ^= Value >> 15;
		Value *= 0x846CA68Bu;
		Value ^= Value >> 16;
		return Value;
	}

	uint64 MakePreparationNonce(const FGuid& PatchId)
	{
		const uint64 High =
			(static_cast<uint64>(PatchId.A) << 32) | PatchId.B;
		const uint64 Low =
			(static_cast<uint64>(PatchId.C) << 32) | PatchId.D;
		const uint64 Nonce = High ^ Low;
		return Nonce == 0 ? 1 : Nonce;
	}

	bool AreLotsIdentical(
		const TArray<FMD_ScrapeRecoveryLot>& A,
		const TArray<FMD_ScrapeRecoveryLot>& B)
	{
		if (A.Num() != B.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < A.Num(); ++Index)
		{
			if (A[Index].SourceId != B[Index].SourceId
				|| A[Index].RewardClass != B[Index].RewardClass
				|| A[Index].CarrierAmount != B[Index].CarrierAmount
				|| A[Index].RewardUnits != B[Index].RewardUnits)
			{
				return false;
			}
		}
		return true;
	}

	bool AreCandidatesIdentical(
		const FMD_TrashBinSpillCandidate& A,
		const FMD_TrashBinSpillCandidate& B)
	{
		return A.TransactionId == B.TransactionId
			&& A.Trigger == B.Trigger
			&& A.ExpectedBinRevision == B.ExpectedBinRevision
			&& A.Payload.LiquidUnits == B.Payload.LiquidUnits
			&& A.Payload.RewardUnits == B.Payload.RewardUnits
			&& AreLotsIdentical(A.Payload.RecoveryLots, B.Payload.RecoveryLots);
	}
}

bool FMD_WetContaminationVisualDescriptor::IsValid() const
{
	return MeshVariantIndex < VariantCount
		&& YawStep < YawStepCount
		&& StretchPreset < StretchPresetCount;
}

float FMD_WetContaminationVisualDescriptor::GetYawDegrees() const
{
	return IsValid() ? static_cast<float>(YawStep) * YawStepDegrees : 0.0f;
}

FVector2D FMD_WetContaminationVisualDescriptor::GetStretchScale() const
{
	// 각 쌍의 곱을 약 1로 유지해 형상 선택이 액체량의 보이는 면적을 바꾸지 않는다.
	if (!IsValid())
	{
		return FVector2D::UnitVector;
	}

	switch (StretchPreset)
	{
	case 1:
		return FVector2D(1.1, 0.9091);
	case 2:
		return FVector2D(0.9091, 1.1);
	case 3:
		return FVector2D(1.2, 0.8333);
	case 4:
		return FVector2D(0.8333, 1.2);
	default:
		return FVector2D::UnitVector;
	}
}

float FMD_WetContaminationVisualDescriptor::CalculateAmountBaseScale(
	const double LiquidUnits)
{
	const double SafeLiquidUnits = FMath::IsFinite(LiquidUnits)
		? FMath::Max(0.0, LiquidUnits)
		: 0.0;
	const double PulseRatio = SafeLiquidUnits / 0.2;
	return FMath::Clamp(
		0.45f * FMath::Sqrt(static_cast<float>(PulseRatio)),
		0.35f,
		0.90f);
}

FVector FMD_WetContaminationVisualDescriptor::MakeRelativeScale(
	const double LiquidUnits) const
{
	const FVector2D Stretch = GetStretchScale();
	const float BaseScale = CalculateAmountBaseScale(LiquidUnits);
	// UE가 component determinant로 reverse culling하도록 mirror는 X 부호만 뒤집는다.
	const double MirrorSign = bMirrorX ? -1.0 : 1.0;
	return FVector(
		MirrorSign * BaseScale * Stretch.X,
		BaseScale * Stretch.Y,
		1.0);
}

FTransform FMD_WetContaminationVisualDescriptor::MakeRelativeTransform(
	const double LiquidUnits) const
{
	return FTransform(
		FQuat(FRotator(0.0f, GetYawDegrees(), 0.0f)),
		FVector(0.0, 0.0, SurfaceOffsetCm),
		MakeRelativeScale(LiquidUnits));
}

FMD_WetContaminationVisualDescriptor
FMD_WetContaminationVisualDescriptor::FromPatchId(const FGuid& PatchId)
{
	uint32 Hash = 0xA511E9B3u;
	Hash = MD_WetContaminationPatchPrivate::MixVisualHash(Hash ^ PatchId.A);
	Hash = MD_WetContaminationPatchPrivate::MixVisualHash(Hash ^ PatchId.B);
	Hash = MD_WetContaminationPatchPrivate::MixVisualHash(Hash ^ PatchId.C);
	Hash = MD_WetContaminationPatchPrivate::MixVisualHash(Hash ^ PatchId.D);

	FMD_WetContaminationVisualDescriptor Descriptor;
	Descriptor.MeshVariantIndex = static_cast<uint8>(Hash & 0x3u);
	Descriptor.YawStep = static_cast<uint8>((Hash >> 2) & 0x7u);
	Descriptor.StretchPreset = static_cast<uint8>(((Hash >> 5) & 0x7u) % 5u);
	Descriptor.bMirrorX = (Hash & 0x100u) != 0;
	return Descriptor;
}

UMD_WetScrapeSurfaceComponent::UMD_WetScrapeSurfaceComponent()
{
	bContaminationEnabled = false;
	InitialContaminationPerCell = 0.0f;
	bUseProceduralContamination = false;
	ContaminationItemsPerFace = 0;
	bShowContaminationDecals = false;
}

bool FMD_WetContaminationPatchState::IsValid() const
{
	return PatchId.IsValid()
		&& FMath::IsFinite(LiquidUnits)
		&& LiquidUnits > 0.0
		&& FMath::IsFinite(RewardUnits)
		&& RewardUnits >= 0.0
		&& RewardUnits <= LiquidUnits
			+ MD_WetContaminationPatchPrivate::LedgerTolerance
		&& Revision > 0;
}

AMD_WetContaminationPatch::AMD_WetContaminationPatch()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicateMovement(false);

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	VisualMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WetVisual"));
	VisualMesh->SetupAttachment(SceneRoot);
	VisualMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	VisualMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	VisualMesh->SetGenerateOverlapEvents(false);
	VisualMesh->SetCanEverAffectNavigation(false);
	VisualMesh->SetCastShadow(false);
	VisualMesh->SetReceivesDecals(false);
	VisualMesh->PrimaryComponentTick.bCanEverTick = false;
	VisualMesh->SetComponentTickEnabled(false);
	VisualMesh->SetHiddenInGame(true);
	VisualMesh->SetRelativeLocation(FVector(
		0.0,
		0.0,
		FMD_WetContaminationVisualDescriptor::SurfaceOffsetCm));
	VisualMesh->SetRelativeScale3D(FVector(0.35, 0.35, 1.0));

	// 인덱스는 PatchId hash와 저장 호환 계약이다. 기존 경로를 재정렬하지 않는다.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> WetMesh01Finder(
		TEXT("/Game/Assets/Contamination/Wet/SM_MD_WetPatch_01.SM_MD_WetPatch_01"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> WetMesh02Finder(
		TEXT("/Game/Assets/Contamination/Wet/SM_MD_WetPatch_02.SM_MD_WetPatch_02"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> WetMesh03Finder(
		TEXT("/Game/Assets/Contamination/Wet/SM_MD_WetPatch_03.SM_MD_WetPatch_03"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> WetMesh04Finder(
		TEXT("/Game/Assets/Contamination/Wet/SM_MD_WetPatch_04.SM_MD_WetPatch_04"));
	VisualVariantMeshes.Reserve(
		FMD_WetContaminationVisualDescriptor::VariantCount);
	VisualVariantMeshes.Add(WetMesh01Finder.Object);
	VisualVariantMeshes.Add(WetMesh02Finder.Object);
	VisualVariantMeshes.Add(WetMesh03Finder.Object);
	VisualVariantMeshes.Add(WetMesh04Finder.Object);
	if (WetMesh01Finder.Succeeded())
	{
		VisualMesh->SetStaticMesh(WetMesh01Finder.Object);
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> WetMaterialFinder(
		TEXT("/Game/Materials/MI_MD_TrashBinSludge.MI_MD_TrashBinSludge"));
	if (WetMaterialFinder.Succeeded())
	{
		VisualMesh->SetMaterial(0, WetMaterialFinder.Object);
	}

	ScrapeSurface = CreateDefaultSubobject<UMD_WetScrapeSurfaceComponent>(
		TEXT("WetScrapeSurface"));
	ScrapeSurface->ConfigureExternalComponents(VisualMesh, nullptr, nullptr);
}

void AMD_WetContaminationPatch::BeginPlay()
{
	Super::BeginPlay();
	ScrapeSurface->ConfigureExternalComponents(VisualMesh, nullptr, nullptr);
	RefreshSupportAttachment();
	RefreshVisual();
}

void AMD_WetContaminationPatch::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMD_WetContaminationPatch, PatchState);
	DOREPLIFETIME(AMD_WetContaminationPatch, SupportState);
}

bool AMD_WetContaminationPatch::PrepareForSpill(
	const FMD_TrashBinSpillCandidate& Candidate,
	const FMD_TrashBinSpillContact& Contact,
	UPrimitiveComponent* const SupportComponent,
	FMD_TrashBinSpillPreparedSinkToken& OutToken)
{
	OutToken = FMD_TrashBinSpillPreparedSinkToken();
	if (!HasAuthority()
		|| bPrepared
		|| bCommitted
		|| bMutationInProgress
		|| !Candidate.IsValid()
		|| !Contact.IsValid()
		|| !IsValid(SupportComponent)
		|| !IsValid(SupportComponent->GetOwner())
		|| FMD_TrashBinSpillContact::CanonicalizeSupportActorPath(
			FSoftObjectPath(SupportComponent->GetOwner()))
			!= Contact.SupportActorPath
		|| SupportComponent->GetFName() != Contact.SupportComponentName)
	{
		return false;
	}
	const FTransform AnchoredWorldTransform =
		Contact.SupportLocalAnchor * SupportComponent->GetComponentTransform();
	const FVector AnchoredNormal = AnchoredWorldTransform.GetUnitAxis(EAxis::Z);
	if (!AnchoredWorldTransform.GetLocation().Equals(
			Contact.WorldLocation,
			1.0)
		|| FVector::DotProduct(AnchoredNormal, Contact.WorldNormal) < 0.999f)
	{
		return false;
	}

	SupportState.Actor = SupportComponent->GetOwner();
	SupportState.ComponentName = SupportComponent->GetFName();
	SupportState.LocalAnchor = Contact.SupportLocalAnchor;
	if (!AttachToComponent(
		SupportComponent,
		FAttachmentTransformRules::KeepRelativeTransform))
	{
		SupportState = FMD_WetContaminationSupportState();
		return false;
	}
	SetActorRelativeTransform(SupportState.LocalAnchor);
	PreparedSupportComponent = SupportComponent;

	PreparedCandidate = Candidate;
	PreparedToken.SinkId = FGuid::NewGuid();
	PreparedToken.PreparationNonce =
		MD_WetContaminationPatchPrivate::MakePreparationNonce(
			PreparedToken.SinkId);
	PreparedToken.TransactionId = Candidate.TransactionId;
	PreparedToken.ExpectedBinRevision = Candidate.ExpectedBinRevision;
	PreparedToken.PreparedLiquidUnits = Candidate.Payload.LiquidUnits;
	PreparedToken.PreparedRewardUnits = Candidate.Payload.RewardUnits;
	bPrepared = PreparedToken.IsValid();
	if (!bPrepared)
	{
		PreparedCandidate = FMD_TrashBinSpillCandidate();
		PreparedToken = FMD_TrashBinSpillPreparedSinkToken();
		return false;
	}

	OutToken = PreparedToken;
	return true;
}

bool AMD_WetContaminationPatch::CanCommitPreparedSpill(
	const FMD_TrashBinSpillCandidate& Candidate,
	const FMD_TrashBinSpillPreparedSinkToken& Token) const
{
	return HasAuthority()
		&& bPrepared
		&& !bCommitted
		&& !bMutationInProgress
		&& Candidate.IsValid()
		&& Token.IsValid()
		&& PreparedToken.SinkId == Token.SinkId
		&& PreparedToken.PreparationNonce == Token.PreparationNonce
		&& PreparedToken.Matches(Candidate)
		&& Token.Matches(Candidate)
		&& MD_WetContaminationPatchPrivate::AreCandidatesIdentical(
			PreparedCandidate,
			Candidate)
		&& DedupeState.WouldAccept(Candidate.TransactionId)
		&& SupportState.IsValid()
		&& IsValid(PreparedSupportComponent.Get())
		&& ResolveSupportComponent() == PreparedSupportComponent.Get()
		&& GetRootComponent()->GetAttachParent() == PreparedSupportComponent.Get();
}

void AMD_WetContaminationPatch::ApplyPreparedSpillNoNotify(
	const FMD_TrashBinSpillCandidate& Candidate,
	const FMD_TrashBinSpillPreparedSinkToken& Token)
{
	check(CanCommitPreparedSpill(Candidate, Token));
	TGuardValue<bool> MutationGuard(bMutationInProgress, true);

	const bool bAccepted = DedupeState.TryAccept(Candidate.TransactionId);
	check(bAccepted);

	PatchState.PatchId = Token.SinkId;
	PatchState.LiquidUnits = Candidate.Payload.LiquidUnits;
	PatchState.RewardUnits = Candidate.Payload.RewardUnits;
	PatchState.Revision = 1;
	LastTransactionId = Candidate.TransactionId;
	RecoveryLots = Candidate.Payload.RecoveryLots;

	bPrepared = false;
	bCommitted = true;
	bCommitFinalized = false;
	PreparedCandidate = FMD_TrashBinSpillCandidate();
	PreparedToken = FMD_TrashBinSpillPreparedSinkToken();
	PreparedSupportComponent.Reset();
	// 통 원장과 sequence 대입이 끝날 때까지 숨김/복제 알림을 유지한다.
}

void AMD_WetContaminationPatch::FinalizeCommittedSpill()
{
	if (!HasAuthority()
		|| !bCommitted
		|| bPrepared
		|| bCommitFinalized
		|| !PatchState.IsValid()
		|| !LastTransactionId.IsValid())
	{
		return;
	}

	bCommitFinalized = true;
	RefreshVisual();
	ForceNetUpdate();
}

void AMD_WetContaminationPatch::AbortPreparedSpill()
{
	if (!bCommitted && !IsActorBeingDestroyed())
	{
		Destroy();
	}
}

void AMD_WetContaminationPatch::OnRep_PatchState()
{
	bCommitted = PatchState.IsValid();
	RefreshVisual();
}

void AMD_WetContaminationPatch::OnRep_Support()
{
	RefreshSupportAttachment();
	RefreshVisual();
}

void AMD_WetContaminationPatch::RefreshVisual()
{
	if (!IsValid(VisualMesh))
	{
		return;
	}

	const UPrimitiveComponent* const ResolvedSupport = ResolveSupportComponent();
	const bool bSupportReady = IsValid(ResolvedSupport)
		&& IsValid(GetRootComponent())
		&& GetRootComponent()->GetAttachParent() == ResolvedSupport;
	const bool bShow = PatchState.IsValid()
		&& (HasAuthority() ? bCommitFinalized : bSupportReady);
	if (!PatchState.IsValid())
	{
		VisualMesh->SetHiddenInGame(true);
		return;
	}

	const FMD_WetContaminationVisualDescriptor Descriptor =
		FMD_WetContaminationVisualDescriptor::FromPatchId(PatchState.PatchId);
	UStaticMesh* const SelectedMesh =
		Descriptor.IsValid()
		&& VisualVariantMeshes.IsValidIndex(Descriptor.MeshVariantIndex)
		? VisualVariantMeshes[Descriptor.MeshVariantIndex].Get()
		: nullptr;
	if (!IsValid(SelectedMesh))
	{
		VisualMesh->SetHiddenInGame(true);
		return;
	}

	VisualMesh->SetStaticMesh(SelectedMesh);
	VisualMesh->SetRelativeTransform(
		Descriptor.MakeRelativeTransform(PatchState.LiquidUnits));
	VisualMesh->SetHiddenInGame(!bShow);
}

void AMD_WetContaminationPatch::RefreshSupportAttachment()
{
	UPrimitiveComponent* const SupportComponent = ResolveSupportComponent();
	if (!IsValid(SupportComponent))
	{
		return;
	}

	AttachToComponent(
		SupportComponent,
		FAttachmentTransformRules::KeepRelativeTransform);
	SetActorRelativeTransform(SupportState.LocalAnchor);
}

UPrimitiveComponent* AMD_WetContaminationPatch::ResolveSupportComponent() const
{
	if (!SupportState.IsValid())
	{
		return nullptr;
	}

	TInlineComponentArray<UPrimitiveComponent*> Components(SupportState.Actor);
	for (UPrimitiveComponent* const Component : Components)
	{
		if (IsValid(Component)
			&& Component->GetFName() == SupportState.ComponentName)
		{
			return Component;
		}
	}
	return nullptr;
}
