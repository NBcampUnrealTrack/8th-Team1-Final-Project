#include "World/Facility/MD_FingerprintInterlock.h"
#include "World/Facility/MD_FingerprintPad.h"
#include "World/Trash/MD_TrashSorter.h"
#include "Component/Interaction/MD_InteractionComponent.h"
#include "Gameplay/Merchant/MD_MerchantSubsystem.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/Pawn.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

bool UMD_FingerprintFaultComponent::CanStartFault() const
{
	const auto* Owner = Cast<AMD_FingerprintInterlock>(GetOwner());
	return Owner && Owner->HasValidConfiguration();
}

AMD_FingerprintInterlock::AMD_FingerprintInterlock()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.1f;
	Fault = CreateDefaultSubobject<UMD_FingerprintFaultComponent>(TEXT("Fault"));
	Fault->Kind = EMD_FacilityKind::Fingerprint;
	// A single controller timer owns this event; the global manager must not double-schedule it.
	Fault->bRandomFaultEligible = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

void AMD_FingerprintInterlock::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMD_FingerprintInterlock, Pads);
	DOREPLIFETIME(AMD_FingerprintInterlock, LossPerSecond);
	DOREPLIFETIME(AMD_FingerprintInterlock, TotalFaultLoss);
	DOREPLIFETIME(AMD_FingerprintInterlock, SoloFirstPad);
	DOREPLIFETIME(AMD_FingerprintInterlock, SoloDeadline);
}

void AMD_FingerprintInterlock::BeginPlay()
{
	Super::BeginPlay();
	Fault->OnStateChanged.AddUniqueDynamic(this, &ThisClass::HandleFault);
	if (HasAuthority())
	{
		if (IsActive()) HandleFault(Fault, FMD_FacilitySnapshot(), Fault->GetSnapshot());
		ArmNextFault();
	}
}

void AMD_FingerprintInterlock::EndPlay(const EEndPlayReason::Type Reason)
{
	GetWorldTimerManager().ClearTimer(FaultTimer);
	Fault->OnStateChanged.RemoveDynamic(this, &ThisClass::HandleFault);
	if (HasAuthority()) { ResetScans(); SetSorterLocks(false); }
	Super::EndPlay(Reason);
}

bool AMD_FingerprintInterlock::IsActive() const { return Fault && Fault->IsFaultActive(); }

bool AMD_FingerprintInterlock::HasValidConfiguration() const
{
	return Pads.Num() == 2 && IsValid(Pads[0]) && IsValid(Pads[1]) && Pads[0] != Pads[1]
		&& Pads[0]->Interlock == this && Pads[1]->Interlock == this
		&& Pads[0]->PadIndex == 0 && Pads[1]->PadIndex == 1
		&& Pads[0]->GetWorld() == GetWorld() && Pads[1]->GetWorld() == GetWorld()
		&& FMath::IsFinite(SoloWindowSeconds) && SoloWindowSeconds > 0.f && CostUnit >= 0;
}

int32 AMD_FingerprintInterlock::GetParticipantCount() const
{
	int32 Count = 0;
	if (const AGameStateBase* GS = GetWorld() ? GetWorld()->GetGameState() : nullptr)
		for (const APlayerState* PS : GS->PlayerArray)
			if (IsValid(PS) && !PS->IsInactive() && !PS->IsSpectator() && !PS->IsOnlyASpectator()) ++Count;
	return FMath::Clamp(Count, 1, 4);
}

int32 AMD_FingerprintInterlock::CalculateLossPerSecond(int32 Count, int32 Unit)
{
	static constexpr int32 Multipliers[] = {0, 1, 2, 4};
	return int32(FMath::Clamp<int64>(int64(FMath::Max(0, Unit)) * Multipliers[FMath::Clamp(Count, 1, 4)-1], 0, MAX_int32));
}

bool AMD_FingerprintInterlock::StartFault()
{
	return HasAuthority() && Fault->TryBreak();
}

void AMD_FingerprintInterlock::ArmNextFault()
{
	if (!HasAuthority() || !bAutomaticFaults || !FMath::IsFinite(MinFaultInterval)
		|| !FMath::IsFinite(MaxFaultInterval) || MinFaultInterval <= 0.f || MaxFaultInterval < MinFaultInterval) return;
	GetWorldTimerManager().SetTimer(FaultTimer, this, &ThisClass::AutomaticFault,
		FMath::FRandRange(MinFaultInterval, MaxFaultInterval), false);
}

void AMD_FingerprintInterlock::AutomaticFault()
{
	StartFault();
	ArmNextFault();
}

void AMD_FingerprintInterlock::SetSorterLocks(bool bLocked)
{
	if (bLocked)
	{
		for (AMD_TrashSorter* Sorter : LinkedSorters)
			if (IsValid(Sorter) && Sorter->GetWorld() == GetWorld())
			{
				LockedSorters.AddUnique(Sorter);
				Sorter->SetSafetyInterlock(this, true);
			}
	}
	else
	{
		for (const auto& Weak : LockedSorters)
			if (AMD_TrashSorter* Sorter = Weak.Get()) Sorter->SetSafetyInterlock(this, false);
		LockedSorters.Empty();
	}
}

void AMD_FingerprintInterlock::ResetScans()
{
	for (AMD_FingerprintPad* Pad : Pads)
		if (IsValid(Pad) && Pad->Interlock == this) { Pad->ActiveScanner = nullptr; Pad->ForceNetUpdate(); }
	SoloPawn.Reset();
	SoloFirstPad = INDEX_NONE;
	SoloDeadline = 0.;
	ForceNetUpdate();
}

void AMD_FingerprintInterlock::RefreshParticipantMode()
{
	const int32 Count = GetParticipantCount();
	const bool bNowSolo = Count == 1;
	if (bSoloMode != bNowSolo) { bSoloMode = bNowSolo; ResetScans(); }
	LossPerSecond = CalculateLossPerSecond(Count, CostUnit);
}

void AMD_FingerprintInterlock::HandleFault(UMD_FacilityFaultComponent*, const FMD_FacilitySnapshot& Previous,
	const FMD_FacilitySnapshot& Current)
{
	if (!HasAuthority() || Previous.IsFaultActive() == Current.IsFaultActive()) return;
	ResetScans();
	if (Current.IsFaultActive())
	{
		bSoloMode = GetParticipantCount() == 1;
		LastChargeTime = GetWorld()->GetTimeSeconds();
		RefreshParticipantMode();
		SetSorterLocks(true);
	}
	else { LossPerSecond = 0; SetSorterLocks(false); }
	ForceNetUpdate();
}

bool AMD_FingerprintInterlock::BeginScan(AMD_FingerprintPad* Pad, APawn* Pawn)
{
	if (!HasAuthority() || !IsActive() || !HasValidConfiguration() || !IsValid(Pad)
		|| !Pads.IsValidIndex(Pad->PadIndex) || Pads[Pad->PadIndex] != Pad || !Pad->CanMaintainScan(Pawn)) return false;
	RefreshParticipantMode();
	if (IsValid(Pad->ActiveScanner) && Pad->ActiveScanner != Pawn) return false;
	Pad->ActiveScanner = Pawn;
	Pad->NotifyScanAccepted();
	Pad->ForceNetUpdate();
	const double Now = GetWorld()->GetTimeSeconds();
	if (bSoloMode)
	{
		if (SoloFirstPad != INDEX_NONE && SoloPawn == Pawn && Now < SoloDeadline && SoloFirstPad != Pad->PadIndex)
			CompleteAuthentication();
		else if (SoloFirstPad == INDEX_NONE || Now >= SoloDeadline || SoloPawn != Pawn)
		{
			SoloFirstPad = Pad->PadIndex; SoloPawn = Pawn; SoloDeadline = Now + SoloWindowSeconds;
			ForceNetUpdate();
		}
	}
	else if (IsValid(Pads[0]->ActiveScanner) && IsValid(Pads[1]->ActiveScanner)
		&& Pads[0]->ActiveScanner != Pads[1]->ActiveScanner
		&& Pads[0]->CanMaintainScan(Pads[0]->ActiveScanner) && Pads[1]->CanMaintainScan(Pads[1]->ActiveScanner))
		CompleteAuthentication();
	return true;
}

void AMD_FingerprintInterlock::ReleaseScan(AMD_FingerprintPad* Pad, APawn* Pawn)
{
	if (HasAuthority() && IsValid(Pad) && Pad->Interlock == this && Pad->ActiveScanner == Pawn)
	{
		Pad->ActiveScanner = nullptr; Pad->ForceNetUpdate();
	}
}

void AMD_FingerprintInterlock::CompleteAuthentication()
{
	// Account for whole elapsed seconds before recovery, never charge after it.
	ApplyElapsedCharges();
	Fault->CompleteRepairFromValidatedSource(Fault->GetSnapshot().FaultId);
}

void AMD_FingerprintInterlock::ApplyElapsedCharges()
{
	const double Now = GetWorld()->GetTimeSeconds();
	const int64 Seconds = FMath::FloorToInt64(FMath::Max(0., Now - LastChargeTime));
	if (Seconds <= 0) return;
	LastChargeTime += double(Seconds);
	if (LossPerSecond <= 0 || !GetGameInstance()) return;
	if (auto* Merchant = GetGameInstance()->GetSubsystem<UMD_MerchantSubsystem>())
	{
		const int32 Charge = int32(FMath::Min<int64>(MAX_int32, Seconds * LossPerSecond));
		const int32 Before = Merchant->GetCredits();
		Merchant->AddCredits(EMD_DailyReportCategory::Accident, -Charge);
		TotalFaultLoss += int64(Before) - Merchant->GetCredits();
		ForceNetUpdate();
	}
}

void AMD_FingerprintInterlock::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority() || !IsActive()) return;
	if (!HasValidConfiguration())
	{
		// Missing/destroyed endpoints must not leave an unrepairable, draining safety lock.
		Fault->CompleteRepairFromValidatedSource(Fault->GetSnapshot().FaultId); return;
	}
	RefreshParticipantMode();
	for (AMD_FingerprintPad* Pad : Pads)
		if (Pad->ActiveScanner && !Pad->CanMaintainScan(Pad->ActiveScanner)) ReleaseScan(Pad, Pad->ActiveScanner);
	if (SoloFirstPad != INDEX_NONE)
	{
		const APawn* Pawn = SoloPawn.Get();
		const auto* Interaction = Pawn ? Pawn->FindComponentByClass<UMD_InteractionComponent>() : nullptr;
		if (GetWorld()->GetTimeSeconds() >= SoloDeadline || !Pawn || !Pawn->GetController()
			|| !Interaction || !Interaction->CanOwnerInteract()) ResetScans();
	}
	ApplyElapsedCharges();
}

static FAutoConsoleCommandWithWorld GBreakFingerprint(
	TEXT("MD.BreakFingerprint"), TEXT("Start the placed fingerprint safety incident on the host/server."),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
	{
		if (!World || World->GetNetMode() == NM_Client || !World->IsGameWorld()) return;
		for (TActorIterator<AMD_FingerprintInterlock> It(World); It; ++It)
			if (It->StartFault()) break;
	}));
