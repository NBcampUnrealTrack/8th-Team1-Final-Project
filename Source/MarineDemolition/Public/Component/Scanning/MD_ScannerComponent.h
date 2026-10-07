#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Gameplay/Scanning/MD_ScanTypes.h"
#include "MD_ScannerComponent.generated.h"

class AMD_WallUtility;
class AMD_SurfaceStain;
class UCameraComponent;
class UMaterialInterface;
class UMaterialParameterCollection;
class UMD_EquipHotbarComponent;
class UMD_ToolDefinition;
class UMD_ScannerModeWidget;
class APawn;
class AController;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMD_OnScanModeChanged, EMD_ScanMode, Mode);

/** Local selection and timed visual ownership. Replication is only for request cancellation. */
UCLASS(ClassGroup=(MD), meta=(BlueprintSpawnableComponent))
class MARINEDEMOLITION_API UMD_ScannerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMD_ScannerComponent();

	UFUNCTION(BlueprintPure, Category="MD|Scanner")
	EMD_ScanMode GetScanMode() const { return ScanMode; }

	UFUNCTION(BlueprintPure, Category="MD|Scanner")
	FText GetScanModeText() const;

	UFUNCTION(BlueprintPure, Category="MD|Scanner")
	bool IsScannerEquipped() const;

	/** Bind the user's Boolean IA Started event, or use InputConfig.ScannerModeSwitchAction. */
	UFUNCTION(BlueprintCallable, Category="MD|Scanner")
	bool ToggleScanMode();

	UPROPERTY(BlueprintAssignable, Category="MD|Scanner")
	FMD_OnScanModeChanged OnScanModeChanged;


	UFUNCTION(BlueprintPure, Category="MD|Scanner")
	bool IsScanVisible() const { return bScanVisible; }

	uint32 BeginLocalRequest();
	void FinishLocalRequest(uint32 RequestId);
	void PresentLocalScan(uint32 RequestId, EMD_ScanMode Mode, const TArray<AActor*>& Targets,
		float Duration, UMaterialParameterCollection* Collection,
		const FVector& Origin, const FVector& Forward, float Radius, float HalfAngle);
	void TrackScannedUtility(AMD_WallUtility* Utility);
	bool AcceptAuthoritativeRequest(uint32 RequestId);
	void RejectAuthoritativeRequest(uint32 RequestId);
	void PlayScanPulse(uint32 RequestId, const FGameplayTag& Tag, float Duration, float Fade);
	void ClearScan(bool bNotifyServer = true);

protected:
	/** Dedicated wall-style pass, installed on the local camera only during a wall scan. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="MD|Scanner|Visual")
	TObjectPtr<UMaterialInterface> WallScanMaterial;

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
	UPROPERTY(Transient, BlueprintReadOnly, Category="MD|Scanner", meta=(AllowPrivateAccess="true"))
	EMD_ScanMode ScanMode = EMD_ScanMode::InternalStructure;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialParameterCollection> ActiveCollection;

	UPROPERTY(Transient)
	TObjectPtr<UMD_ScannerModeWidget> ModeIndicator;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> AppliedWallScanMaterial;

	TWeakObjectPtr<UCameraComponent> WallPostProcessCamera;
	TOptional<float> OriginalWallBlendWeight;
	TArray<FMD_ScanMeshState> WallHighlights;
	TArray<TWeakObjectPtr<AMD_WallUtility>> ScannedUtilities;
	TArray<TWeakObjectPtr<AMD_SurfaceStain>> StainHighlights;
	TWeakObjectPtr<UMD_EquipHotbarComponent> BoundHotbar;
	TWeakObjectPtr<UMD_ToolDefinition> LastTool;
	FTimerHandle ScanTimer;
	FGameplayTag ActivePulseTag;
	float PulseFadeSeconds = 0.15f;
	uint32 IssuedRequestId = 0;
	uint32 ActiveRequestId = 0;
	uint32 AcceptedRequestId = 0;
	uint32 CancelledRequestId = 0;
	bool bScanVisible = false;

	void EnableWallPostProcess();
	void ClearWallPostProcess();
	void StopPulse();
	void RefreshModeIndicator();

	UFUNCTION()
	void HandleControllerChanged(APawn* Pawn, AController* OldController, AController* NewController);

	UFUNCTION()
	void HandleHotbarChanged();

	UFUNCTION(Server, Reliable)
	void ServerCancelScan(uint32 RequestId);

	UFUNCTION(Client, Reliable)
	void ClientRejectScan(uint32 RequestId);
};
