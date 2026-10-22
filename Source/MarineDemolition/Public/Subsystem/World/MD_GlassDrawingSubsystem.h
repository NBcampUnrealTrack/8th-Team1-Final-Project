#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MD_GlassDrawingSubsystem.generated.h"

class AMD_GlassDrawingBoard;
class AMD_GlassDrawingDesk;
class AMD_Character;
class UMD_GlassDrawingProfile;
class UStaticMeshComponent;

/** Lazily attaches drawing boards to approved existing walls. No level authoring or save is needed. */
UCLASS()
class MARINEDEMOLITION_API UMD_GlassDrawingSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintCallable, Category = "Glass|Drawing")
	AMD_GlassDrawingBoard* ResolveSurfaceHit(const FHitResult& Hit, FVector2D& OutUV, bool bCreate = true);

	UFUNCTION(BlueprintPure, Category = "Glass|Drawing")
	UMD_GlassDrawingProfile* GetProfile() const;

	/** Non-mutating coordinate check, also usable before a client has received its board actor. */
	bool ResolveCoordinates(const FHitResult& Hit, FVector2D& OutUV) const;
	void RegisterBoard(AMD_GlassDrawingBoard* Board);
	void UnregisterBoard(AMD_GlassDrawingBoard* Board);

	/** Server-owned pane lease, retained until the desk explicitly finishes its exit. */
	bool TryReserveDeskSurface(UStaticMeshComponent* Surface, AMD_GlassDrawingDesk* Desk);
	void ReleaseDeskSurface(AMD_GlassDrawingDesk* Desk);
	AMD_Character* GetSurfaceOccupant(const UStaticMeshComponent* Surface) const;

private:
	void EnsureProfile();
	void RemoveInvalidDeskReservations();
	/** Loaded from UMD_GlassDrawingSettings; the UPROPERTY keeps it and its mesh/material dependencies alive for this world. */
	UPROPERTY() TObjectPtr<UMD_GlassDrawingProfile> DefaultProfile;
	bool bProfileLookupAttempted = false;
	TMap<TWeakObjectPtr<UStaticMeshComponent>, TWeakObjectPtr<AMD_GlassDrawingBoard>> Boards;
	TMap<TWeakObjectPtr<UStaticMeshComponent>, TWeakObjectPtr<AMD_GlassDrawingDesk>> DeskReservations;
};
