#include "Component/Interaction/MD_InteractionPromptComponent.h"

#include "Gameplay/Interaction/MD_InteractionPresentationLibrary.h"

UMD_InteractionPromptComponent::UMD_InteractionPromptComponent()
{
	// 문구 조회에 불필요한 프레임 갱신 비활성화
	PrimaryComponentTick.bCanEverTick = false;
}

FText UMD_InteractionPromptComponent::GetActionPrompt(UObject* WorldContextObject)
{
	// 액터 선택 유형과 현재 입력 설정에 따른 공용 안내 반환
	return UMD_InteractionPresentationLibrary::FindActionPromptByType(PromptType, WorldContextObject);
}
