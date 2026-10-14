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
	 * 이 대상이 이미 보유한 데이터에서 화면 표시 이름을 반환한다.
	 * 빈 값이면 공용 상호작용 이름표가 대상 클래스에 맞는 이름을 대신 찾는다.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "MD|Interaction")
	FText GetInteractionDisplayName() const;
	virtual FText GetInteractionDisplayName_Implementation() const { return FText::GetEmpty(); }

	/**
	 * 현재 Interactor에게 두 번째 줄로 보여 줄 키와 행동 문구.
	 * 대상 이름은 별도로 해석해 첫 번째 줄에 표시하며, 이 값이 비면 이름만 표시한다.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "MD|Interaction")
	FText GetInteractionPrompt(AActor* Interactor) const;
	virtual FText GetInteractionPrompt_Implementation(AActor* Interactor) const;

	/**
	 * 이 Interactor가 현재 대상과 상호작용할 수 있는지.
	 * 매 프레임과 서버 검증에서 반복 호출될 수 있으므로 상태를 바꾸지 않는 빠른 판정이어야 한다.
	 * 기본값은 기존 상호작용 동작을 보존하는 true다.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "MD|Interaction")
	bool CanInteract(AActor* Interactor) const;
	virtual bool CanInteract_Implementation(AActor* Interactor) const { return true; }

	/** Blueprint 재정의와 C++ 기본 구현을 모두 존중하며 CanInteract를 호출하는 공통 진입점. */
	static bool CanInteractWith(const UObject* Target, AActor* Interactor);

	/**
	 * 상호작용을 서버의 검증된 ServerInteract 경로에서 실행해야 하는지 여부.
	 * 안전한 기본값은 서버 실행이다. 화면만 여는 로컬 전용 대상만 명시적으로 false를 반환한다.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "MD|Interaction")
	bool RequiresServerInteraction() const;
	virtual bool RequiresServerInteraction_Implementation() const { return true; }

	/**
	 * 물리 픽업 태그가 있는 대상이 이번 입력에서 일반 Interact를 먼저 처리할지 조회한다.
	 * 기존 픽업 대상의 동작을 보존하기 위해 기본값은 false다.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "MD|Interaction")
	bool ShouldPrioritizeInteractionOverPhysicsPickup(AActor* Interactor) const;
	virtual bool ShouldPrioritizeInteractionOverPhysicsPickup_Implementation(
		AActor* Interactor) const
	{
		return false;
	}

	/**
	 * 물리 오브젝트를 든 상태에서도 이 대상을 조준하고 Interact를 전달할지 여부.
	 * 기본값 false는 기존의 E 놓기 동작을 보존한다.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "MD|Interaction")
	bool SupportsHeldActorInteraction() const;
	virtual bool SupportsHeldActorInteraction_Implementation() const { return false; }

	/** 플레이어의 상호작용 입력을 액터에 전달한다. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "MD|Interaction")
	void Interact(AActor* Interactor);

	/**
	 * 상호작용 키를 누르고 있는 동안 진행되는 대상인지 여부.
	 * true면 Interact 뒤 키를 뗄 때 InteractReleased가 같은 경로(서버/로컬)로 전달된다.
	 * 기본값 false는 기존 단발 상호작용 동작을 보존한다.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "MD|Interaction")
	bool IsHoldInteraction() const;
	virtual bool IsHoldInteraction_Implementation() const { return false; }

	/** 홀드 대상에서 상호작용 키를 뗐을 때 전달된다. IsHoldInteraction이 true인 대상에만 온다. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "MD|Interaction")
	void InteractReleased(AActor* Interactor);
	virtual void InteractReleased_Implementation(AActor* Interactor) {}
};
