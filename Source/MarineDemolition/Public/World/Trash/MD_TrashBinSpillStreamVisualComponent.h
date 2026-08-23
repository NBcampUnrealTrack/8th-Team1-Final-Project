// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MD_TrashBinSpillStreamVisualComponent.generated.h"

class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;
struct FMD_TrashBinSpillCosmeticResult;

/**
 * 성공한 지속 기울기 쏟김을 통 입구에서 바닥 접점까지 잇는 로컬 전용 줄기.
 *
 * 이 컴포넌트는 네 개의 무충돌 원통을 짧은 quadratic Bezier 구간에 맞춰 놓는다.
 * 서버 원장이나 Wet 상태는 소유하지 않으며, unreliable cosmetic payload가 도착한
 * 현재 관찰 peer에서만 0.50초 lease를 갱신한다.
 */
UCLASS(Transient, NotBlueprintable)
class MARINEDEMOLITION_API UMD_TrashBinSpillStreamVisualComponent final
	: public UActorComponent
{
	GENERATED_BODY()

public:
	UMD_TrashBinSpillStreamVisualComponent();

	/** 렌더링 peer의 BeginPlay에서 한 번 호출해 숨겨진 네 구간을 만든다. */
	void InitializeVisual();

	/** SustainedTilt 성공만 lease를 시작/갱신한다. Impact와 무효 payload는 무변경이다. */
	bool PlayCommittedPulse(
		const FMD_TrashBinSpillCosmeticResult& Result);

	/** 전용 서버에는 이 transient cosmetic을 만들지 않는다. */
	static bool ShouldCreateForNetMode(ENetMode NetMode);

	bool IsStreamActive() const { return bStreamActive; }
	int32 GetSegmentCount() const { return SegmentVisuals.Num(); }

	virtual void TickComponent(
		float DeltaTime,
		ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;
	virtual void OnUnregister() override;

	static constexpr int32 VisualSegmentCount = 4;
	static constexpr double DiameterShrinkSeconds = 0.10;

private:
	bool PlayCommittedPulseAtTime(
		const FMD_TrashBinSpillCosmeticResult& Result,
		double CurrentWorldSeconds);
	void RefreshVisualAtTime(double CurrentWorldSeconds);
	void UpdateSegmentTransforms(float DiameterScale);
	void HideStream();
	void DestroyVisualSegments();
	bool CanRunCosmeticVisual() const;

	static FVector EvaluateQuadraticBezier(
		const FVector& Start,
		const FVector& Control,
		const FVector& End,
		double Alpha);

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> SegmentMeshAsset;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> SegmentMaterial;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> SegmentVisuals;

	/** payload source를 owner local로 바꾼 값. Tick마다 현재 통 transform을 다시 따른다. */
	FVector SourceActorLocal = FVector::ZeroVector;

	/** payload가 확정한 접점. 통이 움직여도 lease 동안 월드에 고정한다. */
	FVector ContactWorldLocation = FVector::ZeroVector;
	FVector CurrentSourceWorldLocation = FVector::ZeroVector;
	double LeaseExpiresWorldSeconds = -1.0;
	bool bStreamActive = false;

};
