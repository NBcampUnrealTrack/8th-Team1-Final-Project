#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "MD_Interactable.generated.h"

class AActor;

/** 플레이어가 바라보고 상호작용할 수 있는 월드 액터의 공통 계약 */
UINTERFACE(MinimalAPI, BlueprintType)
class UMD_Interactable : public UInterface
{
	GENERATED_BODY()
};

class MARINEDEMOLITION_API IMD_Interactable
{
	GENERATED_BODY()

public:
	/** 포스트프로세스 머터리얼이 상호작용 대상을 구분할 때 사용하는 Stencil 값 */
	static constexpr int32 HighlightStencilValue = 250;

	/** 액터가 소유한 하이라이트 대상 컴포넌트의 CustomDepth 기록을 켜거나 끈다. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "MD|Interaction")
	void SetInteractionHighlighted(bool bHighlighted);

	/**
	 * 상호작용을 서버의 검증된 ServerInteract 경로에서 실행해야 하는지 여부.
	 * 안전한 기본값은 서버 실행이다. 화면만 여는 로컬 전용 대상만 명시적으로 false를 반환한다.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "MD|Interaction")
	bool RequiresServerInteraction() const;
	virtual bool RequiresServerInteraction_Implementation() const { return true; }

	/** 플레이어의 상호작용 입력을 액터에 전달한다. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "MD|Interaction")
	void Interact(AActor* Interactor);
};
