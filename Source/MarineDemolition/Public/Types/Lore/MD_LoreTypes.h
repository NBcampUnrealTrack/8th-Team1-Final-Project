#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "MD_LoreTypes.generated.h"

/** 한 현장에서 영구 누적되는 팀 공유 로어 진행 상태 */
USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_LoreSiteProgress
{
	GENERATED_BODY()

public:
	/** 로어 카탈로그와 진행 상태를 연결하는 프로젝트 전역 현장 식별자 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Lore")
	FName SiteId;

	/** 최초 조사 완료 여부와 코덱스 공개 상태를 복원하는 로어 식별자 목록 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Lore")
	TArray<FName> DiscoveredLoreIds;

	/** 전체 수집 보상을 계약과 재접속 이후에도 한 번만 지급하기 위한 상태 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Lore")
	bool bCompletionRewardGranted = false;

public:
	/** 빈 식별자와 중복 발견 기록이 저장 원장에 유입되지 않도록 하는 검사 */
	bool IsStructurallyValid() const;
};

/** 모든 현장의 영구 누적 로어 진행을 저장하는 서버 원장 데이터 */
USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_LoreData
{
	GENERATED_BODY()

public:
	/** 인스턴스 구조체 컨테이너에서 로어 필드를 식별하기 위한 키 */
	static const FGameplayTag DataFieldKey;

	/** 현장별 발견 기록과 완료 보상 지급 상태의 영구 누적 목록 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save Data")
	TArray<FMD_LoreSiteProgress> SiteProgressList;

public:
	/** 중복 현장과 손상된 발견 목록의 저장 적용을 차단하는 검사 */
	bool IsStructurallyValid() const;
};
