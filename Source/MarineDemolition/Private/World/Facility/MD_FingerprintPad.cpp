#include "World/Facility/MD_FingerprintPad.h"
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
	FingerprintDisplay->SetRelativeLocation(FVector(9,0,0));
	FingerprintDisplay->SetRelativeRotation(FRotator(0,90,-90));
	FingerprintDisplay->SetRelativeScale3D(FVector(0.22f,0.32f,1.f));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Plane(TEXT("/Engine/BasicShapes/Plane.Plane"));
	if (Plane.Succeeded()) FingerprintDisplay->SetStaticMesh(Plane.Object);
	StatusLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("StatusLight"));
	StatusLight->SetupAttachment(RootComponent); StatusLight->SetRelativeLocation(FVector(14,0,18));
	StatusLight->SetIntensity(40.f); StatusLight->SetAttenuationRadius(70.f); StatusLight->SetCastShadows(false);
	StatusText = CreateDefaultSubobject<UTextRenderComponent>(TEXT("StatusText"));
	StatusText->SetupAttachment(RootComponent); StatusText->SetRelativeLocation(FVector(10,0,40));
	StatusText->SetHorizontalAlignment(EHTA_Center); StatusText->SetWorldSize(9.f);
}

void AMD_FingerprintPad::BeginPlay()
{
	Super::BeginPlay();
	if (GetNetMode() != NM_DedicatedServer)
		DisplayMaterial = FingerprintDisplay->CreateDynamicMaterialInstance(0);
}

void AMD_FingerprintPad::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMD_FingerprintPad, Interlock);
	DOREPLIFETIME(AMD_FingerprintPad, PadIndex);
	DOREPLIFETIME(AMD_FingerprintPad, ActiveScanner);
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
	PadMesh->SetCustomDepthStencilValue(IMD_Interactable::HighlightStencilValue);
	PadMesh->SetRenderCustomDepth(bHighlighted);
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
	return FText::Format(NSLOCTEXT("MDFingerprint","Prompt","[E 유지] 지문 인증 · 고장 손실 {0}원/초"),FText::AsNumber(Interlock->LossPerSecond));
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
	const bool bActive = IsValid(Interlock) && Interlock->IsActive();
	const bool bScanning = IsValid(ActiveScanner) || (bActive && Interlock->SoloFirstPad==PadIndex);
	const FLinearColor Color = !bActive ? FLinearColor(0.1f,0.9f,0.3f) : bScanning ? FLinearColor(0.1f,0.7f,1.f) : FLinearColor(1.f,0.1f,0.03f);
	StatusLight->SetLightColor(Color);
	if (DisplayMaterial) DisplayMaterial->SetVectorParameterValue(TEXT("Tint"),Color);
	StatusLight->SetIntensity(bActive&&!bScanning ? (FMath::Frac(GetWorld()->GetTimeSeconds())<0.5f?60.f:10.f) : 40.f);
	const FString State = !bActive ? TEXT("READY") : bScanning ? TEXT("SCANNING") : TEXT("AUTH REQUIRED");
	StatusText->SetText(FText::FromString(FString::Printf(TEXT("%s / %s"),PadIndex==0?TEXT("A"):TEXT("B"),*State)));
	StatusText->SetTextRenderColor(Color.ToFColor(true));
}

void AMD_FingerprintPad::EndPlay(const EEndPlayReason::Type Reason)
{
	if (HasAuthority() && IsValid(Interlock)) Interlock->ReleaseScan(this,ActiveScanner);
	Super::EndPlay(Reason);
}
