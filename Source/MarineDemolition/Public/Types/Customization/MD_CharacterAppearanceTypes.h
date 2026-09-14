#pragma once

#include "CoreMinimal.h"
#include "MD_CharacterAppearanceTypes.generated.h"

// 다른 플레이어에게 보이는 외형 선택. 로스터·팔레트 인덱스만 복제한다.
USTRUCT(BlueprintType)
struct FMD_CharacterAppearance
{
	GENERATED_BODY()

	// DA_CharacterRoster의 Characters 인덱스
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance")
	int32 CharacterIndex = 0;

	// DA_CharacterRoster의 Palette 인덱스
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance")
	int32 ColorIndex = 0;

	bool operator==(const FMD_CharacterAppearance& Other) const
	{
		return CharacterIndex == Other.CharacterIndex && ColorIndex == Other.ColorIndex;
	}

	bool operator!=(const FMD_CharacterAppearance& Other) const
	{
		return !(*this == Other);
	}
};
