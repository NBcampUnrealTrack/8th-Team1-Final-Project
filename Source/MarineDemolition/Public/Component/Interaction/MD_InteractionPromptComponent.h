#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Types/Interaction/MD_InteractionTypes.h"
#include "MD_InteractionPromptComponent.generated.h"

UCLASS(ClassGroup = (MD), meta = (BlueprintSpawnableComponent))
// 액터가 공용 설정의 상호작용 문구 유형을 선택하는 컴포넌트
class MARINEDEMOLITION_API UMD_InteractionPromptComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	// 매 프레임 처리 없이 유형 선택만 보관하는 기본 설정
	UMD_InteractionPromptComponent();

public:
	// 공용 설정에서 행동 문구를 조회할 유형
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Interaction")
	EMD_InteractionPromptType PromptType = EMD_InteractionPromptType::Interact;

public:
	// 선택 유형의 행동 문구와 현재 상호작용 키를 조합한 안내
	UFUNCTION(BlueprintPure, Category = "MD|Interaction")
	FText GetActionPrompt(UObject* WorldContextObject);
};
