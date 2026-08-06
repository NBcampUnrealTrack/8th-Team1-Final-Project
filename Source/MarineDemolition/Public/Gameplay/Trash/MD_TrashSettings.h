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

	/** 미지정이면 nullptr. 행 구조체 검사와 경고는 UMD_TrashLibrary가 한다 */
	const UDataTable* GetTrashCategoryTable() const;

	/** 미지정이면 nullptr. 행 구조체 검사와 경고는 UMD_TrashLibrary가 한다 */
	const UDataTable* GetTrashKindTable() const;
};
