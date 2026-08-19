#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "Types/Merchant/MD_MerchantTypes.h"
#include "Types/Merchant/MD_QuestRewardTypes.h"
#include "MD_QuestDefinition.generated.h"

class AMD_SampleItem;

/** 메인·반복 배치와 무관하게 순차 목표 하나를 정의하는 공통 페이즈 데이터 */
USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_QuestPhaseData
{
	GENERATED_BODY()

	/** 납품·청소·마감 등 현재 페이즈의 목표 분류 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Quest|Phase")
	EMD_QuestObjectiveType ObjectiveType = EMD_QuestObjectiveType::RecoverIntact;

	/** 이 페이즈에 스폰하거나 귀속할 샘플 액터 클래스이며 비어 있으면 클래스를 제한하지 않는 설정 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Quest|Phase")
	TSubclassOf<AMD_SampleItem> RequiredItemClass;

	/** 비어 있으면 등급을 제한하지 않고 값이 있으면 하나와 일치해야 하는 허용 등급 목록 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Quest|Phase")
	FGameplayTagContainer AcceptedGradeTags;

	/** 현재 목표를 충족하기 위해 필요한 이벤트 누적 횟수 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Quest|Phase", meta = (ClampMin = "1", UIMin = "1"))
	int32 RequiredCount = 1;

	/** 현재 페이즈 진행 중 HUD 퀘스트 카드에 표시할 목표 내용 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Quest|Phase")
	FText HUDDescription;

	/** 현재 페이즈 목표를 완료했을 때 표시할 대사 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Quest|Phase")
	FText CompletionDialogue;

public:
	/** 퀘스트에 귀속된 제출 샘플이 현재 페이즈의 등급·손상 조건을 충족하는지 판정하는 함수 */
	bool AcceptsSample(const FGameplayTag& GradeTag, bool bIntact) const;

	/** 퀘스트에 귀속된 샘플의 등급이 현재 회수 목표에 포함되는지 판정하는 함수 */
	bool TargetsSample(const FGameplayTag& GradeTag) const;

};

/** 메인·반복 역할을 Site 목록 배치로 결정하는 단일 퀘스트 원본 자산 */
UCLASS(BlueprintType)
class MARINEDEMOLITION_API UMD_QuestDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Asset Manager와 저장 데이터가 사용할 공통 퀘스트 PrimaryAssetId 생성 함수 */
	virtual FPrimaryAssetId GetPrimaryAssetId() const override;

	/** 저장·복제된 PrimaryAssetId로 공통 퀘스트 원본 자산을 동기 로드하는 함수 */
	static UMD_QuestDefinition* LoadDefinition(const FPrimaryAssetId& DefinitionId);

public:
	/** 퀘스트 카드와 HUD에 표시할 이름 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Quest|Identity")
	FText QuestName;

	/** 메인·반복 역할과 무관하게 사용하는 단일 또는 복수 순차 목표 목록 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Quest|Objective")
	TArray<FMD_QuestPhaseData> Phases;

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

public:
	/** 모든 공통 퀘스트 DA가 공유하는 변경 불가 Asset Manager 타입 */
	static const FPrimaryAssetType QuestPrimaryAssetType;

	/** 공통 페이즈 목록이 런타임에서 사용할 수 있는지 확인하는 함수 */
	bool IsValidDefinition() const;

	/** 현재 목표 인덱스가 공통 페이즈 목록에서 유효한지 확인하는 함수 */
	bool HasPhase(int32 PhaseIndex) const;

	/** 현재 목표 인덱스의 원본 페이즈 반환 */
	const FMD_QuestPhaseData* GetPhase(int32 PhaseIndex) const;

	/** 전체 페이즈 수 반환 */
	int32 GetPhaseCount() const;

	/** 현재 페이즈의 목표 분류 반환 */
	EMD_QuestObjectiveType GetObjectiveType(int32 PhaseIndex) const;

	/** 현재 목표의 완료 요구 수량 반환 */
	int32 GetRequiredCount(int32 PhaseIndex) const;

	/** 현재 목표의 HUD 설명 반환 */
	FText GetHUDDescription(int32 PhaseIndex) const;

	/** 현재 목표 완료 후 표시할 대사 반환 */
	FText GetCompletionDialogue(int32 PhaseIndex) const;

	/** 현재 목표가 인정하는 등급 목록 반환 */
	FGameplayTagContainer GetAcceptedGradeTags(int32 PhaseIndex) const;

	/** 현재 목표가 요구하는 샘플 액터 클래스 반환 */
	TSubclassOf<AMD_SampleItem> GetRequiredItemClass(int32 PhaseIndex) const;

	/** 현재 목표가 손상된 회수물도 인정하는지 판정하는 함수 */
	bool AllowsDamagedTarget(int32 PhaseIndex) const;

	/** 퀘스트에 귀속된 제출 샘플이 현재 목표의 등급·손상 조건을 충족하는지 판정하는 함수 */
	bool AcceptsSample(int32 PhaseIndex, const FGameplayTag& GradeTag, bool bIntact) const;

	/** 퀘스트에 귀속된 파괴 샘플의 등급이 현재 회수 목표와 일치하는지 판정하는 함수 */
	bool TargetsSample(int32 PhaseIndex, const FGameplayTag& GradeTag) const;

	/** 마지막 페이즈의 목표 수량을 충족하여 보상을 받을 수 있는지 판정하는 함수 */
	bool IsObjectiveComplete(int32 PhaseIndex, int32 CurrentProgress) const;
};
