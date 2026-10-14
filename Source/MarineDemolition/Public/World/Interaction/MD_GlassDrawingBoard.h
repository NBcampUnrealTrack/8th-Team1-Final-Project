#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "MD_GlassDrawingBoard.generated.h"

class APlayerState;
class UMaterialInstanceDynamic;
class UMD_GlassDrawingProfile;
class UStaticMeshComponent;
class UTextureRenderTarget2D;
class AMD_GlassDrawingBoard;

USTRUCT()
struct FMD_GlassDrawingSegment : public FFastArraySerializerItem
{
	GENERATED_BODY()

	UPROPERTY() uint16 StartX = 0;
	UPROPERTY() uint16 StartY = 0;
	UPROPERTY() uint16 EndX = 0;
	UPROPERTY() uint16 EndY = 0;
	UPROPERTY() int32 AuthorPlayerId = INDEX_NONE;
	UPROPERTY() uint8 ColorIndex = 0;
	/** Independent of FastArray indices; eraser fragments retain their original painting order. */
	UPROPERTY() uint64 PaintOrder = 0;

	FVector2D GetStart() const;
	FVector2D GetEnd() const;
	void SetPoints(const FVector2D& Start, const FVector2D& End);
};

USTRUCT()
struct FMD_GlassDrawingState : public FFastArraySerializer
{
	GENERATED_BODY()

	UPROPERTY() TArray<FMD_GlassDrawingSegment> Items;
	TWeakObjectPtr<AMD_GlassDrawingBoard> Owner;

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& Params)
	{
		return FastArrayDeltaSerialize<FMD_GlassDrawingSegment, FMD_GlassDrawingState>(Items, Params, *this);
	}
	void PreReplicatedRemove(const TArrayView<int32>& RemovedIndices, int32 FinalSize);
	void PostReplicatedAdd(const TArrayView<int32>& AddedIndices, int32 FinalSize);
	void PostReplicatedChange(const TArrayView<int32>& ChangedIndices, int32 FinalSize);
};

template<> struct TStructOpsTypeTraits<FMD_GlassDrawingState> : TStructOpsTypeTraitsBase2<FMD_GlassDrawingState>
{
	enum { WithNetDeltaSerializer = true };
};

/** One server-created board per used glass component; the shared local ink naturally mirrors. */
UCLASS(BlueprintType)
class MARINEDEMOLITION_API AMD_GlassDrawingBoard : public AActor
{
	GENERATED_BODY()

public:
	AMD_GlassDrawingBoard();
	void InitializeSurface(UStaticMeshComponent* Mesh, UMD_GlassDrawingProfile* InProfile);
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Glass|Drawing")
	bool AppendSegment(APlayerState* Author, const FVector2D& Start, const FVector2D& End, uint8 ColorIndex = 0);

	/** Clips ink against the eraser circle; at capacity, affected whole segments may be removed. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Glass|Drawing")
	bool EraseAt(APlayerState* Author, const FVector2D& UV);

	/** Explicit game-flow hook. Repair/restart does not silently clear session ink. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Glass|Drawing")
	void ResetDrawing();

	uint32 GetRevision() const { return Revision; }

	UFUNCTION(BlueprintPure, Category = "Glass|Drawing")
	int64 GetDrawingRevision() const { return Revision; }

	UFUNCTION(BlueprintPure, Category = "Glass|Drawing")
	int32 GetSegmentCount() const { return Segments.Items.Num(); }

	/** Count of stored line pieces using a palette color, including pieces left after erasing. */
	UFUNCTION(BlueprintPure, Category = "Glass|Drawing")
	int32 GetSegmentColorCount(uint8 ColorIndex) const;

	UFUNCTION(BlueprintPure, Category = "Glass|Drawing")
	UStaticMeshComponent* GetSourceMesh() const { return SourceMesh.Get(); }

	UFUNCTION(BlueprintPure, Category = "Glass|Drawing")
	const UMD_GlassDrawingProfile* GetProfile() const { return Profile.Get(); }

	UFUNCTION(BlueprintPure, Category = "Glass|Drawing")
	FVector GetPointWorldPosition(const FVector2D& UV) const;

	UFUNCTION(BlueprintPure, Category = "Glass|Drawing")
	UTextureRenderTarget2D* GetInkTexture() const { return InkTexture.Get(); }

	void QueueVisualUpdate(bool bRebuild, const TArrayView<int32>& AddedIndices = TArrayView<int32>());

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	bool CanAuthor(APlayerState* Author, const FVector2D& UV) const;
	FVector2D GetPhysicalSize() const;
	void NotifyServerChange(bool bRebuild, int32 AddedIndex = INDEX_NONE);
	bool EnsureVisuals();
	void FlushVisuals();
	void ReleaseVisuals();
	void RegisterSurface();

	UFUNCTION() void OnRep_Surface();
	UFUNCTION() void OnRep_Revision();
	UFUNCTION() void HandleSourceEndPlay(AActor* Actor, EEndPlayReason::Type Reason);

	UPROPERTY(ReplicatedUsing = OnRep_Surface) TObjectPtr<UStaticMeshComponent> SourceMesh;
	UPROPERTY(ReplicatedUsing = OnRep_Surface) TObjectPtr<UMD_GlassDrawingProfile> Profile;
	UPROPERTY(Replicated) FMD_GlassDrawingState Segments;
	UPROPERTY(ReplicatedUsing = OnRep_Revision) uint32 Revision = 1;
	UPROPERTY(Transient) TObjectPtr<UTextureRenderTarget2D> InkTexture;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> FrontInkMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> BackInkMaterial;
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> FrontInk;
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> BackInk;

	TArray<FMD_GlassDrawingSegment> PendingPaint;
	FTimerHandle PaintTimer;
	uint64 NextPaintOrder = 1;
	uint64 LastPaintedOrder = 0;
	bool bRebuildPending = true;
	bool bPaintQueued = false;
	bool bSourceEnded = false;
};
