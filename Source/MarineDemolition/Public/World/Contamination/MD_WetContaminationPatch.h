#pragma once

#include "CoreMinimal.h"
#include "Component/Scrape/MD_ScrapeSurfaceComponent.h"
#include "GameFramework/Actor.h"
#include "Types/Trash/MD_TrashBinSpillTypes.h"
#include "MD_WetContaminationPatch.generated.h"

class UMaterialInterface;
class UPrimitiveComponent;
class USceneComponent;
class UStaticMesh;
class UStaticMeshComponent;

/**
 * SPILL-024의 순수 cosmetic 형상 선택 결과.
 *
 * 서버가 이미 복제하는 PatchId만으로 모든 peer가 같은 값을 다시 만들며, 이 구조체
 * 자체는 복제하거나 저장하지 않는다. Variant 인덱스와 hash 규칙은 저장 호환 계약이라
 * 기존 항목을 재정렬하지 않는다.
 */
struct MARINEDEMOLITION_API FMD_WetContaminationVisualDescriptor
{
	static constexpr uint8 VariantCount = 4;
	static constexpr uint8 YawStepCount = 8;
	static constexpr uint8 StretchPresetCount = 5;
	static constexpr float YawStepDegrees = 45.0f;
	static constexpr float SurfaceOffsetCm = 0.15f;

	uint8 MeshVariantIndex = 0;
	uint8 YawStep = 0;
	uint8 StretchPreset = 0;
	bool bMirrorX = false;

	bool IsValid() const;
	float GetYawDegrees() const;
	FVector2D GetStretchScale() const;
	FVector MakeRelativeScale(double LiquidUnits) const;
	FTransform MakeRelativeTransform(double LiquidUnits) const;

	/** 고정 integer mixer를 사용해 플랫폼·참가 시점과 무관한 descriptor를 만든다. */
	static FMD_WetContaminationVisualDescriptor FromPatchId(
		const FGuid& PatchId);

	/** 기존 단일 Wet의 액체량 기반 크기 계약을 그대로 보존한다. */
	static float CalculateAmountBaseScale(double LiquidUnits);
};

/**
 * Wet 패치가 나중에 Adhered 오염으로 바뀔 때 사용할 전용 표면이다.
 *
 * 생성 시점에는 실제 오염 마스크가 없어야 하므로 초기량과 산출물 수를 0으로
 * 고정하고 기능도 꺼 둔다. SPILL-019가 원장을 넘기는 데 성공한 뒤에만 켠다.
 */
UCLASS(ClassGroup = (MD))
class MARINEDEMOLITION_API UMD_WetScrapeSurfaceComponent final
	: public UMD_ScrapeSurfaceComponent
{
	GENERATED_BODY()

public:
	UMD_WetScrapeSurfaceComponent();
};

/** 클라이언트가 현재 젖은 웅덩이를 그리는 데 필요한 작은 복제 요약. */
USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_WetContaminationPatchState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "MD|Wet Contamination")
	FGuid PatchId;

	UPROPERTY(BlueprintReadOnly, Category = "MD|Wet Contamination")
	double LiquidUnits = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "MD|Wet Contamination")
	double RewardUnits = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "MD|Wet Contamination")
	int32 Revision = 0;

	bool IsValid() const;
};

/** 지지면 포인터·컴포넌트·local anchor가 서로 다른 net frame에 섞이지 않는 한 묶음. */
USTRUCT()
struct MARINEDEMOLITION_API FMD_WetContaminationSupportState
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<AActor> Actor;

	UPROPERTY()
	FName ComponentName = NAME_None;

	UPROPERTY()
	FTransform LocalAnchor = FTransform::Identity;

	bool IsValid() const
	{
		return ::IsValid(Actor)
			&& !ComponentName.IsNone()
			&& !LocalAnchor.ContainsNaN();
	}
};

/**
 * 통에서 실제로 빠져나온 액체를 바닥 위에서 소유하는 서버 권위 운반체.
 *
 * Prepare 단계에서는 숨겨진 후보일 뿐이고 통이나 패치 어느 원장도 바꾸지 않는다.
 * 양쪽 preflight가 모두 끝난 같은 서버 호출에서만 Commit되어 보이기 시작한다.
 * Actor Tick, 충돌, overlap, navigation은 사용하지 않는다.
 */
UCLASS()
class MARINEDEMOLITION_API AMD_WetContaminationPatch : public AActor
{
	GENERATED_BODY()

public:
	AMD_WetContaminationPatch();

	virtual void GetLifetimeReplicatedProps(
		TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** 숨겨진 수용 후보를 만들고, 통 commit에 필요한 1회용 token을 돌려준다. */
	bool PrepareForSpill(
		const FMD_TrashBinSpillCandidate& Candidate,
		const FMD_TrashBinSpillContact& Contact,
		UPrimitiveComponent* SupportComponent,
		FMD_TrashBinSpillPreparedSinkToken& OutToken);

	/** 원장에 손대지 않는 마지막 preflight. */
	bool CanCommitPreparedSpill(
		const FMD_TrashBinSpillCandidate& Candidate,
		const FMD_TrashBinSpillPreparedSinkToken& Token) const;

	/** Prepare 이후 어느 preflight라도 실패했을 때 숨겨진 후보만 제거한다. */
	void AbortPreparedSpill();

	UFUNCTION(BlueprintPure, Category = "MD|Wet Contamination")
	double GetLiquidUnits() const { return PatchState.LiquidUnits; }

	UFUNCTION(BlueprintPure, Category = "MD|Wet Contamination")
	double GetRewardUnits() const { return PatchState.RewardUnits; }

	UFUNCTION(BlueprintPure, Category = "MD|Wet Contamination")
	int32 GetPatchRevision() const { return PatchState.Revision; }

	UFUNCTION(BlueprintPure, Category = "MD|Wet Contamination")
	bool IsCommitted() const { return bCommitted; }

	const TArray<FMD_ScrapeRecoveryLot>& GetRecoveryLotsForServer() const
	{
		return RecoveryLots;
	}

	const FMD_TrashBinPerBinDedupeState& GetDedupeStateForServer() const
	{
		return DedupeState;
	}

	const FMD_TrashBinSpillTransactionId& GetLastTransactionIdForServer() const
	{
		return LastTransactionId;
	}

	UStaticMeshComponent* GetVisualMesh() const { return VisualMesh; }
	UMD_WetScrapeSurfaceComponent* GetWetScrapeSurface() const
	{
		return ScrapeSurface;
	}

	/** 제품 Blueprint가 런타임에 보이는 Wet을 만들 모든 에셋을 명시했는지. */
	bool HasCompleteVisualConfiguration() const;

protected:
	virtual void BeginPlay() override;

private:
	UFUNCTION()
	void OnRep_PatchState();

	UFUNCTION()
	void OnRep_Support();

	void RefreshVisual();
	void RefreshSupportAttachment();
	UPrimitiveComponent* ResolveSupportComponent() const;
	/** CanCommitPreparedSpill 성공 뒤, 아직 표시·알림하지 않는 실패 없는 대입 단계. */
	void ApplyPreparedSpillNoNotify(
		const FMD_TrashBinSpillCandidate& Candidate,
		const FMD_TrashBinSpillPreparedSinkToken& Token);
	/** 통과 Wet 양쪽 대입이 모두 끝난 뒤 한 번만 화면·복제를 공개한다. */
	void FinalizeCommittedSpill();

	UPROPERTY(VisibleAnywhere, Category = "MD|Wet Contamination")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, Category = "MD|Wet Contamination")
	TObjectPtr<UStaticMeshComponent> VisualMesh;

	/**
	 * 한 VisualMesh가 PatchId로 고른 전용 다형 자국. 순서는 저장 호환 계약이다.
	 * 별도 Actor/component/replicated state를 만들지 않는다.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly,
		Category = "MD|Wet Contamination|Visual",
		meta = (EditFixedSize, AllowPrivateAccess = "true"))
	TArray<TObjectPtr<UStaticMesh>> VisualVariantMeshes;

	/** 모든 Wet variant가 공유하는 제품 머터리얼. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly,
		Category = "MD|Wet Contamination|Visual",
		meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UMaterialInterface> VisualMaterial;

	UPROPERTY(VisibleAnywhere, Category = "MD|Wet Contamination")
	TObjectPtr<UMD_WetScrapeSurfaceComponent> ScrapeSurface;

	UPROPERTY(ReplicatedUsing = OnRep_PatchState, VisibleInstanceOnly,
		Category = "MD|Wet Contamination")
	FMD_WetContaminationPatchState PatchState;

	UPROPERTY(ReplicatedUsing = OnRep_Support)
	FMD_WetContaminationSupportState SupportState;

	/** provenance 원장은 서버와 세이브만 소유하며 클라이언트로 복제하지 않는다. */
	TArray<FMD_ScrapeRecoveryLot> RecoveryLots;

	/** 동일 transaction 재적용을 막는 서버/세이브 전용 상태. */
	FMD_TrashBinPerBinDedupeState DedupeState;
	FMD_TrashBinSpillTransactionId LastTransactionId;

	FMD_TrashBinSpillCandidate PreparedCandidate;
	FMD_TrashBinSpillPreparedSinkToken PreparedToken;
	TWeakObjectPtr<UPrimitiveComponent> PreparedSupportComponent;
	bool bPrepared = false;
	bool bCommitted = false;
	bool bCommitFinalized = false;
	bool bMutationInProgress = false;

	friend class AMD_TrashBin;
};
