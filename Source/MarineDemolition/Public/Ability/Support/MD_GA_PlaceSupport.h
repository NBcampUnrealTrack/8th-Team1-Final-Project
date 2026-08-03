#pragma once

#include "CoreMinimal.h"
#include "Ability/Wall/MD_GA_WallBase.h"
#include "MD_GA_PlaceSupport.generated.h"

class AMD_SupportBrace;

// 조준한 지주격벽에 임시 지지대 설치. 접지(걷기) 상태에서만 발동
UCLASS()
class MARINEDEMOLITION_API UMD_GA_PlaceSupport : public UMD_GA_WallBase
{
	GENERATED_BODY()

public:
	UMD_GA_PlaceSupport();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	                             const FGameplayAbilityActivationInfo ActivationInfo,
	                             const FGameplayEventData* TriggerEventData) override;

protected:
	// 설치될 지지대 클래스. BP 에셋을 에디터에서 지정
	UPROPERTY(EditDefaultsOnly, Category = "Support")
	TSubclassOf<AMD_SupportBrace> BraceClass;

	// 벽에서 설치자 방향으로 띄우는 거리(cm)
	UPROPERTY(EditDefaultsOnly, Category = "Support")
	float PlacementOffsetCm = 60.f;
};
