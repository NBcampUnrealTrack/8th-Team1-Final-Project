#pragma once

#include "CoreMinimal.h"
#include "Gameplay/Interaction/MD_Interactable.h"
#include "GameFramework/Actor.h"
#include "MD_CustomizationConsole.generated.h"

class UStaticMeshComponent;
class UMD_CharacterCustomizationWidget;

/**
 * 인게임 거울 콘솔. 플레이어가 상호작용하면 로컬에서 캐릭터 커스터마이징 UI를 연다.
 * 서버 상호작용이 필요 없는 로컬 전용 대상이다.
 */
UCLASS(Blueprintable)
class MARINEDEMOLITION_API AMD_CustomizationConsole : public AActor, public IMD_Interactable
{
	GENERATED_BODY()

public:
	AMD_CustomizationConsole();

	virtual bool RequiresServerInteraction_Implementation() const override { return false; }
	virtual void Interact_Implementation(AActor* Interactor) override;
	virtual void SetInteractionHighlighted_Implementation(bool bHighlighted) override;
	virtual FText GetInteractionDisplayName_Implementation() const override;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Console")
	TObjectPtr<UStaticMeshComponent> Mesh;

	/** 열릴 커스터마이징 위젯 블루프린트 클래스. BP에서 할당한다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Console")
	TSubclassOf<UMD_CharacterCustomizationWidget> CustomizationWidgetClass;
};
