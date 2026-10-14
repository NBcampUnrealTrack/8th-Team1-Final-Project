#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MD_DoorAudioComponent.generated.h"

/** Local presentation adapter; also observes fault-exempt BP doors without changing their rules. */
UCLASS(Transient)
class MARINEDEMOLITION_API UMD_DoorAudioComponent : public UActorComponent
{
	GENERATED_BODY()
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
	FTimerHandle Timer;
	TOptional<bool> PreviousOpen;
	void Refresh();
};
