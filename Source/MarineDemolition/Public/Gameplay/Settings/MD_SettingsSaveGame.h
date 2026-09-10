#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "Gameplay/Settings/MD_SettingsTypes.h"
#include "StructUtils/InstancedStruct.h"
#include "MD_SettingsSaveGame.generated.h"

// 게임 진행 저장과 분리하여 로컬 플레이어 설정만 저장한다.
UCLASS()
class MARINEDEMOLITION_API UMD_SettingsSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	// 카테고리별 저장 데이터를 실제 구조체 타입과 함께 보관한다.
	UPROPERTY(SaveGame)
	TMap<EMD_SettingsCategory, FInstancedStruct> SettingDataMap;
};
