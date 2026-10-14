#include "World/Facility/MD_FingerprintPad.h"
#include "Audio/MD_GameplayAudioFeedback.h"
#include "World/Facility/MD_FingerprintInterlock.h"
#include "Component/Interaction/MD_InteractionComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"
#include "Gameplay/Collision/MD_CollisionChannels.h"
#include "Net/UnrealNetwork.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/StaticMesh.h"

AMD_FingerprintPad::AMD_FingerprintPad()
{
	bReplicates = true; bAlwaysRelevant = true;
	PrimaryActorTick.bCanEverTick = true; PrimaryActorTick.TickInterval = 0.1f;
	InteractionBounds = CreateDefaultSubobject<UBoxComponent>(TEXT("InteractionBounds"));
	SetRootComponent(InteractionBounds);
	InteractionBounds->SetBoxExtent(FVector(8,22,32));
	InteractionBounds->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InteractionBounds->SetCollisionResponseToAllChannels(ECR_Ignore);
	InteractionBounds->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	InteractionBounds->SetCollisionResponseToChannel(MDCollisionChannels::InteractTrace, ECR_Block);
	PadMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PadMesh"));
	PadMesh->SetupAttachment(RootComponent); PadMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FingerprintDisplay = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FingerprintDisplay"));
	FingerprintDisplay->SetupAttachment(RootComponent);
	FingerprintDisplay->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FingerprintDisplay->SetCastShadow(false);
	FingerprintDisplay->SetRelativeLocation(FingerprintOffset);
	FingerprintDisplay->SetRelativeRotation(FRotator(0,90,-90));
	FingerprintDisplay->SetRelativeScale3D(FVector(FingerprintSize.X/100.f,FingerprintSize.Y/100.f,1.f));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Plane(TEXT("/Engine/BasicShapes/Plane.Plane"));
	if (Plane.Succeeded()) FingerprintDisplay->SetStaticMesh(Plane.Object);
	StatusLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("StatusLight"));
	StatusLight->SetupAttachment(RootComponent); StatusLight->SetRelativeLocation(FVector(14,0,18));
	StatusLight->SetIntensity(40.f); StatusLight->SetAttenuationRadius(70.f); StatusLight->SetCastShadows(false);
	StatusText = CreateDefaultSubobject<UTextRenderComponent>(TEXT("StatusText"));
	StatusText->SetupAttachment(RootComponent); StatusText->SetRelativeLocation(FVector(10,0,40));
	StatusText->SetHorizontalAlignment(EHTA_Center); StatusText->SetWorldSize(9.f);
}

void AMD_FingerprintPad::ApplyDisplayLayout()
{
	FingerprintDisplay->SetRelativeLocation(FingerprintOffset);
	FingerprintDisplay->SetRelativeRotation(FRotator(0,90,-90));
	FingerprintDisplay->SetRelativeScale3D(FVector(FingerprintSize.X/100.f,FingerprintSize.Y/100.f,1.f));
}

void AMD_FingerprintPad::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyDisplayLayout();
}

void AMD_FingerprintPad::BeginPlay()
{
	Super::BeginPlay();
	// Saved component overrides must not retain the old oversized display.
	ApplyDisplayLayout();
	if (GetNetMode() != NM_DedicatedServer)
	{
		SetActorTickInterval(0.f);
		DisplayMaterial = FingerprintDisplay->CreateDynamicMaterialInstance(0);
		DisplayOpacity = IsValid(Interlock) && Interlock->IsActive() ? 0.15f : 1.f;
		if (DisplayMaterial) DisplayMaterial->SetScalarParameterValue(TEXT("ScanOpacity"),DisplayOpacity);
	}
	else SetActorTickEnabled(false);
}

void AMD_FingerprintPad::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMD_FingerprintPad, Interlock);
	DOREPLIFETIME(AMD_FingerprintPad, PadIndex);
	DOREPLIFETIME(AMD_FingerprintPad, ActiveScanner);
	DOREPLIFETIME(AMD_FingerprintPad, ScanVisualStartTime);
}

void AMD_FingerprintPad::NotifyScanAccepted()
{
	if (!HasAuthority()) return;
	ScanVisualStartTime = GetWorld()->GetTimeSeconds();
	ForceNetUpdate();
}

bool AMD_FingerprintPad::CanMaintainScan(const APawn* Pawn) const
{
	if (!IsValid(Pawn) || Pawn->GetWorld() != GetWorld() || !Pawn->GetController()) return false;
	const auto* Interaction = Pawn->FindComponentByClass<UMD_InteractionComponent>();
	const APlayerState* PS = Pawn->GetPlayerState();
	const AGameStateBase* GS = GetWorld()->GetGameState();
	if (!Interaction || !Interaction->CanOwnerInteract() || Interaction->IsHolding()
		|| !IsValid(PS) || PS->IsInactive() || PS->IsSpectator() || PS->IsOnlyASpectator()
		|| !GS || !GS->PlayerArray.Contains(PS) || !FMath::IsFinite(ScanReach) || ScanReach <= 0.f) return false;
	FVector Origin; FRotator Rotation; Pawn->GetController()->GetPlayerViewPoint(Origin,Rotation);
	FHitResult Hit; FCollisionQueryParams Params(SCENE_QUERY_STAT(FingerprintScan),true,Pawn);
	return GetWorld()->LineTraceSingleByChannel(Hit,Origin,Origin+Rotation.Vector()*ScanReach,ECC_Visibility,Params)
		&& Hit.GetActor() == this;
}

void AMD_FingerprintPad::SetInteractionHighlighted_Implementation(bool bHighlighted)
{
	bInteractionHighlighted = bHighlighted;
	RefreshHighlight();
}

void AMD_FingerprintPad::RefreshHighlight()
{
	if (GetNetMode() == NM_DedicatedServer) return;
	const bool bFault = IsValid(Interlock) && Interlock->IsActive();
	const int32 Stencil = bFault ? FaultHighlightStencilValue : IMD_Interactable::HighlightStencilValue;
	const bool bEnabled = bFault || bInteractionHighlighted;
	// Looking away must not clear a locator needed by the entire team.
	if (PadMesh->CustomDepthStencilValue != Stencil) PadMesh->SetCustomDepthStencilValue(Stencil);
	if (PadMesh->bRenderCustomDepth != bEnabled) PadMesh->SetRenderCustomDepth(bEnabled);
}

FText AMD_FingerprintPad::GetInteractionDisplayName_Implementation() const
{
	return FText::Format(NSLOCTEXT("MDFingerprint","Name","지문 패드 {0}"),FText::FromString(PadIndex==0?TEXT("A"):TEXT("B")));
}

FText AMD_FingerprintPad::GetInteractionPrompt_Implementation(AActor*) const
{
	if (!IsValid(Interlock) || !Interlock->IsActive()) return NSLOCTEXT("MDFingerprint","Normal","처리장 정상 가동");
	if (Interlock->SoloFirstPad != INDEX_NONE)
	{
		const auto* GS = GetWorld()->GetGameState();
		const double Now = GS ? GS->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
		const int32 Remaining = FMath::Max(0, FMath::CeilToInt(Interlock->SoloDeadline - Now));
		return FText::Format(Interlock->SoloFirstPad == PadIndex
			? NSLOCTEXT("MDFingerprint","SoloFirst","인증 완료 · 반대 패드로 이동 · {0}초 남음")
			: NSLOCTEXT("MDFingerprint","SoloSecond","[E 유지] 반대 패드 인증 · {0}초 남음"), FText::AsNumber(Remaining));
	}
	// 비용 정보 없이 현재 수행할 인증 행동만 표시
	return NSLOCTEXT("MDFingerprint", "Prompt", "지문 인증");
}

bool AMD_FingerprintPad::CanInteract_Implementation(AActor* Interactor) const
{
	return IsValid(Interlock) && Interlock->IsActive() && IsValid(Interactor) && Interactor->IsA<APawn>();
}

void AMD_FingerprintPad::Interact_Implementation(AActor* Interactor)
{
	// 서버에서 직접 호출한 인증에 대한 연결 상태 검증
	if (HasAuthority() == true && IsValid(Interlock) == true && IsValid(Interlock->Fault) == true)
	{
		// 서버 직접 입력의 현재 고장 번호를 인증 판정에 전달
		Interlock->BeginScan(this, Cast<APawn>(Interactor), Interlock->Fault->GetSnapshot().FaultId);
	}
}

void AMD_FingerprintPad::InteractReleased_Implementation(AActor* Interactor)
{
	if (HasAuthority() && IsValid(Interlock)) Interlock->ReleaseScan(this,Cast<APawn>(Interactor));
}

void AMD_FingerprintPad::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (GetNetMode()==NM_DedicatedServer) return;
	RefreshHighlight();
	const bool bActive = IsValid(Interlock) && Interlock->IsActive();
	const bool bScanning = IsValid(ActiveScanner) || (bActive && Interlock->SoloFirstPad==PadIndex);
	if (bActive && bScanning && !ScanAudio.IsValid())
		ScanAudio = MDAudioFeedback::Attached(RootComponent, TEXT("SFX.Facility.Fingerprint.WaitLoop"));
	if (!bActive || !bScanning) MDAudioFeedback::Stop(this, ScanAudio);
	const auto* GS = GetWorld()->GetGameState();
	const double Now = GS ? GS->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
	const double Age = FMath::Max(0., Now - ScanVisualStartTime);
	const float FadeDuration = FMath::IsFinite(ScanFadeInSeconds) ? FMath::Max(0.05f, ScanFadeInSeconds) : 0.45f;
	const bool bRecentScan = ScanVisualStartTime >= 0. && Age < FadeDuration;
	if (ScanVisualStartTime >= 0. && ScanVisualStartTime > LastAudioScanTime)
	{
		LastAudioScanTime = ScanVisualStartTime;
		if (bRecentScan) MDAudioFeedback::World(this, TEXT("SFX.Facility.Fingerprint.Scan"));
	}
	float ScanProgress = 1.f;
	if (bRecentScan && (bScanning || !bActive))
	{
		const float Alpha = FMath::Clamp(float(Age / FadeDuration), 0.f, 1.f);
		ScanProgress = Alpha * Alpha * (3.f - 2.f * Alpha);
	}
	const float Target = !bActive || bScanning ? 1.f : 0.15f;
	DisplayOpacity = FMath::FInterpConstantTo(DisplayOpacity, Target, DeltaSeconds, 1.f / 0.18f);
	const FLinearColor Color = !bActive ? FLinearColor(0.1f,0.9f,0.3f) : bScanning ? FLinearColor(0.1f,0.7f,1.f) : FLinearColor(1.f,0.1f,0.03f);
	if (DisplayMaterial)
	{
		DisplayMaterial->SetVectorParameterValue(TEXT("Tint"),Color);
		DisplayMaterial->SetScalarParameterValue(TEXT("ScanOpacity"),DisplayOpacity);
		DisplayMaterial->SetScalarParameterValue(TEXT("ScanProgress"),ScanProgress);
	}
	StatusLight->SetIntensity(bActive&&!bScanning ? (FMath::Frac(GetWorld()->GetTimeSeconds())<0.5f?60.f:10.f) : 40.f);
	const int32 PresentationState = !bActive ? 0 : bScanning ? 2 : 1;
	if (LastPresentationState != PresentationState)
	{
		LastPresentationState = PresentationState;
		StatusLight->SetLightColor(Color);
		const TCHAR* State = !bActive ? TEXT("READY") : bScanning ? TEXT("SCANNING") : TEXT("AUTH REQUIRED");
		StatusText->SetText(FText::FromString(FString::Printf(TEXT("%s / %s"),PadIndex==0?TEXT("A"):TEXT("B"),State)));
		StatusText->SetTextRenderColor(Color.ToFColor(true));
	}
}

void AMD_FingerprintPad::EndPlay(const EEndPlayReason::Type Reason)
{
	MDAudioFeedback::Stop(this, ScanAudio, 0.f);
	if (HasAuthority() && IsValid(Interlock)) Interlock->ReleaseScan(this,ActiveScanner);
	Super::EndPlay(Reason);
}
