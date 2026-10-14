#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayAbilitySpecHandle.h"
#include "MD_GlassMarkerComponent.generated.h"

class AMD_Character;
class AMD_GlassDrawingBoard;
class UMD_GlassMarkerSettings;
class UMD_ToolDefinition;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMDOnGlassMarkerModeChanged, bool, bErasing);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMDOnGlassMarkerInkColorChanged, uint8, InkColorIndex, FLinearColor, InkColor);

/** Held input requests bounded samples; only server traces may alter a shared glass board. */
UCLASS(ClassGroup = (MarineDemolition), meta = (BlueprintSpawnableComponent))
class MARINEDEMOLITION_API UMD_GlassMarkerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMD_GlassMarkerComponent();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintCallable, Category = "Glass Marker")
	void Configure(UMD_GlassMarkerSettings* InSettings);

	UFUNCTION(BlueprintCallable, Category = "Glass Marker")
	void SetDrawInputHeld(bool bHeld);

	UFUNCTION(BlueprintCallable, Category = "Glass Marker")
	void SetEraseInputHeld(bool bHeld);

	UFUNCTION(BlueprintCallable, Category = "Glass Marker")
	void CancelUse();

	/** Advances the authored palette for future strokes without ending held drawing. */
	UFUNCTION(BlueprintCallable, Category = "Glass Marker|Color")
	void CycleInkColor();

	UFUNCTION(BlueprintPure, Category = "Glass Marker|Color")
	uint8 GetInkColorIndex() const { return SelectedInkColorIndex; }

	UFUNCTION(BlueprintPure, Category = "Glass Marker|Color")
	FLinearColor GetSelectedInkColor() const;

	UFUNCTION(BlueprintPure, Category = "Glass Marker")
	bool CanUseEquippedMarker() const;

	/** The occupied desk provides a temporary marker without changing hotbar contents. */
	UFUNCTION(BlueprintPure, Category = "Glass Marker")
	UMD_ToolDefinition* GetActiveToolDefinition() const;

	UFUNCTION(BlueprintPure, Category = "Glass Marker")
	bool IsErasing() const { return bErasing; }

	UFUNCTION(BlueprintPure, Category = "Glass Marker")
	bool IsUsingMarker() const { return ActiveAbility.IsValid() || bServerUsing; }

	UFUNCTION(BlueprintPure, Category = "Glass Marker|Presentation")
	FRotator GetEraserRotationOffset() const;

	UFUNCTION(BlueprintPure, Category = "Glass Marker|Presentation")
	FName GetInkIndicatorMaterialSlot() const;

	UPROPERTY(BlueprintAssignable, Category = "Glass Marker|Presentation")
	FMDOnGlassMarkerModeChanged OnMarkerModeChanged;

	UPROPERTY(BlueprintAssignable, Category = "Glass Marker|Presentation")
	FMDOnGlassMarkerInkColorChanged OnInkColorChanged;

	bool BeginUse(FGameplayAbilitySpecHandle AbilityHandle);
	void FinishUse(FGameplayAbilitySpecHandle AbilityHandle);

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UPROPERTY(Transient)
	TObjectPtr<UMD_GlassMarkerSettings> Settings;

	UPROPERTY(ReplicatedUsing = OnRep_Erasing)
	bool bErasing = false;

	UPROPERTY(ReplicatedUsing = OnRep_SelectedInkColorIndex)
	uint8 SelectedInkColorIndex = 0;

	FGameplayAbilitySpecHandle ActiveAbility;
	TWeakObjectPtr<UMD_ToolDefinition> ActiveTool;
	bool bDrawInputHeld = false;
	bool bEraseInputHeld = false;
	uint32 LocalEpoch = 0;
	uint32 LocalSequence = 0;
	bool bLocalSession = false;
	bool bLocalEraseMode = false;
	FTimerHandle SampleTimer;

	uint32 LastServerEpoch = 0;
	uint32 ServerEpoch = 0;
	uint32 ServerSequence = 0;
	bool bServerUsing = false;
	bool bServerBeginPending = false;
	bool bServerEraseMode = false;
	TWeakObjectPtr<UMD_ToolDefinition> ServerTool;
	TWeakObjectPtr<AMD_GlassDrawingBoard> LastBoard;
	FVector2D LastUV = FVector2D::ZeroVector;
	double LastServerSampleAt = -1.0;
	double ServerRequestedAt = -1.0;
	double LastContactAt = -1.0;
	FTimerHandle ServerWatchdogTimer;

	AMD_Character* GetCharacter() const;
	void RefreshHeldInput();
	void StartLocalSession();
	void StopLocalSession();
	void SendSample();
	void StopServerSession();
	void CheckServerSession();
	bool HasActiveMarkerAbility() const;
	void AcceptServerSession();
	void ResetUse(bool bCancelAbility);
	void SetErasing(bool bNewErasing);
	bool TraceSurface(const FVector2D& Pointer, AMD_GlassDrawingBoard*& OutBoard, FVector2D& OutUV) const;

	UFUNCTION()
	void OnRep_Erasing();

	UFUNCTION()
	void OnRep_SelectedInkColorIndex();

	UFUNCTION(Server, Reliable)
	void ServerCycleInkColor(UMD_ToolDefinition* ExpectedTool);

	UFUNCTION(Server, Reliable)
	void ServerBeginUse(uint32 Epoch, UMD_ToolDefinition* ExpectedTool, bool bErase);

	UFUNCTION(Server, Unreliable)
	void ServerSample(uint32 Epoch, uint32 Sequence, FVector2D Pointer);

	UFUNCTION(Server, Reliable)
	void ServerEndUse(uint32 Epoch);

	UFUNCTION(Client, Reliable)
	void ClientStopUse(uint32 Epoch);
};
