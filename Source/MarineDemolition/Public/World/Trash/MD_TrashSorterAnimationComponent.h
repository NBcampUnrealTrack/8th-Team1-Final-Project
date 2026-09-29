#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MD_TrashSorterAnimationComponent.generated.h"

class UCurveFloat;
class UStaticMesh;
class UStaticMeshComponent;
class UMaterialInterface;

USTRUCT()
struct FMD_SorterMaterialVector
{
	GENERATED_BODY()
	UPROPERTY() int32 Slot = 0;
	UPROPERTY() FName Name;
	UPROPERTY() FLinearColor Value = FLinearColor::White;
};

/** Asset references survive destruction of the gameplay item and can replicate to late viewers. */
USTRUCT()
struct FMD_SorterItemVisual
{
	GENERATED_BODY()
	UPROPERTY() TObjectPtr<UStaticMesh> Mesh;
	UPROPERTY() TArray<TObjectPtr<UMaterialInterface>> Materials;
	UPROPERTY() TArray<FMD_SorterMaterialVector> VectorParameters;
	UPROPERTY() TArray<float> CustomPrimitiveData;
	UPROPERTY() FTransform Transform;
};

/** Cosmetic motion only. Never classifies, consumes, awards score, or drives StateTree transitions. */
UCLASS(ClassGroup = (MD), meta = (BlueprintSpawnableComponent))
class MARINEDEMOLITION_API UMD_TrashSorterAnimationComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UMD_TrashSorterAnimationComponent();
	static FMD_SorterItemVisual CaptureItem(const AActor* Item);
	void StartSorting(const FMD_SorterItemVisual& Item, float Duration, float Elapsed);
	void StopSorting();
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Turn off to replace the motion with the sorter's Blueprint animation hook. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation")
	bool bEnabled = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blade")
	FName BladeComponentName = TEXT("Blade");
	/** Both are in the Blade mesh's local coordinates, before its component transform. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blade")
	FVector BladePivot = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blade")
	FVector BladeAxis = FVector::ForwardVector;
	/** Number of revolutions (absolute value). Direction is opposite to the feed-derived rotation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blade")
	float BladeTurns = 3.0f;
	/** Normalized time -> normalized rotation. Optional; linear rotation by default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blade")
	TObjectPtr<UCurveFloat> BladeCurve;
	/** Saved legacy arc controls; no longer used by the conveyor feed. */
	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Use FeedEntryOffset."))
	FVector IntakeLocation = FVector(0, 0, 85);
	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Use FeedExitOffset."))
	FVector SwallowLocation = FVector(0, 0, 35);
	/** Offsets from the Blade pivot, along the sorter actor's axes. Front is -Y. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intake")
	FVector FeedEntryOffset = FVector(0, -21, -6);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intake")
	FVector FeedExitOffset = FVector(0, 8, -19);
	/** First pull directly to the front slot; use the remaining time for the conveyor feed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intake", meta = (ClampMin = "0.05", ClampMax = "0.95"))
	float ApproachFraction = 0.3f;
	/** Normalized time -> path progress. Optional; constant feed speed by default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intake")
	TObjectPtr<UCurveFloat> IntakeCurve;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intake", meta = (ClampMin = "0.01", ClampMax = "1.0"))
	float EndScale = 0.05f;

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> IntakeVisual;
	TWeakObjectPtr<UStaticMeshComponent> Blade;
	FTransform BladeStart;
	FTransform ItemStart;
	FVector ItemLocalCenter = FVector::ZeroVector;
	FVector ItemStartCenter = FVector::ZeroVector;
	FVector FeedEntry = FVector::ZeroVector;
	FVector FeedExit = FVector::ZeroVector;
	float SignedBladeTurns = 0.0f;
	float AnimationDuration = 0.0f;
	float AnimationElapsed = 0.0f;
	void ApplyPose(float Alpha);
};
