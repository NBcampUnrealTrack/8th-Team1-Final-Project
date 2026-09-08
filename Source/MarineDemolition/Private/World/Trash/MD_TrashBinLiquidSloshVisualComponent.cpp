// Copyright Epic Games, Inc. All Rights Reserved.

#include "World/Trash/MD_TrashBinLiquidSloshVisualComponent.h"

#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"

namespace MD_TrashBinLiquidSloshPrivate
{
	constexpr float MaximumSurfaceDisplacementCm = 1.5f;
	constexpr float SurfaceDepthFraction = 0.35f;
	constexpr float FullBinHeadroomAllowanceCm = 0.25f;
	constexpr float GravityAccelerationCmPerSecondSquared = 980.0f;
	constexpr float LinearAccelerationInfluence = 0.65f;
	constexpr float AngularVelocityInfluence = 0.10f;
	constexpr float NaturalFrequencyHz = 2.0f;
	constexpr float DampingRatio = 0.92f;
	constexpr float MaximumTickDeltaSeconds = 0.10f;
	constexpr float MaximumSpringSubstepSeconds = 1.0f / 120.0f;
	constexpr float MinimumVisibleLiquid = 1.0e-4f;
	constexpr float MinimumWrittenAmplitudeCm = 1.0e-4f;

	bool IsFiniteVector(const FVector& Value)
	{
		return FMath::IsFinite(Value.X)
			&& FMath::IsFinite(Value.Y)
			&& FMath::IsFinite(Value.Z);
	}

	bool IsFiniteVector2D(const FVector2D& Value)
	{
		return FMath::IsFinite(Value.X)
			&& FMath::IsFinite(Value.Y);
	}

	FVector2D ClampToMagnitude(
		const FVector2D& Value,
		const float MaximumMagnitude)
	{
		if (!IsFiniteVector2D(Value)
			|| !FMath::IsFinite(MaximumMagnitude)
			|| MaximumMagnitude <= 0.0f)
		{
			return FVector2D::ZeroVector;
		}

		const double MagnitudeSquared = Value.SizeSquared();
		const double MaximumMagnitudeSquared =
			static_cast<double>(MaximumMagnitude) * MaximumMagnitude;
		if (MagnitudeSquared <= MaximumMagnitudeSquared)
		{
			return Value;
		}

		const double Magnitude = FMath::Sqrt(MagnitudeSquared);
		return Magnitude > UE_SMALL_NUMBER
			? Value * (static_cast<double>(MaximumMagnitude) / Magnitude)
			: FVector2D::ZeroVector;
	}
}

UMD_TrashBinLiquidSloshVisualComponent::
	UMD_TrashBinLiquidSloshVisualComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.bAllowTickOnDedicatedServer = false;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
	SetIsReplicatedByDefault(false);
}

void UMD_TrashBinLiquidSloshVisualComponent::InitializeVisual(
	UStaticMeshComponent* const InLiquidFillMesh,
	UPrimitiveComponent* const InMotionSource)
{
	ResetSlosh(/*bDisableTick=*/true);
	bMotionAwake = false;
	bHasPreviousMotionSample = false;
	LiquidFillMesh = InLiquidFillMesh;
	MotionSource = InMotionSource;
	ResetSlosh(/*bDisableTick=*/true);
}

void UMD_TrashBinLiquidSloshVisualComponent::SetLiquidVisualState(
	const float InFillRatio,
	const float InVisibleHeightCm,
	const float InFullHeightCm)
{
	if (!FMath::IsFinite(InFillRatio)
		|| !FMath::IsFinite(InVisibleHeightCm)
		|| !FMath::IsFinite(InFullHeightCm)
		|| InFillRatio <= 0.0f
		|| InVisibleHeightCm <= 0.0f
		|| InFullHeightCm <= 0.0f)
	{
		FillRatio = 0.0f;
		VisibleHeightCm = 0.0f;
		FullHeightCm = 0.0f;
		MaximumAmplitudeCm = 0.0f;
		bMotionAwake = false;
		bHasPreviousMotionSample = false;
		ResetSlosh(/*bDisableTick=*/true);
		return;
	}

	FillRatio = FMath::Clamp(InFillRatio, 0.0f, 1.0f);
	FullHeightCm = InFullHeightCm;
	VisibleHeightCm = FMath::Clamp(
		InVisibleHeightCm,
		0.0f,
		FullHeightCm);
	MaximumAmplitudeCm = CalculateMaximumAmplitudeCm(
		VisibleHeightCm,
		FullHeightCm);
	if (!HasVisibleLiquid())
	{
		bMotionAwake = false;
		ResetSlosh(/*bDisableTick=*/true);
		return;
	}

	ClampStateToAmplitudeLimit();
	WriteCustomPrimitiveData();
	if (!bMotionAwake && IsValid(MotionSource))
	{
		if (MotionSource->RigidBodyIsAwake())
		{
			NotifyPhysicsWake();
		}
		else
		{
			// JIP나 초기 spawn부터 잠든 통도 현재 기울기를 한 번 재구성한다.
			NotifyPhysicsSleep();
		}
	}
}

void UMD_TrashBinLiquidSloshVisualComponent::NotifyPhysicsWake()
{
	if (!IsValid(MotionSource))
	{
		bMotionAwake = false;
		bHasPreviousMotionSample = false;
		ResetSlosh(/*bDisableTick=*/true);
		return;
	}

	StartMotion(
		MotionSource->GetPhysicsLinearVelocity(),
		MotionSource->GetPhysicsAngularVelocityInRadians());
}

void UMD_TrashBinLiquidSloshVisualComponent::NotifyPhysicsSleep()
{
	bMotionAwake = false;
	bHasPreviousMotionSample = false;
	if (!HasVisibleLiquid() || !IsValid(MotionSource))
	{
		ResetSlosh(/*bDisableTick=*/true);
		return;
	}

	// 정지했다고 통 로컬 평면으로 갑자기 돌아가면 기울어진 통에서 부자연스럽다.
	// 관성 history만 버리고 현재 중력 방향의 정적 경사를 한 번 기록한 뒤 Tick을 끈다.
	SloshVelocityCmPerSecond = FVector2D::ZeroVector;
	SloshOffsetCm = CalculateTargetOffsetCm(
		FVector::ZeroVector,
		FVector::ZeroVector,
		1.0f / 60.0f);
	ClampStateToAmplitudeLimit();
	WriteCustomPrimitiveData();
	SetComponentTickEnabled(false);
}

void UMD_TrashBinLiquidSloshVisualComponent::NotifyReplicatedMotion(
	const FVector& LinearVelocityCmPerSecond,
	const FVector& AngularVelocityRadiansPerSecond,
	const bool bPhysicsAwake)
{
	if (!bPhysicsAwake)
	{
		NotifyPhysicsSleep();
		return;
	}

	StartMotion(
		LinearVelocityCmPerSecond,
		AngularVelocityRadiansPerSecond);
}

bool UMD_TrashBinLiquidSloshVisualComponent::ShouldCreateForNetMode(
	const ENetMode NetMode)
{
	return NetMode != NM_DedicatedServer;
}

float UMD_TrashBinLiquidSloshVisualComponent::CalculateMaximumAmplitudeCm(
	const float InVisibleHeightCm,
	const float InFullHeightCm)
{
	if (!FMath::IsFinite(InVisibleHeightCm)
		|| !FMath::IsFinite(InFullHeightCm)
		|| InVisibleHeightCm <= 0.0f
		|| InFullHeightCm <= 0.0f)
	{
		return 0.0f;
	}

	const float ClampedVisibleHeightCm = FMath::Clamp(
		InVisibleHeightCm,
		0.0f,
		InFullHeightCm);
	const float DepthLimitCm =
		ClampedVisibleHeightCm
		* MD_TrashBinLiquidSloshPrivate::SurfaceDepthFraction;
	const float HeadroomLimitCm =
		FMath::Max(0.0f, InFullHeightCm - ClampedVisibleHeightCm)
		+ MD_TrashBinLiquidSloshPrivate::FullBinHeadroomAllowanceCm;
	return FMath::Max(
		0.0f,
		FMath::Min(
			MD_TrashBinLiquidSloshPrivate::MaximumSurfaceDisplacementCm,
			FMath::Min(DepthLimitCm, HeadroomLimitCm)));
}

void UMD_TrashBinLiquidSloshVisualComponent::TickComponent(
	const float DeltaTime,
	const ELevelTick TickType,
	FActorComponentTickFunction* const ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bMotionAwake || !CanRunCosmeticVisual() || !HasVisibleLiquid())
	{
		NotifyPhysicsSleep();
		return;
	}
	if (!FMath::IsFinite(DeltaTime) || DeltaTime <= 0.0f)
	{
		return;
	}

	FVector LinearVelocityCmPerSecond =
		MotionSource->GetPhysicsLinearVelocity();
	FVector AngularVelocityRadiansPerSecond =
		MotionSource->GetPhysicsAngularVelocityInRadians();
	if (!MD_TrashBinLiquidSloshPrivate::IsFiniteVector(
			LinearVelocityCmPerSecond))
	{
		LinearVelocityCmPerSecond = PreviousLinearVelocityCmPerSecond;
	}
	if (!MD_TrashBinLiquidSloshPrivate::IsFiniteVector(
			AngularVelocityRadiansPerSecond))
	{
		AngularVelocityRadiansPerSecond =
			PreviousAngularVelocityRadiansPerSecond;
	}

	const float SafeDeltaTime = FMath::Min(
		DeltaTime,
		MD_TrashBinLiquidSloshPrivate::MaximumTickDeltaSeconds);
	const FVector2D TargetOffsetCm = CalculateTargetOffsetCm(
		LinearVelocityCmPerSecond,
		AngularVelocityRadiansPerSecond,
		SafeDeltaTime);
	AdvanceHighViscositySpring(TargetOffsetCm, SafeDeltaTime);
	PreviousLinearVelocityCmPerSecond = LinearVelocityCmPerSecond;
	PreviousAngularVelocityRadiansPerSecond =
		AngularVelocityRadiansPerSecond;
	bHasPreviousMotionSample = true;
	WriteCustomPrimitiveData();
}

void UMD_TrashBinLiquidSloshVisualComponent::OnUnregister()
{
	bMotionAwake = false;
	bHasPreviousMotionSample = false;
	ResetSlosh(/*bDisableTick=*/true);
	LiquidFillMesh = nullptr;
	MotionSource = nullptr;
	Super::OnUnregister();
}

bool UMD_TrashBinLiquidSloshVisualComponent::CanRunCosmeticVisual() const
{
	return CanWriteCustomPrimitiveData() && IsValid(MotionSource);
}

bool UMD_TrashBinLiquidSloshVisualComponent::CanWriteCustomPrimitiveData() const
{
	if (!IsValid(LiquidFillMesh))
	{
		return false;
	}

	const UWorld* const World = GetWorld();
	return !IsValid(World)
		|| ShouldCreateForNetMode(World->GetNetMode());
}

bool UMD_TrashBinLiquidSloshVisualComponent::HasVisibleLiquid() const
{
	return CanRunCosmeticVisual()
		&& FMath::IsFinite(FillRatio)
		&& FillRatio > MD_TrashBinLiquidSloshPrivate::MinimumVisibleLiquid
		&& FMath::IsFinite(VisibleHeightCm)
		&& VisibleHeightCm > 0.0f
		&& FMath::IsFinite(FullHeightCm)
		&& FullHeightCm > 0.0f
		&& FMath::IsFinite(MaximumAmplitudeCm)
		&& MaximumAmplitudeCm > 0.0f;
}

void UMD_TrashBinLiquidSloshVisualComponent::StartMotion(
	const FVector& LinearVelocityCmPerSecond,
	const FVector& AngularVelocityRadiansPerSecond)
{
	if (!HasVisibleLiquid()
		|| !MD_TrashBinLiquidSloshPrivate::IsFiniteVector(
			LinearVelocityCmPerSecond)
		|| !MD_TrashBinLiquidSloshPrivate::IsFiniteVector(
			AngularVelocityRadiansPerSecond))
	{
		bMotionAwake = false;
		bHasPreviousMotionSample = false;
		ResetSlosh(/*bDisableTick=*/true);
		return;
	}

	PreviousLinearVelocityCmPerSecond = LinearVelocityCmPerSecond;
	PreviousAngularVelocityRadiansPerSecond =
		AngularVelocityRadiansPerSecond;
	bHasPreviousMotionSample = true;
	bMotionAwake = true;
	SetComponentTickEnabled(true);
}

void UMD_TrashBinLiquidSloshVisualComponent::ResetSlosh(
	const bool bDisableTick)
{
	SloshOffsetCm = FVector2D::ZeroVector;
	SloshVelocityCmPerSecond = FVector2D::ZeroVector;
	LastCustomPrimitiveData = FVector4(0.0, 0.0, 0.0, 0.0);
	if (CanWriteCustomPrimitiveData())
	{
		LiquidFillMesh->SetCustomPrimitiveDataVector4(
			SloshCustomPrimitiveDataIndex,
			LastCustomPrimitiveData);
	}
	if (bDisableTick)
	{
		SetComponentTickEnabled(false);
	}
}

void UMD_TrashBinLiquidSloshVisualComponent::ClampStateToAmplitudeLimit()
{
	SloshOffsetCm = MD_TrashBinLiquidSloshPrivate::ClampToMagnitude(
		SloshOffsetCm,
		MaximumAmplitudeCm);
	if (!MD_TrashBinLiquidSloshPrivate::IsFiniteVector2D(
			SloshVelocityCmPerSecond))
	{
		SloshVelocityCmPerSecond = FVector2D::ZeroVector;
	}

	const double AmplitudeCm = SloshOffsetCm.Size();
	if (AmplitudeCm >= MaximumAmplitudeCm
		&& AmplitudeCm > UE_SMALL_NUMBER)
	{
		const FVector2D SurfaceDirection =
			SloshOffsetCm / AmplitudeCm;
		const double OutwardSpeed = FVector2D::DotProduct(
			SloshVelocityCmPerSecond,
			SurfaceDirection);
		if (OutwardSpeed > 0.0)
		{
			SloshVelocityCmPerSecond -=
				SurfaceDirection * OutwardSpeed;
		}
	}
}

FVector2D
UMD_TrashBinLiquidSloshVisualComponent::CalculateTargetOffsetCm(
	const FVector& LinearVelocityCmPerSecond,
	const FVector& AngularVelocityRadiansPerSecond,
	const float DeltaTime) const
{
	if (!IsValid(MotionSource)
		|| !FMath::IsFinite(DeltaTime)
		|| DeltaTime <= 0.0f
		|| !MD_TrashBinLiquidSloshPrivate::IsFiniteVector(
			LinearVelocityCmPerSecond)
		|| !MD_TrashBinLiquidSloshPrivate::IsFiniteVector(
			AngularVelocityRadiansPerSecond))
	{
		return FVector2D::ZeroVector;
	}

	// CPD 방향은 물리 root가 아니라 실제 WPO가 적용되는 채움 메시의 로컬 축이다.
	const FTransform VisualTransform = LiquidFillMesh->GetComponentTransform();
	const FVector LocalWorldUp = VisualTransform
		.InverseTransformVectorNoScale(FVector::UpVector)
		.GetSafeNormal();
	FVector2D TargetOffsetCm(LocalWorldUp.X, LocalWorldUp.Y);
	TargetOffsetCm *= MaximumAmplitudeCm;

	if (bHasPreviousMotionSample)
	{
		const FVector WorldAccelerationCmPerSecondSquared =
			(LinearVelocityCmPerSecond
				- PreviousLinearVelocityCmPerSecond)
			/ DeltaTime;
		if (MD_TrashBinLiquidSloshPrivate::IsFiniteVector(
				WorldAccelerationCmPerSecondSquared))
		{
			const FVector LocalInertialAcceleration = VisualTransform
				.InverseTransformVectorNoScale(
					-WorldAccelerationCmPerSecondSquared);
			TargetOffsetCm += FVector2D(
				LocalInertialAcceleration.X,
				LocalInertialAcceleration.Y)
				* (MaximumAmplitudeCm
					* MD_TrashBinLiquidSloshPrivate::LinearAccelerationInfluence
					/ MD_TrashBinLiquidSloshPrivate::
						GravityAccelerationCmPerSecondSquared);
		}
	}

	const FVector LocalAngularVelocity = VisualTransform
		.InverseTransformVectorNoScale(
			AngularVelocityRadiansPerSecond);
	TargetOffsetCm += FVector2D(
		LocalAngularVelocity.Y,
		-LocalAngularVelocity.X)
		* (MaximumAmplitudeCm
			* MD_TrashBinLiquidSloshPrivate::AngularVelocityInfluence);

	return MD_TrashBinLiquidSloshPrivate::ClampToMagnitude(
		TargetOffsetCm,
		MaximumAmplitudeCm);
}

void UMD_TrashBinLiquidSloshVisualComponent::AdvanceHighViscositySpring(
	const FVector2D& TargetOffsetCm,
	const float DeltaTime)
{
	if (!MD_TrashBinLiquidSloshPrivate::IsFiniteVector2D(TargetOffsetCm)
		|| !FMath::IsFinite(DeltaTime)
		|| DeltaTime <= 0.0f)
	{
		SloshOffsetCm = FVector2D::ZeroVector;
		SloshVelocityCmPerSecond = FVector2D::ZeroVector;
		return;
	}

	const int32 SubstepCount = FMath::Clamp(
		FMath::CeilToInt(
			DeltaTime
			/ MD_TrashBinLiquidSloshPrivate::MaximumSpringSubstepSeconds),
		1,
		12);
	const float SubstepSeconds = DeltaTime / SubstepCount;
	const double NaturalAngularFrequency =
		2.0 * UE_PI * MD_TrashBinLiquidSloshPrivate::NaturalFrequencyHz;
	const double SpringCoefficient =
		NaturalAngularFrequency * NaturalAngularFrequency;
	const double DampingCoefficient =
		2.0
		* MD_TrashBinLiquidSloshPrivate::DampingRatio
		* NaturalAngularFrequency;

	for (int32 StepIndex = 0; StepIndex < SubstepCount; ++StepIndex)
	{
		const FVector2D SpringAcceleration =
			(TargetOffsetCm - SloshOffsetCm) * SpringCoefficient
			- SloshVelocityCmPerSecond * DampingCoefficient;
		SloshVelocityCmPerSecond +=
			SpringAcceleration * SubstepSeconds;
		SloshOffsetCm +=
			SloshVelocityCmPerSecond * SubstepSeconds;
		if (!MD_TrashBinLiquidSloshPrivate::IsFiniteVector2D(SloshOffsetCm)
			|| !MD_TrashBinLiquidSloshPrivate::IsFiniteVector2D(
				SloshVelocityCmPerSecond))
		{
			SloshOffsetCm = FVector2D::ZeroVector;
			SloshVelocityCmPerSecond = FVector2D::ZeroVector;
			break;
		}
		ClampStateToAmplitudeLimit();
	}
}

void UMD_TrashBinLiquidSloshVisualComponent::WriteCustomPrimitiveData()
{
	ClampStateToAmplitudeLimit();
	const double AmplitudeCm = SloshOffsetCm.Size();
	FVector2D Direction = FVector2D::ZeroVector;
	if (AmplitudeCm
		> MD_TrashBinLiquidSloshPrivate::MinimumWrittenAmplitudeCm)
	{
		Direction = SloshOffsetCm / AmplitudeCm;
	}

	LastCustomPrimitiveData = FVector4(
		Direction.X,
		Direction.Y,
		FMath::Clamp(
			AmplitudeCm,
			0.0,
			static_cast<double>(MaximumAmplitudeCm)),
		0.0);
	if (CanWriteCustomPrimitiveData())
	{
		LiquidFillMesh->SetCustomPrimitiveDataVector4(
			SloshCustomPrimitiveDataIndex,
			LastCustomPrimitiveData);
	}
}
