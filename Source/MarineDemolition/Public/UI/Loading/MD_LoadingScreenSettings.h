#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "MD_LoadingScreenSettings.generated.h"

class UTexture2D;
class UFont;

/**
 * Project Settings → Game → MD Loading Screen.
 * 로딩 화면이 쓰는 에셋을 프로젝트에서 한 번만 지정한다. 코드가 경로를 알지 않게 하기 위한 것이며,
 * 에셋을 옮겨도 여기만 다시 지정하면 된다.
 */
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "MD Loading Screen"))
class MARINEDEMOLITION_API UMD_LoadingScreenSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	static const UMD_LoadingScreenSettings* Get();

	virtual FName GetCategoryName() const override { return FName(TEXT("Game")); }
	virtual FName GetSectionName() const override { return FName(TEXT("MD Loading Screen")); }

#if WITH_EDITOR
	virtual FText GetSectionText() const override;
	virtual FText GetSectionDescription() const override;
#endif

	/** 파도형 진행 트랙 위를 이동하는 작업선 텍스처. 미지정이면 트랙만 그린다. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Loading Screen")
	TSoftObjectPtr<UTexture2D> ShipTexture;

	/** PEAK식 항로 종점에 표시할 등대 아이콘. 투명 배경 PNG를 사용한다. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Loading Screen")
	TSoftObjectPtr<UTexture2D> BeaconTexture;

	/** 로딩 화면 제목과 부제에 사용할 폰트. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Loading Screen")
	TSoftObjectPtr<UFont> Font;

	// 미지정이면 nullptr. 로딩 시작 직전에 한 번만 동기 로드하고 서브시스템이 붙들고 있는다.
	UTexture2D* GetShipTexture() const;
	UTexture2D* GetBeaconTexture() const;
	UFont* GetFont() const;
};
