#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Types/Travel/MD_TravelTypes.h"
#include "World/Trash/MD_TrashSpawnVolume.h"
#include "MD_TrashSpawnProfile.generated.h"

// 현장별 쓰레기 총량 규칙. 기획자가 에셋에서 기준 개수와 인원·난이도 배율을 직접 조정한다
UCLASS(BlueprintType)
class MARINEDEMOLITION_API UMD_TrashSpawnProfile : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UMD_TrashSpawnProfile();

	virtual FPrimaryAssetId GetPrimaryAssetId() const override;

	// 인원 1명·표준 난이도 기준의 맵 전체 쓰레기 개수
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Total", meta = (ClampMin = "0", UIMin = "0"))
	int32 BaseTrashCount = 100;

	// 인원별 배율. [0]이 1인이며 배열보다 많은 인원은 마지막 값을 쓴다
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Total", meta = (ClampMin = "0.0", UIMin = "0.0"))
	TArray<float> PlayerCountMultipliers;

	// 계약 난이도별 배율. 빠진 난이도는 1.0
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Total", meta = (ClampMin = "0.0", UIMin = "0.0"))
	TMap<EMD_Difficulty, float> DifficultyMultipliers;

	// 볼륨에 종류 확률표가 비어 있을 때 쓰는 기본 종류 확률(합계 100%). 비어 있으면 허용 카테고리 균등 추첨
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kinds", meta = (TitleProperty = "TrashKindId"))
	TArray<FMD_TrashSpawnKindChance> DefaultKindChances;

	UFUNCTION(BlueprintPure, Category = "MD|Trash Spawn")
	float GetPlayerCountMultiplier(int32 PlayerCount) const;

	UFUNCTION(BlueprintPure, Category = "MD|Trash Spawn")
	float GetDifficultyMultiplier(EMD_Difficulty Difficulty) const;

	// 기준 개수 × 인원 배율 × 난이도 배율을 반올림한 총량
	UFUNCTION(BlueprintPure, Category = "MD|Trash Spawn")
	int32 ComputeTotalTrashCount(int32 PlayerCount, EMD_Difficulty Difficulty) const;
};
