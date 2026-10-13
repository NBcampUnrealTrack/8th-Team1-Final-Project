#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "GameFramework/Actor.h"
#include "Gameplay/Interaction/MD_Interactable.h"
#include "MD_GlassDrawingDesk.generated.h"

class AMD_Character;
class UMD_GlassDrawingProfile;
class UMD_ToolDefinition;
class UStaticMeshComponent;

/** Exclusive drawing station for one explicitly assigned, approved glass surface. */
UCLASS(Blueprintable)
class MARINEDEMOLITION_API AMD_GlassDrawingDesk : public AActor, public IMD_Interactable
{
	GENERATED_BODY()

public:
	AMD_GlassDrawingDesk();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	virtual void SetInteractionHighlighted_Implementation(bool bHighlighted) override;
	virtual FText GetInteractionDisplayName_Implementation() const override;
	virtual FText GetInteractionPrompt_Implementation(AActor* Interactor) const override;
	virtual bool CanInteract_Implementation(AActor* Interactor) const override;
	virtual bool RequiresServerInteraction_Implementation() const override { return true; }
	virtual void Interact_Implementation(AActor* Interactor) override;

	UFUNCTION(BlueprintPure, Category = "Glass|Desk")
	UStaticMeshComponent* GetTargetGlassMesh() const;

	UFUNCTION(BlueprintPure, Category = "Glass|Desk")
	UMD_ToolDefinition* GetMarkerTool() const { return MarkerTool; }

	UFUNCTION(BlueprintPure, Category = "Glass|Desk")
	AMD_Character* GetOccupant() const { return Occupant; }

	UFUNCTION(BlueprintPure, Category = "Glass|Desk")
	bool HasValidConfiguration() const;

	/** Fits the assigned pane without moving the character or changing any camera. */
	bool ResolveFocusView(const AMD_Character* Character, float Aspect, FVector& OutLocation,
		FRotator& OutRotation, float& OutFOV, float& OutTraceDistance) const;

	/** Only the current occupant may release the desk and pane after the exit blend finishes. */
	void ReleaseOccupant(AMD_Character* Character);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Glass|Desk")
	TObjectPtr<UStaticMeshComponent> DeskMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Glass|Desk")
	TObjectPtr<UStaticMeshComponent> PenDisplayMesh;

	/** Explicit level reference. An unset or invalid reference never selects nearby glass. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Replicated, Category = "Glass|Desk")
	TObjectPtr<AActor> TargetGlassActor;

	/** Optional component selector on TargetGlassActor. Invalid selectors do not fall back. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Replicated, Category = "Glass|Desk",
		meta = (UseComponentPicker, AllowedClasses = "/Script/Engine.StaticMeshComponent"))
	FComponentReference TargetGlassComponent;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Glass|Desk")
	TObjectPtr<UMD_ToolDefinition> MarkerTool;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Glass|Desk")
	FText DisplayName;

	/** Candidate tabletop placement; editable without changing the marker mesh. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Glass|Desk")
	FTransform PenDisplayTransform = FTransform(FRotator(90.0, 0.0, 0.0), FVector(0.0, 0.0, 67.2));

	/** Candidate camera/input tuning, subject to gameplay review. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Glass|Desk|View", meta = (ClampMin = "0.0", ClampMax = "3.0", Units = "s"))
	float BlendSeconds = 0.65f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Glass|Desk|View", meta = (ClampMin = "1.0", ClampMax = "2.0"))
	float FramePadding = 1.12f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Glass|Desk|View", meta = (ClampMin = "40.0", ClampMax = "100.0", Units = "deg"))
	float FocusFieldOfView = 70.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Glass|Desk|Cursor", meta = (ClampMin = "0.0001", ClampMax = "0.05"))
	float CursorSpeed = 0.002f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Glass|Desk|Cursor", meta = (ClampMin = "5.0", ClampMax = "300.0", Units = "cm"))
	float CursorDepthCm = 60.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Glass|Desk|Cursor", meta = (ClampMin = "0.1", ClampMax = "2.0"))
	float CursorScale = 0.4f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Glass|Desk|Cursor")
	FRotator CursorRotationOffset = FRotator::ZeroRotator;

	/** Candidate contact for the existing marker nib. Zero allows the focus component's mesh-bound default. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Glass|Desk|Cursor")
	FVector CursorTipLocalOffset = FVector(0.0, 0.0, 9.3);

	/** Candidate contact for the existing marker eraser. Zero allows the focus component's mesh-bound default. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Glass|Desk|Cursor")
	FVector CursorEraserLocalOffset = FVector(0.0, 0.0, -9.2);

protected:
	/** Separate drawing walls reuse the session without displaying a tabletop pen. */
	virtual void RefreshPenDisplay();

private:
	const UMD_GlassDrawingProfile* GetDrawingProfile() const;

	UPROPERTY(ReplicatedUsing = OnRep_Occupant)
	TObjectPtr<AMD_Character> Occupant;

	UFUNCTION()
	void OnRep_Occupant();
};
