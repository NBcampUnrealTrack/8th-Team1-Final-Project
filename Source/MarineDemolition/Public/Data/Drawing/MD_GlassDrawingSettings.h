#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "MD_GlassDrawingSettings.generated.h"

class UMD_GlassDrawingProfile;

/** Project Settings -> Game -> MD Glass Drawing에서 유리 그리기 기본 프로필을 지정한다. */
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "MD Glass Drawing"))
class MARINEDEMOLITION_API UMD_GlassDrawingSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	static const UMD_GlassDrawingSettings* Get();

	virtual FName GetCategoryName() const override { return FName(TEXT("Game")); }
	virtual FName GetSectionName() const override { return FName(TEXT("MD Glass Drawing")); }

	/** 유리 그리기 서브시스템이 쓰는 기본 프로필. 쿡 포함은 DefaultGame.ini의 DirectoriesToAlwaysCook이 맡는다. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Drawing")
	TSoftObjectPtr<UMD_GlassDrawingProfile> DefaultProfile;

	/** 설정된 기본 프로필을 동기 로드한다. 미지정이면 nullptr. */
	UMD_GlassDrawingProfile* LoadDefaultProfile() const;
};
