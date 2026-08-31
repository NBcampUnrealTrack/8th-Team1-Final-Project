#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "MD_LoadingScreenSettings.generated.h"

class UTexture2D;

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

	/** 로딩 중 떠오르는 물방울 텍스처. 미지정이면 물방울 없이 배경과 문구만 그린다. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Loading Screen")
	TSoftObjectPtr<UTexture2D> BubbleTexture;

	/** 파도형 진행 트랙 위를 이동하는 작업선 텍스처. 미지정이면 트랙만 그린다. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Loading Screen")
	TSoftObjectPtr<UTexture2D> ShipTexture;

	/** 수평선 아래에 깔리는 너울 띠 텍스처. 미지정이면 너울을 생략한다. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Loading Screen")
	TSoftObjectPtr<UTexture2D> WaveTexture;

	/** 미지정이면 nullptr. 로딩 시작 직전에 한 번만 동기 로드한다. */
	UTexture2D* GetBubbleTexture() const;
	UTexture2D* GetShipTexture() const;
	UTexture2D* GetWaveTexture() const;
};
