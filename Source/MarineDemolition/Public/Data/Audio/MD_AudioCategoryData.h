#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Data/Audio/MD_AudioTypes.h"
#include "MD_AudioCategoryData.generated.h"

/**
 * 카테고리 하나 분량의 효과음 정의 묶음. 카테고리별로 파일을 나눠 두면
 * 작업자가 서로 다른 카테고리를 동시에 편집해도 에셋 잠금이 겹치지 않는다.
 *
 * 에셋 이름은 `DA_MD_Audio_<카테고리>` 를 쓴다. 예: `DA_MD_Audio_Clean`
 */
UCLASS(BlueprintType)
class MARINEDEMOLITION_API UMD_AudioCategoryData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** 이 에셋이 담당하는 카테고리. 라이브러리 중복 검사에 쓰인다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Audio")
	EMD_AudioCategory Category = EMD_AudioCategory::Clean;

	/** 이벤트 정의 목록. 같은 EventTag 를 조건만 달리해 여러 개 둘 수 있다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Audio", meta = (TitleProperty = "EventTag"))
	TArray<FMD_AudioEntry> Entries;

#if WITH_EDITOR
	/** Sound 가 비었거나 EventTag 가 없는 항목을 편집 중에 잡아준다 */
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};

/**
 * 카테고리 에셋을 모아 런타임에 하나의 태그 인덱스로 만드는 최상위 카탈로그.
 * 프로젝트 설정(`MD_AudioSettings`)이 이 에셋 하나만 가리킨다.
 */
UCLASS(BlueprintType)
class MARINEDEMOLITION_API UMD_AudioLibrary : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** 포함할 카테고리 에셋 목록 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Audio")
	TArray<TObjectPtr<UMD_AudioCategoryData>> Categories;

	/** 모든 카테고리의 항목을 순서대로 모아 돌려준다 */
	void CollectEntries(TArray<FMD_AudioEntry>& OutEntries) const;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};
