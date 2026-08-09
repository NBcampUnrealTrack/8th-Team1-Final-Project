#pragma once

#include "CoreMinimal.h"
#include "Stats/Stats.h"

/**
 * AMD_CuttableWall 구현 파일들이 공유하는 내부 선언.
 *
 * 이 헤더는 Private 아래에 있어 모듈 밖에서는 보이지 않는다. 벽 구현이 여러
 * 번역 단위로 나뉘면서 파일 하나에 있던 통계와 상수를 공유해야 해졌을 뿐,
 * 새로운 공개 API가 아니다.
 *
 * 파일이 나뉜 기준은 "나중에 오염 표면을 컴포넌트로 뽑을 때의 경계"다.
 * MD_CuttableWall.cpp에는 액터로서의 책임(수명·철거·스캔)만 남기고, 오염
 * 표면에 속하는 것은 전부 접미사가 붙은 파일로 옮겼다. 그래서 컴포넌트
 * 추출은 파일을 통째로 옮기는 작업이 된다.
 *
 * | 파일 | 책임 |
 * |---|---|
 * | MD_CuttableWall.cpp             | 액터 수명, 철거(Cut/CanCut), 스캔 |
 * | MD_CuttableWall_Surface.cpp     | 작업면 기하 해석, 셀 초기화, 좌표 변환 |
 * | MD_CuttableWall_Scrape.cpp      | 긁기 적용, 제거율, 따개비 경로 차단 |
 * | MD_CuttableWall_Visual.cpp      | G8 마스크, 데칼, 오버레이 |
 * | MD_CuttableWall_Replication.cpp | Fast Array, 복제 상태 반영 |
 * | MD_CuttableWall_Persistence.cpp | 밤샘 악화, Day 스냅샷, 오염물 드롭 |
 */

/**
 * 긁기 한 번의 비용을 구간별로 나눠 재는 통계.
 *
 * 콘솔에 `stat MD_Scrape`를 입력하면 화면에 구간별 밀리초가 뜬다.
 * 어느 구간이 실제로 비싼지 숫자로 확인한 뒤 최적화 대상을 정하기 위한 것이다.
 * Shipping 빌드에서는 STATS가 꺼져 매크로가 통째로 사라진다.
 *
 * 구현이 여러 파일로 나뉘어 선언과 정의를 분리했다. 정의(DEFINE_STAT)는
 * MD_CuttableWall.cpp 한 곳에만 있다.
 */
DECLARE_STATS_GROUP(TEXT("MD Scrape"), STATGROUP_MD_Scrape, STATCAT_Advanced);
DECLARE_CYCLE_STAT_EXTERN(TEXT("Stroke 전체"), STAT_MD_ScrapeStrokeTotal, STATGROUP_MD_Scrape, MARINEDEMOLITION_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("셀 판정 (Evaluator)"), STAT_MD_ScrapeEvaluate, STATGROUP_MD_Scrape, MARINEDEMOLITION_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("제거율 캐시"), STAT_MD_ScrapeCleanRatio, STATGROUP_MD_Scrape, MARINEDEMOLITION_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("G8 마스크 갱신"), STAT_MD_ScrapeMaskUpdate, STATGROUP_MD_Scrape, MARINEDEMOLITION_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("ISM 오버레이 갱신"), STAT_MD_ScrapeOverlayUpdate, STATGROUP_MD_Scrape, MARINEDEMOLITION_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("복제 발행"), STAT_MD_ScrapePublish, STATGROUP_MD_Scrape, MARINEDEMOLITION_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("완료 판정"), STAT_MD_ScrapeFullyRemovedCheck, STATGROUP_MD_Scrape, MARINEDEMOLITION_API);

/**
 * 여러 구현 파일이 함께 쓰는 상수.
 *
 * 무명 네임스페이스로 두면 파일마다 사본이 생기고, 값을 하나 고칠 때 어느
 * 파일의 것을 고쳐야 하는지가 모호해진다. 이름 있는 네임스페이스에 두고
 * 정의는 MD_CuttableWall.cpp 한 곳에만 둔다.
 *
 * ★ Mask* 파라미터 이름은 데칼 머티리얼과의 계약이다. 머티리얼 쪽 파라미터
 * 이름을 바꾸면 여기도 함께 바꿔야 하며, 어긋나면 오염이 그려지지 않는다.
 */
namespace MD_CuttableWallInternal
{
	extern const FLinearColor NeutralWallColor;
	extern const FName ContaminationMaskTextureParameterName;
	extern const FName ContaminationMaskVScaleParameterName;
	extern const FName ContaminationMaskVOffsetParameterName;
	extern const FName ContaminationMaskUFlipParameterName;
	extern const FName ContaminationMaskHalfTexelUParameterName;
	extern const FName ContaminationMaskHalfTexelVParameterName;
}
