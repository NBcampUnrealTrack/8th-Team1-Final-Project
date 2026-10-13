#include "Component/Scanning/MD_ScannerComponent.h"
#include "Audio/MD_GameplayAudioFeedback.h"

#include "Audio/MD_AudioRelayComponent.h"
#include "Character/MD_Character.h"
#include "Camera/CameraComponent.h"
#include "Component/Equipment/MD_EquipHotbarComponent.h"
#include "Component/Facility/MD_FacilityFaultManagerComponent.h"
#include "Component/Interaction/MD_InteractionComponent.h"
#include "Components/SpotLightComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/GameStateBase.h"
#include "AbilitySystemComponent.h"
#include "Components/StaticMeshComponent.h"
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
#include "UI/Equipment/MD_ScannerModeWidget.h"
#include "UObject/ConstructorHelpers.h"
#include "World/Wall/MD_CuttableWall.h"
#include "World/Wall/MD_WallUtility.h"

UMD_ScannerComponent::UMD_ScannerComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	SetIsReplicatedByDefault(true);
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> WallStyle(
		TEXT("/Game/VFX/03_Scanner/MI_ScanWallType.MI_ScanWallType"));
	WallScanMaterial = WallStyle.Object;
}

void UMD_ScannerComponent::BeginPlay()
{
	Super::BeginPlay();
	if (APawn* Pawn = Cast<APawn>(GetOwner()))
		Pawn->ReceiveControllerChangedDelegate.AddUniqueDynamic(this, &ThisClass::HandleControllerChanged);
	if (const AMD_Character* Character = Cast<AMD_Character>(GetOwner()))
	{
		if(auto* Interaction=Character->GetInteractionComponent())
			Interaction->OnHeldActorChanged.AddUniqueDynamic(this,&ThisClass::HandleHeldActorChanged);
		BoundHotbar = Character->GetHotbarComponent();
		if (BoundHotbar.IsValid())
		{
			BoundHotbar->OnHotbarChanged.AddUniqueDynamic(this, &ThisClass::HandleHotbarChanged);
			LastTool = BoundHotbar->GetCurrentTool();
			RefreshModeIndicator();
		}
	}
	if(GetWorld()->GetNetMode()!=NM_DedicatedServer)
	{
		BlackoutLight=NewObject<USpotLightComponent>(GetOwner(),TEXT("ScannerBlackoutLight"),RF_Transient);
		BlackoutLight->SetupAttachment(GetOwner()->GetRootComponent());BlackoutLight->SetMobility(EComponentMobility::Movable);
		BlackoutLight->SetIntensityUnits(ELightUnits::Lumens);BlackoutLight->SetIntensity(BlackoutLightLumens);
		BlackoutLight->SetLightColor(BlackoutLightColor);BlackoutLight->SetAttenuationRadius(BlackoutLightRadius);
		BlackoutLight->SetInnerConeAngle(16);BlackoutLight->SetOuterConeAngle(28);BlackoutLight->SetCastShadows(true);
		BlackoutLight->SetVolumetricScatteringIntensity(.15f);BlackoutLight->SetVisibility(false);BlackoutLight->RegisterComponent();
		GetWorld()->GetTimerManager().SetTimer(BlackoutLightTimer,this,&ThisClass::RefreshBlackoutLight,.1f,true);
		RefreshBlackoutLight();
	}
}

bool UMD_ScannerComponent::IsBlackoutLightActive() const { return BlackoutLight && BlackoutLight->IsVisible(); }
void UMD_ScannerComponent::RefreshBlackoutLight()
{
	if(!BlackoutLight || !GetWorld()) return;
	const auto* GS=GetWorld()->GetGameState();auto* Manager=GS?GS->FindComponentByClass<UMD_FacilityFaultManagerComponent>():nullptr;
	if(Manager!=BoundPowerManager.Get())
	{
		if(BoundPowerManager.IsValid()) BoundPowerManager->OnBlackoutChanged.RemoveDynamic(this,&ThisClass::HandleBlackoutChanged);
		BoundPowerManager=Manager;if(Manager) Manager->OnBlackoutChanged.AddUniqueDynamic(this,&ThisClass::HandleBlackoutChanged);
	}
	const auto* Character=Cast<AMD_Character>(GetOwner());const auto* Interaction=Character?Character->GetInteractionComponent():nullptr;
	const auto* ASC=Character?Character->GetAbilitySystemComponent():nullptr;
	const bool bHasPossession=Character && (Character->GetLocalRole()==ROLE_SimulatedProxy || Character->GetController());
	const bool bOn=Manager && Manager->IsBlackoutActive() && bHasPossession && !Character->IsHidden() && IsScannerEquipped()
		&& Interaction && !Interaction->IsHolding() && (!ASC || !ASC->HasMatchingGameplayTag(MDGameplayTags::State_Dead));
	if (BlackoutLight->IsVisible() != bOn)
		MDAudioFeedback::LocalPawn(Character, bOn ? TEXT("SFX.Tool.Scanner.LightOn") : TEXT("SFX.Tool.Scanner.LightOff"));
	BlackoutLight->SetVisibility(bOn);SetComponentTickEnabled(bOn);if(bOn) UpdateBlackoutLightTransform();
}
void UMD_ScannerComponent::UpdateBlackoutLightTransform()
{
	const auto* Pawn=Cast<APawn>(GetOwner());if(!Pawn || !BlackoutLight) return;
	FVector Location=Pawn->GetPawnViewLocation();FRotator Rotation=Pawn->GetBaseAimRotation();
	if(Pawn->IsLocallyControlled())
		if(const auto* PC=Cast<APlayerController>(Pawn->GetController());PC && PC->PlayerCameraManager)
		{ Location=PC->PlayerCameraManager->GetCameraLocation();Rotation=PC->PlayerCameraManager->GetCameraRotation(); }
	BlackoutLight->SetWorldLocationAndRotation(Location+Rotation.RotateVector(FVector(15,10,-8)),Rotation);
}
void UMD_ScannerComponent::TickComponent(float DeltaSeconds,ELevelTick TickType,FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaSeconds,TickType,ThisTickFunction);RefreshBlackoutLight();
}
void UMD_ScannerComponent::HandleBlackoutChanged(bool) { RefreshBlackoutLight(); }
void UMD_ScannerComponent::HandleHeldActorChanged(AActor*) { RefreshBlackoutLight(); }

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
	return NSLOCTEXT("MDScanner", "InternalMode", "내부 구조물");
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
	// 내부 구조물 ↔ 오염물·따개비 2모드 토글
	ScanMode = ScanMode == EMD_ScanMode::InternalStructure ? EMD_ScanMode::Cleanup : EMD_ScanMode::InternalStructure;
	RefreshModeIndicator();
	MDAudioFeedback::LocalPawn(Pawn, TEXT("SFX.Tool.Scanner.ModeSwitch"));
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
	if (Mode != EMD_ScanMode::InternalStructure && (!WallHighlights.IsEmpty() || !StainHighlights.IsEmpty())) EnableWallPostProcess();
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
	for (const TWeakObjectPtr<AMD_SurfaceStain>& Stain : StainHighlights)
		if (Stain.IsValid()) Stain->SetScanHighlighted(this, false);
	StainHighlights.Empty();
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
	UMD_ToolDefinition* Current = BoundHotbar.IsValid() ? BoundHotbar->GetCurrentTool() : nullptr;
	if (Current != LastTool.Get())
	{
		ClearScan();
		LastTool = Current;
	}
	RefreshModeIndicator();
	RefreshBlackoutLight();
	// Send the selected-slot change promptly even when an idle character's adaptive replication is slow.
	if(GetOwner() && GetOwner()->HasAuthority()) GetOwner()->ForceNetUpdate();
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
	ClearScan(false);
	IssuedRequestId = 0;
	AcceptedRequestId = 0;
	CancelledRequestId = 0;
	if (ModeIndicator) ModeIndicator->RemoveFromParent();
	ModeIndicator = nullptr;
	RefreshModeIndicator();
	RefreshBlackoutLight();
}

void UMD_ScannerComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	if(GetWorld()) GetWorld()->GetTimerManager().ClearTimer(BlackoutLightTimer);
	if(BoundPowerManager.IsValid()) BoundPowerManager->OnBlackoutChanged.RemoveDynamic(this,&ThisClass::HandleBlackoutChanged);
	if(const auto* Character=Cast<AMD_Character>(GetOwner()))
		if(auto* Interaction=Character->GetInteractionComponent()) Interaction->OnHeldActorChanged.RemoveDynamic(this,&ThisClass::HandleHeldActorChanged);
	if(BlackoutLight) { BlackoutLight->DestroyComponent();BlackoutLight=nullptr; }
	ClearScan(false);
	if (ModeIndicator) ModeIndicator->RemoveFromParent();
	ModeIndicator = nullptr;
	if (APawn* Pawn = Cast<APawn>(GetOwner()))
		Pawn->ReceiveControllerChangedDelegate.RemoveDynamic(this, &ThisClass::HandleControllerChanged);
	if (BoundHotbar.IsValid())
		BoundHotbar->OnHotbarChanged.RemoveDynamic(this, &ThisClass::HandleHotbarChanged);
	Super::EndPlay(Reason);
}
