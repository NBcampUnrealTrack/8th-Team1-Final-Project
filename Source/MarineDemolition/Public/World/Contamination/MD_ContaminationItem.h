#pragma once

#include "CoreMinimal.h"
#include "World/Contamination/MD_ScrapedDebrisBase.h"
#include "MD_ContaminationItem.generated.h"

/**
 * 헤라로 긁어낸 오염물 덩어리 (기획 02 §1 청소 요소 표)
 *
 * 대상은 "벽·바닥·설비 표면의 바이오필름·도장·점액층"이다. 따개비와 달리 보통벽
 * 제한이 없어 철거 대상 벽에도 붙는다.
 *
 * 조각벽과 달리 칸이 갈리지 않는다. 오염물은 언제나 오염된 쓰레기다.
 * 그래서 복제할 상태가 없고 베이스에 더할 것도 내용물 태그 하나뿐이다.
 */
UCLASS()
class MARINEDEMOLITION_API AMD_ContaminationItem : public AMD_ScrapedDebrisBase
{
	GENERATED_BODY()

public:
	AMD_ContaminationItem();
};
