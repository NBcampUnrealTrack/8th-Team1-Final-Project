// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "MD_TrashSettings.generated.h"

class UDataTable;

/**
 * Project Settings → Game → MD Trash.
 * 쓰레기 수치 테이블의 위치를 프로젝트에서 한 번만 지정한다. 조회는 UMD_TrashLibrary를 거친다.
 */
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "MD Trash"))
class MARINEDEMOLITION_API UMD_TrashSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	static const UMD_TrashSettings* Get();

	virtual FName GetCategoryName() const override { return FName(TEXT("Game")); }
	virtual FName GetSectionName() const override { return FName(TEXT("MD Trash")); }

#if WITH_EDITOR
	virtual FText GetSectionText() const override;
	virtual FText GetSectionDescription() const override;
#endif

	/** 컨테이너 5종 수치. 행 구조체는 FMD_TrashCategoryRow */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Trash")
	TSoftObjectPtr<UDataTable> TrashCategoryTable;

	/** 산재 쓰레기 종류. 행 구조체는 FMD_TrashKindRow, 행 이름이 종류 ID */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Trash")
	TSoftObjectPtr<UDataTable> TrashKindTable;

	// 인원 1명·표준 난이도 기준의 맵 전체 쓰레기 개수. SpawnShare를 쓰는 볼륨들이 이 총량을 나눠 받는다
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Trash Spawn", meta = (ClampMin = "0", UIMin = "0"))
	int32 ProceduralTrashBaseCount = 100;

	// 인원별 총량 배율. [0]이 1인이며, 인원이 배열보다 많으면 마지막 값을 쓴다. 비어 있으면 1.0. 값은 DefaultGame.ini에서 채운다
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Trash Spawn", meta = (ClampMin = "0.0", UIMin = "0.0"))
	TArray<float> PlayerCountTrashMultipliers;

	// 계약 난이도 배율(안전 0.8·표준 1.0·위험 1.2·극한 1.4)을 총량에 곱할지 여부
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Trash Spawn")
	bool bScaleTrashCountByDifficulty = true;

	// 인원수에 해당하는 배율. 0명 이하는 1인으로 본다
	float GetPlayerCountTrashMultiplier(int32 PlayerCount) const;

	/** 미지정이면 nullptr. 행 구조체 검사와 경고는 UMD_TrashLibrary가 한다 */
	const UDataTable* GetTrashCategoryTable() const;

	/** 미지정이면 nullptr. 행 구조체 검사와 경고는 UMD_TrashLibrary가 한다 */
	const UDataTable* GetTrashKindTable() const;
};
