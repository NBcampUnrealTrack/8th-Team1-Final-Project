#include "Component/Equipment/MD_GlassMarkerComponent.h"

#include "AbilitySystemComponent.h"
#include "Abilities/GameplayAbility.h"
#include "Camera/CameraComponent.h"
#include "Character/MD_Character.h"
#include "Component/Equipment/MD_EquipHotbarComponent.h"
#include "Component/Interaction/MD_GlassDrawingFocusComponent.h"
#include "Component/Equipment/MD_ThrowComponent.h"
#include "Component/Interaction/MD_InteractionComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/Equipment/MD_GlassMarkerSettings.h"
#include "Data/Equipment/MD_ToolDefinition.h"
#include "Data/Drawing/MD_GlassDrawingProfile.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerState.h"
#include "GameplayTags/MD_GameplayTags.h"
#include "Net/UnrealNetwork.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Subsystem/World/MD_GlassDrawingSubsystem.h"
#include "TimerManager.h"
#include "World/Interaction/MD_GlassDrawingBoard.h"

namespace
{
	constexpr int32 MaxSegmentsPerSample = 64;

	bool IsNewerMarkerSerial(uint32 Candidate, uint32 Previous)
	{
		return Candidate != 0 && static_cast<int32>(Candidate - Previous) > 0;
	}
}

UMD_GlassMarkerComponent::UMD_GlassMarkerComponent()
{
	SetIsReplicatedByDefault(true);
	PrimaryComponentTick.bCanEverTick = false;
}

void UMD_GlassMarkerComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UMD_GlassMarkerComponent, bErasing);
	DOREPLIFETIME(UMD_GlassMarkerComponent, SelectedInkColorIndex);
}

AMD_Character* UMD_GlassMarkerComponent::GetCharacter() const
{
	return Cast<AMD_Character>(GetOwner());
}

void UMD_GlassMarkerComponent::Configure(UMD_GlassMarkerSettings* InSettings)
{
	CancelUse();
	Settings = InSettings;
}

bool UMD_GlassMarkerComponent::CanUseEquippedMarker() const
{
	const AMD_Character* Character = GetCharacter();
	if (!IsValid(Character) || Character->IsActorBeingDestroyed() || !GetWorld() || GetWorld()->IsPaused()
		|| !IsValid(Settings) || !Settings->IsValidForMarker()) return false;
	const UMD_InteractionComponent* Interaction = Character->GetInteractionComponent();
	const AController* Controller = Character->GetController();
	const UMD_EquipHotbarComponent* Hotbar = Character->GetHotbarComponent();
	const UMD_GlassDrawingFocusComponent* Focus = Character->GetGlassDrawingFocusComponent();
	const UMD_ToolDefinition* Tool = GetActiveToolDefinition();
	const UAbilitySystemComponent* ASC = Character->GetAbilitySystemComponent();
	return IsValid(Controller) && !Controller->IsMoveInputIgnored() && !Controller->IsLookInputIgnored()
		&& IsValid(Interaction) && Interaction->CanOwnerInteract() && !Interaction->IsHolding()
		&& IsValid(Hotbar) && (!IsValid(Focus) || !Focus->IsFocusActive() || Focus->IsDrawingReady())
		&& IsValid(Tool) && Tool->ToolAbilityTag.MatchesTagExact(MDGameplayTags::Ability_UtilityRepair_GlassMarker)
		&& ASC && !ASC->HasMatchingGameplayTag(MDGameplayTags::State_Dead)
		&& (!Character->GetThrowComponent() || !Character->GetThrowComponent()->IsCharging());
}

UMD_ToolDefinition* UMD_GlassMarkerComponent::GetActiveToolDefinition() const
{
	const AMD_Character* Character = GetCharacter();
	if (!IsValid(Character)) return nullptr;
	const UMD_GlassDrawingFocusComponent* Focus = Character->GetGlassDrawingFocusComponent();
	if (IsValid(Focus) && Focus->IsFocusActive()) return Focus->GetMarkerTool();
	const UMD_EquipHotbarComponent* Hotbar = Character->GetHotbarComponent();
	return IsValid(Hotbar) ? Hotbar->GetCurrentTool() : nullptr;
}

FRotator UMD_GlassMarkerComponent::GetEraserRotationOffset() const
{
	return IsValid(Settings) ? Settings->EraserRotationOffset : FRotator::ZeroRotator;
}

FName UMD_GlassMarkerComponent::GetInkIndicatorMaterialSlot() const
{
	return IsValid(Settings) ? Settings->InkIndicatorMaterialSlot : NAME_None;
}

FLinearColor UMD_GlassMarkerComponent::GetSelectedInkColor() const
{
	const UMD_GlassDrawingSubsystem* Drawing = GetWorld() ? GetWorld()->GetSubsystem<UMD_GlassDrawingSubsystem>() : nullptr;
	const UMD_GlassDrawingProfile* Profile = Drawing ? Drawing->GetProfile() : nullptr;
	return IsValid(Profile) ? Profile->GetInkColor(SelectedInkColorIndex) : FLinearColor::White;
}

void UMD_GlassMarkerComponent::CycleInkColor()
{
	AMD_Character* Character = GetCharacter();
	if (!Character || !Character->IsLocallyControlled() || !CanUseEquippedMarker()) return;
	ServerCycleInkColor(GetActiveToolDefinition());
}

void UMD_GlassMarkerComponent::ServerCycleInkColor_Implementation(UMD_ToolDefinition* ExpectedTool)
{
	AMD_Character* Character = GetCharacter();
	if (!CanUseEquippedMarker()) return;
	UMD_EquipHotbarComponent* Hotbar = Character->GetHotbarComponent();
	if (GetActiveToolDefinition() != ExpectedTool || !Hotbar->FindItemAbility(ExpectedTool).IsValid()) return;
	UMD_GlassDrawingSubsystem* Drawing = GetWorld()->GetSubsystem<UMD_GlassDrawingSubsystem>();
	const UMD_GlassDrawingProfile* Profile = Drawing ? Drawing->GetProfile() : nullptr;
	if (!IsValid(Profile) || !Profile->IsValidForDrawing()) return;
	const int32 ColorCount = Profile->GetColorCount();
	if (ColorCount < 1 || ColorCount > 8) return;
	const uint8 NewIndex = static_cast<uint8>((int32(SelectedInkColorIndex) + 1) % ColorCount);
	if (NewIndex == SelectedInkColorIndex) return;
	SelectedInkColorIndex = NewIndex;
	// A new color starts at its first contact point; existing segments keep their own palette index.
	LastBoard.Reset();
	LastContactAt = -1.0;
	OnRep_SelectedInkColorIndex();
	Character->ForceNetUpdate();
}

void UMD_GlassMarkerComponent::OnRep_SelectedInkColorIndex()
{
	OnInkColorChanged.Broadcast(SelectedInkColorIndex, GetSelectedInkColor());
}

void UMD_GlassMarkerComponent::SetDrawInputHeld(bool bHeld)
{
	if (bHeld && !CanUseEquippedMarker()) return;
	bDrawInputHeld = bHeld;
	RefreshHeldInput();
}

void UMD_GlassMarkerComponent::SetEraseInputHeld(bool bHeld)
{
	if (bHeld && !CanUseEquippedMarker()) return;
	bEraseInputHeld = bHeld;
	RefreshHeldInput();
}

void UMD_GlassMarkerComponent::RefreshHeldInput()
{
	AMD_Character* Character = GetCharacter();
	if (!Character || !Character->IsLocallyControlled()) return;
	if ((!bDrawInputHeld && !bEraseInputHeld) || !CanUseEquippedMarker())
	{
		CancelUse();
		return;
	}
	if (!ActiveAbility.IsValid())
	{
		const FGameplayAbilitySpecHandle Handle = Character->GetHotbarComponent()->FindItemAbility(GetActiveToolDefinition());
		UAbilitySystemComponent* ASC = Character->GetAbilitySystemComponent();
		if (!Handle.IsValid() || !ASC || !ASC->TryActivateAbility(Handle)) CancelUse();
		return;
	}
	if (!bLocalSession || bLocalEraseMode != bEraseInputHeld) StartLocalSession();
}

bool UMD_GlassMarkerComponent::BeginUse(FGameplayAbilitySpecHandle AbilityHandle)
{
	if (!AbilityHandle.IsValid() || ActiveAbility.IsValid() || !CanUseEquippedMarker()) return false;
	AMD_Character* Character = GetCharacter();
	ActiveAbility = AbilityHandle;
	ActiveTool = GetActiveToolDefinition();
	if (Character->HasAuthority() && bServerBeginPending) AcceptServerSession();
	if (Character->IsLocallyControlled())
	{
		if (!bDrawInputHeld && !bEraseInputHeld)
		{
			ActiveAbility = FGameplayAbilitySpecHandle();
			ActiveTool.Reset();
			return false;
		}
		StartLocalSession();
	}
	return true;
}

void UMD_GlassMarkerComponent::FinishUse(FGameplayAbilitySpecHandle AbilityHandle)
{
	if (ActiveAbility == AbilityHandle) ResetUse(false);
}

void UMD_GlassMarkerComponent::StartLocalSession()
{
	StopLocalSession();
	if (++LocalEpoch == 0) ++LocalEpoch;
	LocalSequence = 0;
	bLocalSession = true;
	bLocalEraseMode = bEraseInputHeld;
	SetErasing(bLocalEraseMode);
	ServerBeginUse(LocalEpoch, ActiveTool.Get(), bLocalEraseMode);
	GetWorld()->GetTimerManager().SetTimer(SampleTimer, this, &UMD_GlassMarkerComponent::SendSample,
		Settings->SampleIntervalSeconds, true);
	SendSample();
}

void UMD_GlassMarkerComponent::StopLocalSession()
{
	if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(SampleTimer);
	const bool bHadSession = bLocalSession;
	bLocalSession = false;
	if (bHadSession) ServerEndUse(LocalEpoch);
}

void UMD_GlassMarkerComponent::SendSample()
{
	AMD_Character* Character = GetCharacter();
	if (!bLocalSession || !Character || !CanUseEquippedMarker()
		|| GetActiveToolDefinition() != ActiveTool.Get())
	{
		CancelUse();
		return;
	}
	if (++LocalSequence == 0) ++LocalSequence;
	const UMD_GlassDrawingFocusComponent* Focus = Character->GetGlassDrawingFocusComponent();
	const FVector2D Pointer = IsValid(Focus) && Focus->IsFocusActive()
		? Focus->GetPointerNormalized() : FVector2D(0.5, 0.5);
	ServerSample(LocalEpoch, LocalSequence, Pointer);
}

void UMD_GlassMarkerComponent::CancelUse()
{
	ResetUse(true);
}

void UMD_GlassMarkerComponent::ResetUse(bool bCancelAbility)
{
	const FGameplayAbilitySpecHandle AbilityToCancel = ActiveAbility;
	ActiveAbility = FGameplayAbilitySpecHandle();
	ActiveTool.Reset();
	bDrawInputHeld = false;
	bEraseInputHeld = false;
	StopLocalSession();
	if (GetOwner() && GetOwner()->HasAuthority()) StopServerSession();
	SetErasing(false);
	if (bCancelAbility && AbilityToCancel.IsValid())
		if (AMD_Character* Character = GetCharacter())
			if (UAbilitySystemComponent* ASC = Character->GetAbilitySystemComponent()) ASC->CancelAbilityHandle(AbilityToCancel);
}

void UMD_GlassMarkerComponent::SetErasing(bool bNewErasing)
{
	if (bErasing == bNewErasing) return;
	bErasing = bNewErasing;
	OnMarkerModeChanged.Broadcast(bErasing);
	if (GetOwner() && GetOwner()->HasAuthority()) GetOwner()->ForceNetUpdate();
}

void UMD_GlassMarkerComponent::OnRep_Erasing()
{
	OnMarkerModeChanged.Broadcast(bErasing);
}

void UMD_GlassMarkerComponent::ServerBeginUse_Implementation(uint32 Epoch, UMD_ToolDefinition* ExpectedTool, bool bErase)
{
	AMD_Character* Character = GetCharacter();
	if (!Character || !IsNewerMarkerSerial(Epoch, LastServerEpoch)) return;
	LastServerEpoch = Epoch;
	StopServerSession();
	UMD_EquipHotbarComponent* Hotbar = Character->GetHotbarComponent();
	if (!CanUseEquippedMarker() || GetActiveToolDefinition() != ExpectedTool
		|| !Hotbar->FindItemAbility(ExpectedTool).IsValid())
	{
		ClientStopUse(Epoch);
		return;
	}
	ServerEpoch = Epoch;
	ServerSequence = 0;
	ServerTool = ExpectedTool;
	bServerBeginPending = true;
	bServerEraseMode = bErase;
	ServerRequestedAt = GetWorld()->GetTimeSeconds();
	GetWorld()->GetTimerManager().SetTimer(ServerWatchdogTimer, this, &UMD_GlassMarkerComponent::CheckServerSession,
		Settings->SampleIntervalSeconds, true);
	AcceptServerSession();
}

bool UMD_GlassMarkerComponent::HasActiveMarkerAbility() const
{
	const AMD_Character* Character = GetCharacter();
	const UAbilitySystemComponent* ASC = Character ? Character->GetAbilitySystemComponent() : nullptr;
	const FGameplayAbilitySpec* Spec = ASC ? ASC->FindAbilitySpecFromHandle(ActiveAbility) : nullptr;
	return Spec && Spec->IsActive() && !Spec->PendingRemove && !Spec->RemoveAfterActivation
		&& Spec->SourceObject.Get() == ServerTool.Get() && IsValid(Spec->Ability)
		&& Spec->Ability->GetAssetTags().HasTagExact(MDGameplayTags::Ability_UtilityRepair_GlassMarker);
}

void UMD_GlassMarkerComponent::AcceptServerSession()
{
	// GAS activation uses the PlayerState's ASC channel; a Character RPC may arrive first.
	// Keep that request inert until the exact source item's committed GA is active.
	if (!bServerBeginPending || !HasActiveMarkerAbility()) return;
	bServerBeginPending = false;
	bServerUsing = true;
	SetErasing(bServerEraseMode);
}

void UMD_GlassMarkerComponent::ServerEndUse_Implementation(uint32 Epoch)
{
	if ((bServerUsing || bServerBeginPending) && ServerEpoch == Epoch) StopServerSession();
}

void UMD_GlassMarkerComponent::StopServerSession()
{
	bServerUsing = false;
	bServerBeginPending = false;
	ServerTool.Reset();
	LastBoard.Reset();
	LastContactAt = -1.0;
	if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(ServerWatchdogTimer);
	SetErasing(false);
}

void UMD_GlassMarkerComponent::CheckServerSession()
{
	AMD_Character* Character = GetCharacter();
	if (!bServerUsing && !bServerBeginPending) return;
	const double Now = GetWorld()->GetTimeSeconds();
	if (bServerBeginPending) AcceptServerSession();
	if (!CanUseEquippedMarker() || GetActiveToolDefinition() != ServerTool.Get()
		|| (bServerUsing && !HasActiveMarkerAbility())
		|| (bServerBeginPending && Now - ServerRequestedAt > 1.0)
		|| (bServerUsing && Now - FMath::Max(LastServerSampleAt, ServerRequestedAt)
			> FMath::Max(1.0f, Settings->MaximumJoinGapSeconds * 3.0f)))
	{
		const uint32 Epoch = ServerEpoch;
		CancelUse();
		ClientStopUse(Epoch);
	}
}

bool UMD_GlassMarkerComponent::TraceSurface(const FVector2D& Pointer, AMD_GlassDrawingBoard*& OutBoard, FVector2D& OutUV) const
{
	OutBoard = nullptr;
	const AMD_Character* Character = GetCharacter();
	if (!Character || !GetWorld()) return false;
	const UMD_GlassDrawingFocusComponent* Focus = Character->GetGlassDrawingFocusComponent();
	const bool bDeskFocus = IsValid(Focus) && Focus->IsFocusActive();
	FVector Origin;
	FVector Direction;
	float Reach = Settings->ReachCm;
	if (bDeskFocus)
	{
		if (!Focus->GetValidatedDrawRay(Pointer, Origin, Direction, Reach)) return false;
	}
	else
	{
		const UCameraComponent* Camera = Character->FindComponentByClass<UCameraComponent>();
		Origin = Camera ? Camera->GetComponentLocation() : Character->GetPawnViewLocation();
		const FRotator Rotation = Character->GetController()->GetControlRotation();
		if (Rotation.ContainsNaN()) return false;
		Direction = Rotation.Vector();
	}
	if (Origin.ContainsNaN() || Direction.ContainsNaN() || !Direction.Normalize()
		|| !FMath::IsFinite(Reach) || Reach <= 0.0f) return false;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MD_GlassMarkerTrace), true, Character);
	Params.bReturnFaceIndex = true;
	FHitResult Hit;
	if (!GetWorld()->LineTraceSingleByChannel(Hit, Origin, Origin + Direction * Reach,
		ECC_Visibility, Params)) return false;
	// A desk session authorizes only its configured glass, never a surface behind another blocker.
	if (bDeskFocus && Hit.GetComponent() != Focus->GetTargetGlassMesh()) return false;
	UMD_GlassDrawingSubsystem* Drawing = GetWorld()->GetSubsystem<UMD_GlassDrawingSubsystem>();
	OutBoard = Drawing ? Drawing->ResolveSurfaceHit(Hit, OutUV, true) : nullptr;
	return IsValid(OutBoard);
}

void UMD_GlassMarkerComponent::ServerSample_Implementation(uint32 Epoch, uint32 Sequence, FVector2D Pointer)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(MD_GlassMarkerSample);
	if (!bServerUsing || Epoch != ServerEpoch || !IsNewerMarkerSerial(Sequence, ServerSequence)) return;
	ServerSequence = Sequence;
	AMD_Character* Character = GetCharacter();
	if (!CanUseEquippedMarker() || GetActiveToolDefinition() != ServerTool.Get()
		|| !HasActiveMarkerAbility())
	{
		CheckServerSession();
		return;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	// A sequence number never authorizes an unbounded RPC trace rate.
	if (LastServerSampleAt >= 0.0 && Now - LastServerSampleAt < Settings->SampleIntervalSeconds * 0.8) return;
	LastServerSampleAt = Now;
	AMD_GlassDrawingBoard* Board = nullptr;
	FVector2D UV;
	if (!TraceSurface(Pointer, Board, UV))
	{
		LastBoard.Reset();
		LastContactAt = -1.0;
		return;
	}
	APlayerState* Author = Character->GetPlayerState();
	if (!Author) return;
	if (bServerEraseMode)
	{
		Board->EraseAt(Author, UV);
		LastBoard.Reset();
		LastContactAt = -1.0;
		return;
	}
	const bool bSameContact = LastBoard.Get() == Board && LastContactAt >= 0.0
		&& Now - LastContactAt <= Settings->MaximumJoinGapSeconds;
	const double Distance = bSameContact
		? FVector::Distance(Board->GetPointWorldPosition(LastUV), Board->GetPointWorldPosition(UV)) : 0.0;
	if (bSameContact && Distance < Settings->MinimumPointSpacingCm)
	{
		LastContactAt = Now;
		return;
	}
	const UMD_GlassDrawingProfile* Profile = Board->GetProfile();
	// Leave rounding room for the board's strict world-space length check.
	const double SegmentLength = IsValid(Profile)
		? FMath::Min(double(Settings->MaximumSegmentLengthCm), double(Profile->MaxSegmentLengthCm)) * 0.999 : 0.0;
	const bool bJoinContact = bSameContact && FMath::IsFinite(Distance) && SegmentLength > 0.0
		&& Distance <= SegmentLength * MaxSegmentsPerSample;
	const FVector2D Start = bJoinContact ? LastUV : UV;
	const int32 SegmentCount = bJoinContact ? FMath::Max(1, FMath::CeilToInt32(Distance / SegmentLength)) : 1;
	bool bAppended = true;
	FVector2D SegmentStart = Start;
	// Keep fast motion continuous without increasing the sample/RPC rate or exceeding a board's piece limit.
	for (int32 Index = 1; Index <= SegmentCount; ++Index)
	{
		const FVector2D SegmentEnd = Index == SegmentCount ? UV
			: FMath::Lerp(Start, UV, double(Index) / SegmentCount);
		if (!Board->AppendSegment(Author, SegmentStart, SegmentEnd, SelectedInkColorIndex))
		{
			bAppended = false;
			break;
		}
		SegmentStart = SegmentEnd;
	}
	if (bAppended)
	{
		LastBoard = Board;
		LastUV = UV;
		LastContactAt = Now;
	}
	else
	{
		LastBoard.Reset();
		LastContactAt = -1.0;
	}
}

void UMD_GlassMarkerComponent::ClientStopUse_Implementation(uint32 Epoch)
{
	if (LocalEpoch == Epoch) CancelUse();
}

void UMD_GlassMarkerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	CancelUse();
	Super::EndPlay(EndPlayReason);
}
