#pragma once

#include "CoreMinimal.h"
#include "TickableEditorObject.h"
#include "UObject/StrongObjectPtr.h"

class AMD_SurfaceStain;
class AActor;
class UMD_SurfaceStainDefinition;
class UStaticMeshComponent;
class USceneComponent;
class FMD_StainStickerAsyncBake;
class SNotificationItem;

/** One bounded editor job. The existing actor is only modified after a valid final build. */
class FMD_StainRefit final : public FTickableEditorObject
{
public:
	static FMD_StainRefit& Get();
	static void Shutdown();
	virtual ~FMD_StainRefit() override;
	bool Start(AMD_SurfaceStain* Stain);
	void Cancel();
	bool IsRunning() const { return Bake.IsValid(); }
	FText GetStatus() const { return Status; }
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override { return IsRunning(); }
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(FMD_StainRefit, STATGROUP_Tickables); }

private:
	bool IsCurrent() const;
	void Finish(const FText& Message, bool bSuccess);
	void Release();
	TWeakObjectPtr<AMD_SurfaceStain> Target;
	TWeakObjectPtr<UStaticMeshComponent> Receiver;
	TWeakObjectPtr<USceneComponent> OriginalParent;
	TWeakObjectPtr<UMD_SurfaceStainDefinition> PreviousDefinition;
	TStrongObjectPtr<UMD_SurfaceStainDefinition> Source;
	TStrongObjectPtr<AActor> Helper;
	TSharedPtr<FMD_StainStickerAsyncBake> Bake;
	TWeakPtr<SNotificationItem> Notification;
	FTransform OriginalTransform, ContactTransform;
	int32 Size = 0;
	uint32 SourceRevision = 0;
	bool bCancelling = false;
	FText Status;
};
