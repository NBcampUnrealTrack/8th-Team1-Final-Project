#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MD_DoorStatusLightSubsystem.generated.h"

/** 레벨에 배치된 BP_Door에 런타임 상태등 컴포넌트를 자동 부착한다. */
UCLASS()
class MARINEDEMOLITION_API UMD_DoorStatusLightSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

private:
	void AttachStatusLight(AActor* Door);
	void HandleActorSpawned(AActor* Actor);

	UPROPERTY(Transient)
	TObjectPtr<UClass> DoorClass;
	FDelegateHandle ActorSpawnedHandle;
};
