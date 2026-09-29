#include "Component/Scanning/MD_ScannerComponent.h"

#include "Audio/MD_AudioRelayComponent.h"
#include "Character/MD_Character.h"
#include "Camera/CameraComponent.h"
#include "Component/Equipment/MD_EquipHotbarComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Component/Scrape/MD_ScrapeSurfaceComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "World/Contamination/MD_Barnacle.h"
#include "World/Contamination/MD_SurfaceStain.h"
#include "Data/Equipment/MD_ToolDefinition.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Gameplay/Scanning/MD_Scannable.h"
#include "GameplayTags/MD_GameplayTags.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "TimerManager.h"
#include "Sound/SoundBase.h"
#include "UI/Equipment/MD_ScannerModeWidget.h"
#include "UObject/ConstructorHelpers.h"
#include "World/Wall/MD_CuttableWall.h"
#include "World/Wall/MD_WallUtility.h"

UMD_ScannerComponent::UMD_ScannerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> WallStyle(
		TEXT("/Game/VFX/03_Scanner/MI_ScanWallType.MI_ScanWallType"));
	WallScanMaterial = WallStyle.Object;
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Mask(
		TEXT("/Game/VFX/03_Scanner/M_ScanCleanupMask.M_ScanCleanupMask"));
	ContaminationMaskMaterial = Mask.Object;
	static ConstructorHelpers::FObjectFinder<USoundBase> LocatorSound(
		TEXT("/Game/Audio/Scanner/SW_QuestSampleBeacon.SW_QuestSampleBeacon"));
	QuestLocatorSound = LocatorSound.Object;
}

void UMD_ScannerComponent::BeginPlay()
{
	Super::BeginPlay();
	if (GetWorld() && GetNetMode() != NM_DedicatedServer)
		GetWorld()->GetTimerManager().SetTimer(QuestLocatorTimer, this, &ThisClass::UpdateQuestLocator, 0.1f, true);
	if (APawn* Pawn = Cast<APawn>(GetOwner()))
		Pawn->ReceiveControllerChangedDelegate.AddUniqueDynamic(this, &ThisClass::HandleControllerChanged);
	if (const AMD_Character* Character = Cast<AMD_Character>(GetOwner()))
	{
		BoundHotbar = Character->GetHotbarComponent();
		if (BoundHotbar.IsValid())
		{
			BoundHotbar->OnHotbarChanged.AddUniqueDynamic(this, &ThisClass::HandleHotbarChanged);
			LastTool = BoundHotbar->GetCurrentTool();
			RefreshModeIndicator();
		}
	}
}

bool UMD_ScannerComponent::IsScannerEquipped() const
{
	const AMD_Character* Character = Cast<AMD_Character>(GetOwner());
	const UMD_EquipHotbarComponent* Hotbar = Character ? Character->GetHotbarComponent() : nullptr;
	const UMD_ToolDefinition* Tool = Hotbar ? Hotbar->GetCurrentTool() : nullptr;
	return IsValid(Tool) && Tool->ToolAbilityTag.MatchesTagExact(MDGameplayTags::Ability_WallScan);
}

FText UMD_ScannerComponent::GetScanModeText() const
{
	if (ScanMode == EMD_ScanMode::Cleanup) return NSLOCTEXT("MDScanner", "CleanupMode", "오염물·따개비");
	return ScanMode == EMD_ScanMode::WallType
		? NSLOCTEXT("MDScanner", "WallTypeMode", "벽 종류")
		: NSLOCTEXT("MDScanner", "InternalMode", "내부 구조물");
}

bool UMD_ScannerComponent::ToggleScanMode()
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	const APlayerController* PC = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	if (!Pawn || !Pawn->IsLocallyControlled() || !PC || !IsScannerEquipped()
		|| UGameplayStatics::IsGamePaused(this) || PC->IsMoveInputIgnored() || PC->IsLookInputIgnored())
	{
		return false;
	}
	ClearScan();
	switch (ScanMode)
	{
	case EMD_ScanMode::InternalStructure: ScanMode = EMD_ScanMode::WallType; break;
	case EMD_ScanMode::WallType: ScanMode = EMD_ScanMode::Cleanup; break;
	default: ScanMode = EMD_ScanMode::InternalStructure; break;
	}
	RefreshModeIndicator();
	OnScanModeChanged.Broadcast(ScanMode);
	return true;
}

uint32 UMD_ScannerComponent::BeginLocalRequest()
{
	ClearScan();
	if (++IssuedRequestId == 0) ++IssuedRequestId;
	ActiveRequestId = IssuedRequestId;
	RefreshModeIndicator();
	return ActiveRequestId;
}

void UMD_ScannerComponent::PresentLocalScan(uint32 RequestId, EMD_ScanMode Mode,
	const TArray<AActor*>& Targets, float Duration, UMaterialParameterCollection* Collection,
	const FVector& Origin, const FVector& Forward, float Radius, float HalfAngle)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(MD_Scanner_Present);
	const APawn* Pawn = Cast<APawn>(GetOwner());
	if (!Pawn || !Pawn->IsLocallyControlled() || RequestId != ActiveRequestId
		|| !MDScanning::IsValidMode(Mode) || Mode != ScanMode || !FMath::IsFinite(Duration) || Duration <= 0
		|| Targets.IsEmpty() || !GetWorld()) return;

	ActiveCollection = Collection;
	bScanVisible = true;
	for (AActor* Target : Targets)
	{
		if (!IsValid(Target)) continue;
		if (Mode == EMD_ScanMode::Cleanup)
		{
			if (AMD_Barnacle* Barnacle = Cast<AMD_Barnacle>(Target))
			{
				if (!Barnacle->IsHidden() && !Barnacle->IsFullyRemoved())
					WallHighlights.AddDefaulted_GetRef().Apply(Barnacle->GetScanMesh(), MDScanning::BarnacleStencil);
			}
			else if (AMD_SurfaceStain* Stain = Cast<AMD_SurfaceStain>(Target))
			{
				if (Stain->GetRemainingSize() > 0)
				{
					Stain->SetScanHighlighted(this, true);
					StainHighlights.AddUnique(Stain);
				}
			}
			else AddContaminationHighlight(Target->FindComponentByClass<UMD_ScrapeSurfaceComponent>());
		}
		else if (Mode == EMD_ScanMode::WallType)
		{
			AMD_CuttableWall* Wall = Cast<AMD_CuttableWall>(Target);
			if (!Wall || Wall->IsHidden() || !Wall->CanCut()) continue;
			FMD_ScanMeshState& State = WallHighlights.AddDefaulted_GetRef();
			State.Apply(Wall->GetScanMesh(), Wall->GetWallType() == EMD_WallType::Strut
				? MDScanning::StrutStencil : MDScanning::PartitionStencil);
		}
		else if (Target->GetClass()->ImplementsInterface(UMD_Scannable::StaticClass()))
		{
			IMD_Scannable::Execute_OnScanned(Target, Duration, GetOwner());
		}
	}
	if (Collection)
	{
		UMaterialParameterCollectionInstance* MPC = GetWorld()->GetParameterCollectionInstance(Collection);
		MPC->SetVectorParameterValue(TEXT("ScanCenter"), FLinearColor(Origin));
		MPC->SetVectorParameterValue(TEXT("ScanForward"), FLinearColor(Forward));
		MPC->SetScalarParameterValue(TEXT("ScanRadius"), Radius);
		MPC->SetScalarParameterValue(TEXT("ScanConeCosine"), FMath::Cos(FMath::DegreesToRadians(FMath::Clamp(HalfAngle, 0.f, 180.f))));
		MPC->SetScalarParameterValue(TEXT("ScanMode"), static_cast<float>(Mode));
		MPC->SetScalarParameterValue(TEXT("ScanActive"), 1.f);
		MPC->SetScalarParameterValue(TEXT("ScanDisplayDuration"), Duration);
		MPC->SetScalarParameterValue(TEXT("ScanStartTime"), GetWorld()->GetTimeSeconds());
	}
	if (Mode != EMD_ScanMode::InternalStructure && (!WallHighlights.IsEmpty() || !ContaminationHighlights.IsEmpty() || !StainHighlights.IsEmpty())) EnableWallPostProcess();
	if (!ContaminationHighlights.IsEmpty())
		GetWorld()->GetTimerManager().SetTimer(ContaminationRefreshTimer, this,
			&ThisClass::RefreshContaminationHighlights, 0.1f, true);
	FTimerDelegate Expire;
	Expire.BindWeakLambda(this, [this, RequestId] { FinishLocalRequest(RequestId); });
	GetWorld()->GetTimerManager().SetTimer(ScanTimer, Expire, Duration, false);
}

void UMD_ScannerComponent::TrackScannedUtility(AMD_WallUtility* Utility)
{
	if (bScanVisible && ScanMode == EMD_ScanMode::InternalStructure && IsValid(Utility))
		ScannedUtilities.AddUnique(Utility);
}

void UMD_ScannerComponent::FinishLocalRequest(uint32 RequestId)
{
	if (RequestId == ActiveRequestId) ClearScan(false);
}

void UMD_ScannerComponent::EnableWallPostProcess()
{
	ClearWallPostProcess();
	UCameraComponent* Camera = GetOwner() ? GetOwner()->FindComponentByClass<UCameraComponent>() : nullptr;
	if (!Camera || !WallScanMaterial) return;
	WallPostProcessCamera = Camera;
	AppliedWallScanMaterial = WallScanMaterial;
	auto& Blendables = Camera->PostProcessSettings.WeightedBlendables.Array;
	if (FWeightedBlendable* Existing = Blendables.FindByPredicate(
		[this](const FWeightedBlendable& Entry) { return Entry.Object == AppliedWallScanMaterial; }))
	{
		OriginalWallBlendWeight = Existing->Weight;
		Existing->Weight = 1.f;
	}
	else
	{
		Blendables.Emplace(1.f, AppliedWallScanMaterial.Get());
	}
}

void UMD_ScannerComponent::ClearWallPostProcess()
{
	if (UCameraComponent* Camera = WallPostProcessCamera.Get())
	{
		auto& Blendables = Camera->PostProcessSettings.WeightedBlendables.Array;
		const int32 Index = Blendables.IndexOfByPredicate(
			[this](const FWeightedBlendable& Entry) { return Entry.Object == AppliedWallScanMaterial; });
		// Preserve a weight subsequently changed by another effect.
		if (Index != INDEX_NONE && Blendables[Index].Weight == 1.f)
		{
			if (OriginalWallBlendWeight.IsSet()) Blendables[Index].Weight = OriginalWallBlendWeight.GetValue();
			else Blendables.RemoveAt(Index);
		}
	}
	WallPostProcessCamera.Reset();
	AppliedWallScanMaterial = nullptr;
	OriginalWallBlendWeight.Reset();
}

void UMD_ScannerComponent::StopPulse()
{
	if (ActivePulseTag.IsValid())
	{
		if (const AMD_Character* Character = Cast<AMD_Character>(GetOwner()))
			if (UMD_AudioRelayComponent* Audio = Character->GetAudioRelayComponent())
				Audio->StopEverywhere(ActivePulseTag, PulseFadeSeconds);
		ActivePulseTag = FGameplayTag();
	}
}

void UMD_ScannerComponent::ClearScan(bool bNotifyServer)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(MD_Scanner_Clear);
	const uint32 PreviousRequest = ActiveRequestId;
	ActiveRequestId = 0;
	if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(ScanTimer);
	ClearContaminationHighlights();
	for (FMD_ScanMeshState& State : WallHighlights) State.Restore();
	WallHighlights.Empty();
	ClearWallPostProcess();
	for (const auto& WeakUtility : ScannedUtilities)
		if (AMD_WallUtility* Utility = WeakUtility.Get()) Utility->StopScanFor(GetOwner());
	ScannedUtilities.Empty();
	if (bScanVisible && ActiveCollection && GetWorld())
		GetWorld()->GetParameterCollectionInstance(ActiveCollection)->SetScalarParameterValue(TEXT("ScanActive"), 0.f);
	bScanVisible = false;
	ActiveCollection = nullptr;
	StopPulse();
	if (bNotifyServer && PreviousRequest != 0)
	{
		const APawn* Pawn = Cast<APawn>(GetOwner());
		if (Pawn && Pawn->IsLocallyControlled() && !Pawn->HasAuthority()) ServerCancelScan(PreviousRequest);
	}
}

bool UMD_ScannerComponent::AcceptAuthoritativeRequest(uint32 RequestId)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !IsScannerEquipped()
		|| !MDScanning::IsNewerRequest(RequestId, CancelledRequestId)
		|| !MDScanning::IsNewerRequest(RequestId, AcceptedRequestId)) return false;
	AcceptedRequestId = RequestId;
	return true;
}

void UMD_ScannerComponent::RejectAuthoritativeRequest(uint32 RequestId)
{
	if (GetOwner() && GetOwner()->HasAuthority()) ClientRejectScan(RequestId);
}

void UMD_ScannerComponent::ClientRejectScan_Implementation(uint32 RequestId)
{
	if (RequestId == ActiveRequestId) ClearScan(false);
}

void UMD_ScannerComponent::ServerCancelScan_Implementation(uint32 RequestId)
{
	if (!MDScanning::IsNewerRequest(RequestId, CancelledRequestId)) return;
	CancelledRequestId = RequestId;
	if (!MDScanning::IsNewerRequest(AcceptedRequestId, RequestId)) StopPulse();
}

void UMD_ScannerComponent::PlayScanPulse(uint32 RequestId, const FGameplayTag& Tag, float Duration, float Fade)
{
	if (!Tag.IsValid() || Duration <= 0 || !FMath::IsFinite(Duration)) return;
	const AMD_Character* Character = Cast<AMD_Character>(GetOwner());
	if (!Character || (!Character->HasAuthority() && RequestId != ActiveRequestId)) return;
	ActivePulseTag = Tag;
	PulseFadeSeconds = Fade;
	if (UMD_AudioRelayComponent* Audio = Character->GetAudioRelayComponent())
		Audio->PlayAttachedForDuration(Tag, Duration, Fade);
}

void UMD_ScannerComponent::HandleHotbarChanged()
{
	if (!IsScannerEquipped()) StopQuestLocator();
	UMD_ToolDefinition* Current = BoundHotbar.IsValid() ? BoundHotbar->GetCurrentTool() : nullptr;
	if (Current != LastTool.Get())
	{
		ClearScan();
		LastTool = Current;
	}
	RefreshModeIndicator();
}

void UMD_ScannerComponent::RefreshModeIndicator()
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	APlayerController* PC = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	const bool bShow = PC && Pawn->IsLocallyControlled() && IsScannerEquipped();
	if (bShow && !ModeIndicator)
	{
		ModeIndicator = CreateWidget<UMD_ScannerModeWidget>(PC);
		if (ModeIndicator) ModeIndicator->AddToPlayerScreen();
	}
	if (ModeIndicator)
	{
		ModeIndicator->SetModeText(GetScanModeText());
		ModeIndicator->SetVisibility(bShow ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
}

void UMD_ScannerComponent::HandleControllerChanged(APawn*, AController*, AController*)
{
	StopQuestLocator();
	ClearScan(false);
	IssuedRequestId = 0;
	AcceptedRequestId = 0;
	CancelledRequestId = 0;
	if (ModeIndicator) ModeIndicator->RemoveFromParent();
	ModeIndicator = nullptr;
	RefreshModeIndicator();
}

void UMD_ScannerComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(QuestLocatorTimer);
	StopQuestLocator();
	ClearScan(false);
	if (ModeIndicator) ModeIndicator->RemoveFromParent();
	ModeIndicator = nullptr;
	if (APawn* Pawn = Cast<APawn>(GetOwner()))
		Pawn->ReceiveControllerChangedDelegate.RemoveDynamic(this, &ThisClass::HandleControllerChanged);
	if (BoundHotbar.IsValid())
		BoundHotbar->OnHotbarChanged.RemoveDynamic(this, &ThisClass::HandleHotbarChanged);
	Super::EndPlay(Reason);
}
