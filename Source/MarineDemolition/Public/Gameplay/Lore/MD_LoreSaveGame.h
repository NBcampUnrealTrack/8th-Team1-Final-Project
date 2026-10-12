#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "MD_LoreSaveGame.generated.h"

// 월드와 계약 저장에서 독립된 개인 로어 기록
UCLASS()
class MARINEDEMOLITION_API UMD_LoreSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	// 지원하지 않는 저장 형식의 덮어쓰기 방지용 버전
	UPROPERTY(SaveGame)
	int32 Version = 1;

	// 계정별 누적 발견 기록의 영구 식별자
	UPROPERTY(SaveGame)
	TArray<FPrimaryAssetId> DiscoveredLoreAssetIds;
};
