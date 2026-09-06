#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "MD_InteractionSettings.generated.h"

class UDataTable;

/** Project Settings -> Game -> MD Interaction에서 공용 상호작용 이름표를 지정한다. */
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "MD Interaction"))
class MARINEDEMOLITION_API UMD_InteractionSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	static const UMD_InteractionSettings* Get();

	virtual FName GetCategoryName() const override { return FName(TEXT("Game")); }
	virtual FName GetSectionName() const override { return FName(TEXT("MD Interaction")); }

	/** 행 구조체는 FMD_InteractionPresentationRow이며, 행 이름은 대상 종류의 ID다. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Presentation")
	TSoftObjectPtr<UDataTable> InteractionPresentationTable;

	/** 설정된 공용 이름표를 동기 로드한다. 미지정이면 nullptr. */
	const UDataTable* GetInteractionPresentationTable() const;
};
