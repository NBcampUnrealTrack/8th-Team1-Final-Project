#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Types/Merchant/MD_QuestRewardTypes.h"
#include "MD_QuestDefinition.generated.h"

/** 모든 회수 퀘스트 자산이 공유하는 식별·표시·보상 정보를 소유하는 부모 자산 */
UCLASS(Abstract, BlueprintType)
class MARINEDEMOLITION_API UMD_QuestDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Asset Manager와 저장 데이터가 사용할 공통 퀘스트 PrimaryAssetId 생성 함수 */
	virtual FPrimaryAssetId GetPrimaryAssetId() const override;

public:
	/** 퀘스트 자산을 Asset Manager에서 분류할 PrimaryAssetType */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Quest|Identity")
	FPrimaryAssetType QuestPrimaryAssetType;

	/** 퀘스트 카드와 HUD에 표시할 이름 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Quest|Identity")
	FText QuestName;

	/** 수락 카드에 처음 표시할 발주처 대사 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Quest|Dialogue")
	FText OfferDialogue;

	/** 성공 크레딧·점수·호감도를 정산하기 위한 공통 보상 데이터 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Quest|Reward")
	FMD_QuestRewardData SuccessReward;

	/** 실패(미회수) 크레딧·점수·호감도를 정산하기 위한 공통 패널티 데이터 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Quest|Reward")
	FMD_QuestRewardData UnrecoveredFailurePenalty;

	/** 실패(파괴) 크레딧·점수·호감도를 정산하기 위한 공통 패널티 데이터 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Quest|Reward")
	FMD_QuestRewardData DestroyedFailureReward;
};
