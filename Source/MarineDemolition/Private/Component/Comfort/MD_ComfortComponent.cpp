// Fill out your copyright notice in the Description page of Project Settings.

#include "Component/Comfort/MD_ComfortComponent.h"

#include "Camera/CameraComponent.h"
#include "Engine/Engine.h"
#include "GameFramework/GameUserSettings.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Player/MD_ComfortSettings.h"
#include "Util/MD_LogUtility.h"

namespace
{
	/** 반감기를 지수 감쇠 계수로 바꿀 때 쓰는 ln(2). */
	constexpr float ComfortLogTwo = 0.6931472f;
}

UMD_ComfortComponent::UMD_ComfortComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UMD_ComfortComponent::BeginPlay()
{
	Super::BeginPlay();

	// 멀미 완화는 화면을 보는 로컬 플레이어에게만 의미가 있다.
	// 원격 플레이어와 서버 전용 인스턴스에서는 카메라도 프레임도 건드릴 대상이 아니다.
	if (GetLocalPlayerController() == nullptr)
	{
		SetComponentTickEnabled(false);
		return;
	}

	ApplyCameraSettings();
	ApplyFramePacingSettings();
}

void UMD_ComfortComponent::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	DrainPendingLookInput(DeltaTime);
}

FVector2D UMD_ComfortComponent::ShapeLookInput(const FVector2D& RawLookInput)
{
	const UMD_ComfortSettings* Settings = UMD_ComfortSettings::Get();
	if (IsValid(Settings) == false)
	{
		return RawLookInput;
	}

	FVector2D ShapedInput = RawLookInput;

	// 축별로 커브를 먹이면 대각선 입력에서 방향이 휘어진다. 크기에만 계수를 곱해 방향을 보존한다.
	const float InputMagnitude = RawLookInput.Size();
	if (Settings->LookResponseExponent > 1.f && InputMagnitude > UE_KINDA_SMALL_NUMBER)
	{
		const float ReferenceDelta = FMath::Max(Settings->LookResponseReferenceDelta, UE_KINDA_SMALL_NUMBER);
		const float NormalizedMagnitude = FMath::Min(InputMagnitude / ReferenceDelta, 1.f);
		ShapedInput = RawLookInput * FMath::Pow(NormalizedMagnitude, Settings->LookResponseExponent - 1.f);
	}

	if (Settings->LookSmoothingHalfLife <= 0.f)
	{
		// 기본 경로. 누적을 거치지 않으므로 프레임 지연이 전혀 생기지 않는다.
		return ShapedInput;
	}

	PendingLookInput += ShapedInput;
	return FVector2D::ZeroVector;
}

void UMD_ComfortComponent::ApplyCameraSettings() const
{
	const UMD_ComfortSettings* Settings = UMD_ComfortSettings::Get();
	UCameraComponent* Camera = FindOwnerCamera();
	if (IsValid(Settings) == false || IsValid(Camera) == false)
	{
		return;
	}

	if (Settings->bOverrideFieldOfView)
	{
		Camera->SetFieldOfView(Settings->FieldOfView);
	}

	if (Settings->bOverrideMotionBlur)
	{
		// 레벨의 PostProcessVolume을 고치면 맵 에셋이 변경되어 다른 작업과 충돌한다.
		// 카메라 후처리는 볼륨보다 뒤에 적용되므로, 여기서 누르면 맵을 건드리지 않고 이긴다.
		//
		// PostProcessBlendWeight는 건드리지 않는다. 0으로 꺼둔 카메라를 여기서 살리면
		// 모션 블러만이 아니라 그 카메라에 걸린 다른 오버라이드까지 한꺼번에 되살아난다.
		// 대신 적용이 안 되는 상황임을 알려서 BP 쪽에서 판단하게 한다.
		if (Camera->PostProcessBlendWeight <= 0.f)
		{
			UE_LOG(MarineDemolition, Warning,
				TEXT("[Comfort] 카메라 PostProcessBlendWeight가 %.2f라서 모션 블러 설정이 적용되지 않는다. ")
				TEXT("BP에서 값을 올리거나 MD Comfort의 모션 블러 오버라이드를 끌 것"),
				Camera->PostProcessBlendWeight);
		}

		Camera->PostProcessSettings.bOverride_MotionBlurAmount = true;
		Camera->PostProcessSettings.MotionBlurAmount = Settings->MotionBlurAmount;
	}
}

void UMD_ComfortComponent::ApplyFramePacingSettings() const
{
	const UMD_ComfortSettings* Settings = UMD_ComfortSettings::Get();
	if (IsValid(Settings) == false || GEngine == nullptr)
	{
		return;
	}

	const bool bWantsFrameRateLimit = Settings->FrameRateLimit > 0.f;
	const bool bWantsVSyncOverride = Settings->VSyncOverride != EMD_ComfortOverride::Unchanged;
	if (bWantsFrameRateLimit == false && bWantsVSyncOverride == false)
	{
		return;
	}

	// r.VSync를 콘솔 변수로 직접 Set하면 무시된다. 에디터가 그 변수를 SetByGameSetting보다
	// 높은 우선순위로 잡고 있어서다. UGameUserSettings가 정식 경로이고, 나중에 옵션 UI를
	// 붙일 때도 같은 값을 공유하게 된다. 프레임 상한도 같은 경로로 넣어야
	// ApplyNonResolutionSettings가 t.MaxFPS를 저장값으로 되돌려 버리지 않는다.
	UGameUserSettings* UserSettings = GEngine->GetGameUserSettings();
	if (IsValid(UserSettings) == false)
	{
		return;
	}

	if (bWantsFrameRateLimit)
	{
		UserSettings->SetFrameRateLimit(Settings->FrameRateLimit);
	}

	if (bWantsVSyncOverride)
	{
		UserSettings->SetVSyncEnabled(Settings->VSyncOverride == EMD_ComfortOverride::ForceOn);
	}

	// ApplySettings는 ini 저장까지 해서 플레이어가 고른 값을 덮어쓴다.
	// 저장 없이 적용만 하는 ApplyNonResolutionSettings를 쓴다.
	UserSettings->ApplyNonResolutionSettings();
}

void UMD_ComfortComponent::DrainPendingLookInput(float DeltaTime)
{
	if (PendingLookInput.IsNearlyZero())
	{
		PendingLookInput = FVector2D::ZeroVector;
		return;
	}

	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (IsValid(OwnerPawn) == false)
	{
		PendingLookInput = FVector2D::ZeroVector;
		return;
	}

	const UMD_ComfortSettings* Settings = UMD_ComfortSettings::Get();
	const float HalfLife = IsValid(Settings) == true ? Settings->LookSmoothingHalfLife : 0.f;

	// 반감기가 0이면 스무딩이 꺼진 것이다. 설정을 런타임에 0으로 되돌렸을 때
	// 이미 쌓여 있던 입력이 남지 않도록 이번 프레임에 전부 털어낸다.
	FVector2D InputStep = PendingLookInput;
	if (HalfLife > 0.f && DeltaTime > 0.f)
	{
		const float DecayAlpha = FMath::Clamp(1.f - FMath::Exp(-ComfortLogTwo * DeltaTime / HalfLife), 0.f, 1.f);
		InputStep = PendingLookInput * DecayAlpha;
	}

	PendingLookInput -= InputStep;

	OwnerPawn->AddControllerYawInput(InputStep.X);
	OwnerPawn->AddControllerPitchInput(InputStep.Y);
}

UCameraComponent* UMD_ComfortComponent::FindOwnerCamera() const
{
	const AActor* OwnerActor = GetOwner();
	return IsValid(OwnerActor) == true ? OwnerActor->FindComponentByClass<UCameraComponent>() : nullptr;
}

APlayerController* UMD_ComfortComponent::GetLocalPlayerController() const
{
	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (IsValid(OwnerPawn) == false || OwnerPawn->IsLocallyControlled() == false)
	{
		return nullptr;
	}

	return Cast<APlayerController>(OwnerPawn->GetController());
}
