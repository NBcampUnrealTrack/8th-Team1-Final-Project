#pragma once

#include "CoreMinimal.h"
#include "World/Contamination/MD_ScrapedDebrisBase.h"
#include "MD_BarnacleItem.generated.h"

/**
 * 헤라로 떼어낸 따개비 (기획 02 §1 청소 요소 표)
 *
 * 오염물과 같은 오염된 쓰레기 칸으로 가지만 기획상 별도 품목이라 타입을 가른다.
 * 02 §1 표가 "내력벽·칸막이벽·오염물과 별도 품목"이라 못 박고, §1.5 임시 쓰레기통
 * 수용 목록도 오염물과 따개비를 따로 적으며, 05 배치 예산도 따개비x32를 따로 센다.
 *
 * 붙어 있던 따개비 본체는 AMD_Barnacle이고, 이 클래스는 떼어낸 뒤 굴러다니는 산출물이다.
 */
UCLASS()
class MARINEDEMOLITION_API AMD_BarnacleItem : public AMD_ScrapedDebrisBase
{
	GENERATED_BODY()

public:
	AMD_BarnacleItem();
};
