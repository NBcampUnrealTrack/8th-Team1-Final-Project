#pragma once

#include "CoreMinimal.h"
#include "GameFramework/OnlineReplStructs.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MD_VoiceVolumeSubsystem.generated.h"

UCLASS()
class MARINEDEMOLITION_API UMD_VoiceVolumeSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

protected:
	// 월드와 PlayerState 교체 이후에도 현재 클라이언트에서 유지할 플레이어별 음성 배율
	UPROPERTY(Transient)
	TMap<FString, float> PlayerVolumeMultipliers;

public:
	// 유효한 온라인 식별자에 저장된 현재 클라이언트의 음성 배율 조회
	bool FindVolumeMultiplier(FUniqueNetIdRepl PlayerUniqueId, float& OutVolumeMultiplier);

	// 유효한 온라인 식별자에 대한 현재 클라이언트의 음성 배율 보존
	bool StoreVolumeMultiplier(FUniqueNetIdRepl PlayerUniqueId, float VolumeMultiplier);

protected:
	// 온라인 제공자와 식별자 값을 함께 구분하는 플레이어 키 생성
	FString BuildPlayerKey(FUniqueNetIdRepl PlayerUniqueId);
};
