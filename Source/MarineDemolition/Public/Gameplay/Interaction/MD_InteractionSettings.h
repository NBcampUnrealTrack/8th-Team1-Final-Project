#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Types/Interaction/MD_InteractionTypes.h"
#include "MD_InteractionSettings.generated.h"

class UDataTable;

/** Project Settings -> Game -> MD Interaction에서 공용 상호작용 이름표를 지정한다. */
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "MD Interaction"))
class MARINEDEMOLITION_API UMD_InteractionSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	// 유형별 공용 행동 문구의 기본값 설정
	UMD_InteractionSettings();

	static const UMD_InteractionSettings* Get();

	virtual FName GetCategoryName() const override { return FName(TEXT("Game")); }
	virtual FName GetSectionName() const override { return FName(TEXT("MD Interaction")); }

	/** 행 구조체는 FMD_InteractionPresentationRow이며, 행 이름은 대상 종류의 ID다. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Presentation")
	TSoftObjectPtr<UDataTable> InteractionPresentationTable;

	// 입력 키를 제외하고 설정하는 유형별 공용 행동 문구
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Presentation")
	TMap<EMD_InteractionPromptType, FText> InteractionPromptTexts;

	/** 설정된 공용 이름표를 동기 로드한다. 미지정이면 nullptr. */
	const UDataTable* GetInteractionPresentationTable() const;

	// 선택된 유형의 행동 문구 조회와 미지정 유형의 숨김 처리
	FText GetPromptText(EMD_InteractionPromptType PromptType) const;
};
