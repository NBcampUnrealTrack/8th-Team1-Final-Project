// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MD_TrashBinLiquidSloshVisualComponent.generated.h"

class UPrimitiveComponent;
class UStaticMeshComponent;

/**
 * 통 내부의 영속 채움 메시를 고점도 슬라임처럼 보이게 하는 로컬 전용 드라이버.
 *
 * 이 컴포넌트는 액체 원장이나 쏟김 판정을 소유하지 않는다. 외부에서 전달받은 현재
 * 수위와 물리 움직임만 읽고 LiquidFillMesh의 Custom Primitive Data 0~3번에
 * `RG=기울어지는 로컬 방향, B=진폭(cm), A=0`을 기록한다.
 */
UCLASS(Transient, NotBlueprintable)
class MARINEDEMOLITION_API UMD_TrashBinLiquidSloshVisualComponent final
	: public UActorComponent
{
	GENERATED_BODY()

public:
	UMD_TrashBinLiquidSloshVisualComponent();

	/** 런타임 생성 직후 한 번 호출해 시각 메시와 그 움직임 원본을 연결한다. */
	void InitializeVisual(
		UStaticMeshComponent* InLiquidFillMesh,
		UPrimitiveComponent* InMotionSource);

	/**
	 * 기존 수위 갱신이 계산한 결과를 전달한다. 이 값은 읽기 전용이며 컴포넌트가
	 * LiquidUnits/RewardUnits/Revision을 다시 계산하거나 변경하지 않는다.
	 */
	void SetLiquidVisualState(
		float InFillRatio,
		float InVisibleHeightCm,
		float InFullHeightCm);

	/** 로컬 Chaos wake 이벤트에서 현재 물리 속도를 읽고 시각 Tick을 시작한다. */
	void NotifyPhysicsWake();

	/** 로컬 Chaos sleep에서 관성을 버리고 현재 중력의 정적 경사만 남긴 채 Tick을 끈다. */
	void NotifyPhysicsSleep();

	/**
	 * 원격 peer의 ReplicatedMovement 수신 시 현재 속도와 sleep 상태를 전달한다.
	 * AngularVelocity는 radians/second 단위다.
	 */
	void NotifyReplicatedMotion(
		const FVector& LinearVelocityCmPerSecond,
		const FVector& AngularVelocityRadiansPerSecond,
		bool bPhysicsAwake);

	/** 전용 서버에는 이 cosmetic 드라이버를 만들지 않는다. */
	static bool ShouldCreateForNetMode(ENetMode NetMode);

	/** 현재 깊이와 남은 headroom을 함께 지키는 최대 WPO 진폭(cm). */
	static float CalculateMaximumAmplitudeCm(
		float VisibleHeightCm,
		float FullHeightCm);

	bool IsMotionAwake() const { return bMotionAwake; }
	float GetMaximumAmplitudeCm() const { return MaximumAmplitudeCm; }
	const FVector4& GetLastCustomPrimitiveData() const
	{
		return LastCustomPrimitiveData;
	}

	virtual void TickComponent(
		float DeltaTime,
		ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;
	virtual void OnUnregister() override;

	/** Material `SloshState`가 읽는 Custom Primitive Data의 첫 인덱스. */
	static constexpr int32 SloshCustomPrimitiveDataIndex = 0;

private:
	bool CanWriteCustomPrimitiveData() const;
	bool CanRunCosmeticVisual() const;
	bool HasVisibleLiquid() const;
	void StartMotion(
		const FVector& LinearVelocityCmPerSecond,
		const FVector& AngularVelocityRadiansPerSecond);
	void ResetSlosh(bool bDisableTick);
	void ClampStateToAmplitudeLimit();
	FVector2D CalculateTargetOffsetCm(
		const FVector& LinearVelocityCmPerSecond,
		const FVector& AngularVelocityRadiansPerSecond,
		float DeltaTime) const;
	void AdvanceHighViscositySpring(
		const FVector2D& TargetOffsetCm,
		float DeltaTime);
	void WriteCustomPrimitiveData();

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> LiquidFillMesh;

	UPROPERTY(Transient)
	TObjectPtr<UPrimitiveComponent> MotionSource;

	FVector PreviousLinearVelocityCmPerSecond = FVector::ZeroVector;
	FVector PreviousAngularVelocityRadiansPerSecond = FVector::ZeroVector;
	FVector2D SloshOffsetCm = FVector2D::ZeroVector;
	FVector2D SloshVelocityCmPerSecond = FVector2D::ZeroVector;
	FVector4 LastCustomPrimitiveData = FVector4(0.0, 0.0, 0.0, 0.0);
	float FillRatio = 0.0f;
	float VisibleHeightCm = 0.0f;
	float FullHeightCm = 0.0f;
	float MaximumAmplitudeCm = 0.0f;
	bool bHasPreviousMotionSample = false;
	bool bMotionAwake = false;
};
