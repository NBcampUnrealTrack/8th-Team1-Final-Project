#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MD_DoorFaultSubsystem.generated.h"

UCLASS()
class MARINEDEMOLITION_API UMD_DoorFaultSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;
private:
	void AttachFaultAdapter(AActor* Actor);
	UPROPERTY(Transient)
	TObjectPtr<UClass> DoorClass;
	UPROPERTY(Transient)
	TObjectPtr<UClass> BlackDoorClass;
	FDelegateHandle SpawnHandle;
};
