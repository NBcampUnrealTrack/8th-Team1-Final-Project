#include "Gameplay/Interaction/MD_Interactable.h"

#include "UObject/Class.h"
#include "GameFramework/Actor.h"
#include "Gameplay/Interaction/MD_InteractionPresentationLibrary.h"

bool IMD_Interactable::CanInteractWith(const UObject* Target, AActor* Interactor)
{
	if (!IsValid(Target)
		|| !Target->GetClass()->ImplementsInterface(UMD_Interactable::StaticClass()))
	{
		return false;
	}

	static const FName CanInteractFunctionName(TEXT("CanInteract"));
	if (Target->GetClass()->IsFunctionImplementedInScript(CanInteractFunctionName))
	{
		return Execute_CanInteract(Target, Interactor);
	}

	if (const void* NativeAddress = Target->GetNativeInterfaceAddress(UMD_Interactable::StaticClass()))
	{
		const IMD_Interactable* NativeInteractable = static_cast<const IMD_Interactable*>(NativeAddress);
		return NativeInteractable->CanInteract_Implementation(Interactor);
	}

	// Blueprint가 인터페이스만 추가하고 판정 함수를 재정의하지 않았다면 기존 동작을 유지한다.
	return true;
}

FText IMD_Interactable::GetInteractionPrompt_Implementation(AActor* Interactor) const
{
	// 별도 상태 분기가 없는 액터의 선택 유형 안내 반환
	return UMD_InteractionPresentationLibrary::FindActorActionPrompt(Cast<AActor>(this), Interactor);
}
