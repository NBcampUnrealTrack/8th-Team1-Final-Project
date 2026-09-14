#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Types/Customization/MD_CharacterAppearanceTypes.h"
#include "MD_CharacterAppearanceLibrary.generated.h"

class UMD_CharacterRosterData;

// 마지막으로 확정한 외형 하나만 로컬에 저장한다. 게임 진행 저장과는 별개다.
UCLASS()
class MARINEDEMOLITION_API UMD_CharacterAppearanceSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	UPROPERTY(SaveGame)
	FMD_CharacterAppearance Appearance;
};

// 로스터 조회와 로컬 저장·복원 공용 진입점
UCLASS()
class MARINEDEMOLITION_API UMD_CharacterAppearanceLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// Project Settings에 지정된 로스터. 미지정이면 nullptr
	UFUNCTION(BlueprintPure, Category = "MD|Customization")
	static UMD_CharacterRosterData* GetCharacterRoster();

	// 저장된 선택이 없으면 false
	UFUNCTION(BlueprintCallable, Category = "MD|Customization")
	static bool LoadLocalAppearance(FMD_CharacterAppearance& OutAppearance);

	UFUNCTION(BlueprintCallable, Category = "MD|Customization")
	static bool SaveLocalAppearance(const FMD_CharacterAppearance& Appearance);
};
