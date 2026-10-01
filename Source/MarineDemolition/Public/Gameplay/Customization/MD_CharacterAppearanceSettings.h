#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "MD_CharacterAppearanceSettings.generated.h"

class UMD_CharacterRosterData;

// Project Settings → Game → MD Character Appearance. 로스터 DA와 로컬 저장 슬롯 이름을 지정한다.
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "MD Character Appearance"))
class MARINEDEMOLITION_API UMD_CharacterAppearanceSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	static const UMD_CharacterAppearanceSettings* Get();

	virtual FName GetCategoryName() const override { return FName(TEXT("Game")); }
	virtual FName GetSectionName() const override { return FName(TEXT("MD Character Appearance")); }

	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Roster")
	TSoftObjectPtr<UMD_CharacterRosterData> CharacterRoster;

	// 마지막 선택을 저장하는 로컬 SaveGame 슬롯
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Save")
	FString SaveSlotName = TEXT("MD_CharacterAppearance");

	// 로스터를 동기 로드한다. 미지정이면 nullptr
	UMD_CharacterRosterData* GetRoster() const;
};
