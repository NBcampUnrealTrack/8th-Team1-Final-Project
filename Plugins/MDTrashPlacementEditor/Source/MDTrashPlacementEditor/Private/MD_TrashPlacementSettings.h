#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "UObject/SoftObjectPtr.h"
#include "MD_TrashPlacementSettings.generated.h"

class AMD_TrashItem;
class UMaterialInterface;

/** Project Settings > Plugins > MD Trash Placement에서 배치 도구의 에셋을 직접 지정한다. */
UCLASS(config = Editor, defaultconfig, meta = (DisplayName = "MD Trash Placement"))
class MDTRASHPLACEMENTEDITOR_API UMD_TrashPlacementSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetCategoryName() const override { return FName(TEXT("Plugins")); }

	/** 배치할 쓰레기 Blueprint 클래스. 미할당이면 배치를 차단한다. 변경 후 도구를 다시 연다. */
	UPROPERTY(config, EditAnywhere, Category = "Assets")
	TSoftClassPtr<AMD_TrashItem> TrashItemClass;

	/** 미리보기 재질. HighlightColor와 OpacityAmount 파라미터를 사용한다. 변경 후 도구를 다시 연다. */
	UPROPERTY(config, EditAnywhere, Category = "Assets")
	TSoftObjectPtr<UMaterialInterface> GhostPreviewMaterial;
};
