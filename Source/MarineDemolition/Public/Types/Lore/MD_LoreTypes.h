#pragma once

#include "CoreMinimal.h"
#include "Engine/AssetManagerTypes.h"
#include "GameplayTagContainer.h"
#include "MD_LoreTypes.generated.h"

/** 전체 로어의 영구 누적 발견 상태를 저장하는 서버 원장 데이터 */
USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_LoreData
{
	GENERATED_BODY()

public:
	/** 인스턴스 구조체 컨테이너에서 로어 필드를 식별하기 위한 키 */
	static const FGameplayTag DataFieldKey;

	/** 최초 조사 완료 여부와 코덱스 공개 상태를 복원하는 전역 로어 에셋 목록 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save Data")
	TArray<FPrimaryAssetId> DiscoveredLoreAssetIds;

public:
	/** 빈 식별자와 중복 발견 기록의 저장 적용을 차단하는 검사 */
	bool IsStructurallyValid() const;
};
