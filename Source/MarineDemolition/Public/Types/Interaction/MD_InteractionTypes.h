#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "MD_InteractionTypes.generated.h"

/** 월드 상호작용 대상의 화면 표시 정보를 보관하는 데이터 테이블 행. */
USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_InteractionPresentationRow : public FTableRowBase
{
	GENERATED_BODY()

	/** 조준했을 때 HUD에 표시할 이름. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Interaction")
	FText DisplayName;
};
