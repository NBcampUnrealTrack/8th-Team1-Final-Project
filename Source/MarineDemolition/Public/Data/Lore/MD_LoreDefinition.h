#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "MD_LoreDefinition.generated.h"

/** 현장에 배치하고 코덱스에서 열람하는 로어 기록물 한 건의 정적 정의 */
UCLASS(BlueprintType)
class MARINEDEMOLITION_API UMD_LoreDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	virtual FPrimaryAssetId GetPrimaryAssetId() const override;

public:
	/** Asset Manager에서 로어 정의를 분류하는 Primary Asset 타입 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Lore")
	FPrimaryAssetType AssetType;

	/** 발견 이후 코덱스 목록에 공개되는 기록물 이름 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Lore")
	FText EntryName;

	/** 해독 이후 코덱스 상세 화면에 공개되는 기록물 본문 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Lore", meta = (MultiLine = "true"))
	FText EntryContent;

	/** 배열이 비어 있지 않으면 모두 발견할 때까지 본문을 암호화하는 선행 로어 에셋 목록 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Lore|Decryption", meta = (AllowedTypes = "Lore"))
	TArray<FPrimaryAssetId> DecryptionPrerequisiteLoreIds;

public:
	/** 월드 배치와 코덱스 등록에 사용할 최소 식별 정보의 유효성 확인 */
	bool IsValidDefinition() const;

	/** Primary Asset ID를 Asset Manager에서 로어 정의 에셋으로 해석하는 공통 함수 */
	static UMD_LoreDefinition* LoadDefinition(FPrimaryAssetId LoreAssetId);
};
