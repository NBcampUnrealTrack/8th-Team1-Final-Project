#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayEffectTypes.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "MD_GlassDrawingFocusComponent.generated.h"

class AMD_Character;
class AMD_GlassDrawingDesk;
class ACameraActor;
class APlayerController;
class UAbilitySystemComponent;
class UMaterialInstanceDynamic;
class UMD_ToolDefinition;
class UStaticMeshComponent;
class UPrimitiveComponent;

UENUM(BlueprintType)
enum class EMD_GlassDrawingFocusPhase : uint8
{
	Idle,
	Entering,
	Drawing,
	Exiting
};

/** Server-owned desk lease with a local blended camera and model-based virtual pointer. */
UCLASS(ClassGroup = (MarineDemolition), meta = (BlueprintSpawnableComponent))
class MARINEDEMOLITION_API UMD_GlassDrawingFocusComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMD_GlassDrawingFocusComponent();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	bool CanBeginFocus(const AMD_GlassDrawingDesk* Desk) const;
	bool BeginFocus(AMD_GlassDrawingDesk* Desk);

	UFUNCTION(BlueprintCallable, Category = "Glass|Desk")
	void EndFocus(bool bImmediate = false);

	UFUNCTION(BlueprintPure, Category = "Glass|Desk")
	bool IsFocusActive() const;

	UFUNCTION(BlueprintPure, Category = "Glass|Desk")
	bool IsDrawingReady() const;

	UFUNCTION(BlueprintPure, Category = "Glass|Desk")
	AMD_GlassDrawingDesk* GetActiveDesk() const { return ActiveDesk; }

	UFUNCTION(BlueprintPure, Category = "Glass|Desk")
	UMD_ToolDefinition* GetMarkerTool() const;

	UFUNCTION(BlueprintPure, Category = "Glass|Desk")
	UStaticMeshComponent* GetTargetGlassMesh() const;

	UFUNCTION(BlueprintPure, Category = "Glass|Desk")
	FVector2D GetPointerNormalized() const { return PointerNormalized; }

	UFUNCTION(BlueprintPure, Category = "Glass|Desk")
	EMD_GlassDrawingFocusPhase GetFocusPhase() const { return LocalPhase; }

	UFUNCTION(BlueprintPure, Category = "Glass|Desk")
	UStaticMeshComponent* GetCursorPenMesh() const { return CursorPen; }

	UFUNCTION(BlueprintPure, Category = "Glass|Desk")
	ACameraActor* GetFocusCameraActor() const { return FocusCamera; }

	/** Returns true while the station owns look input, including either transition. */
	bool ConsumeLookInput(const FVector2D& Delta);
	bool GetValidatedDrawRay(const FVector2D& Pointer, FVector& OutOrigin, FVector& OutDirection, float& OutReach) const;
	bool CanAuthorSurface(const UStaticMeshComponent* Mesh, const FVector2D& UV) const;
	bool IsChangingView() const { return bChangingView; }

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UPROPERTY(ReplicatedUsing = OnRep_ActiveDesk)
	TObjectPtr<AMD_GlassDrawingDesk> ActiveDesk;

	UPROPERTY(Replicated)
	uint32 SessionSerial = 0;

	UPROPERTY(Replicated)
	uint32 ReadySerial = 0;

	UPROPERTY(Transient)
	TObjectPtr<ACameraActor> FocusCamera;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> CursorPen;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> CursorInkMaterial;

	TArray<TWeakObjectPtr<UPrimitiveComponent>> HiddenBodyComponents;
	TWeakObjectPtr<AActor> OriginalViewTarget;
	TWeakObjectPtr<APlayerController> SessionController;
	TWeakObjectPtr<UAbilitySystemComponent> GrantedASC;
	TWeakObjectPtr<UStaticMeshComponent> SessionGlass;
	TWeakObjectPtr<AActor> SessionGlassActor;
	TWeakObjectPtr<UMD_ToolDefinition> SessionTool;
	FActiveGameplayEffectHandle TemporaryGrant;
	EMovementMode OriginalMovementMode = MOVE_None;
	uint8 OriginalCustomMovementMode = 0;
	FVector OriginalPawnLocation = FVector::ZeroVector;
	FRotator OriginalControlRotation = FRotator::ZeroRotator;
	FVector FocusViewLocation = FVector::ZeroVector;
	FRotator FocusViewRotation = FRotator::ZeroRotator;
	FVector ReturnViewLocation = FVector::ZeroVector;
	FRotator ReturnViewRotation = FRotator::ZeroRotator;
	FTransform OriginalGlassTransform = FTransform::Identity;
	float FocusFOV = 70.0f;
	float FocusAspect = 16.0f / 9.0f;
	float FocusTraceDistance = 0.0f;
	FVector2D PointerNormalized = FVector2D(0.5, 0.5);
	EMD_GlassDrawingFocusPhase LocalPhase = EMD_GlassDrawingFocusPhase::Idle;
	uint32 LocalSerial = 0;
	double LocalTransitionStartedAt = 0.0;
	double ServerViewStartedAt = 0.0;
	double ServerLeaseStartedAt = 0.0;
	double ServerExitStartedAt = 0.0;
	double LastReadyRequestAt = -1.0;
	float LocalBlendSeconds = 0.0f;
	bool bServerExiting = false;
	bool bViewConfigured = false;
	bool bChangingView = false;
	bool bOriginalThirdPerson = false;
	uint8 LastCursorColor = 255;
	FTimerHandle WatchdogTimer;
	FTimerHandle ExitFallbackTimer;

	AMD_Character* GetCharacter() const;
	bool IsSessionValid() const;
	bool IsDead() const;
	void CheckSession();
	void FinalizeServerFocus();
	void StartLocalExit(bool bImmediate);
	void FinishLocalExit();
	void SetLocalBodyHidden(bool bHideBody);
	void UpdateCursorPen();
	void CreateCursorPen();
	void DestroyCursorPen();
	void RefreshToolPresentation();
	FVector DirectionForPointer(const FVector2D& Pointer) const;

	UFUNCTION()
	void OnRep_ActiveDesk();

	UFUNCTION()
	void HandleGlassEndPlay(AActor* Actor, EEndPlayReason::Type Reason);

	UFUNCTION(Client, Reliable)
	void ClientBeginFocus(AMD_GlassDrawingDesk* Desk, uint32 Serial);

	UFUNCTION(Server, Reliable)
	void ServerConfigureView(uint32 Serial, float Aspect);

	UFUNCTION(Client, Reliable)
	void ClientConfigureView(uint32 Serial, FVector Location, FRotator Rotation, float FieldOfView, float Aspect);

	UFUNCTION(Server, Reliable)
	void ServerReady(uint32 Serial);

	UFUNCTION(Server, Reliable)
	void ServerRequestExit(uint32 Serial, bool bImmediate);

	UFUNCTION(Client, Reliable)
	void ClientExitFocus(uint32 Serial, bool bImmediate);

	UFUNCTION(Server, Reliable)
	void ServerFinishedExit(uint32 Serial);
};
