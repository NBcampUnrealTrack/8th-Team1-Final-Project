
#pragma once

#include "CoreMinimal.h"
#include "Engine/AssetManagerTypes.h"
#include "MD_MerchantTypes.generated.h"

UENUM(BlueprintType)
enum class EMD_QuestObjectiveType : uint8
{
	RecoverIntact UMETA(DisplayName = "회수"),
	RecoverDamaged UMETA(DisplayName = "손상 허용 회수"),
	Cleanup        UMETA(DisplayName = "청소 계약"),
	Sealing        UMETA(DisplayName = "마감 계약"),
	Composite      UMETA(DisplayName = "복합")
};

UENUM(BlueprintType)
enum class EMD_MainQuestStatus : uint8
{
	Locked    UMETA(DisplayName = "잠김"),
	Active    UMETA(DisplayName = "진행중"),
	Completed UMETA(DisplayName = "완료"),
	Failed    UMETA(DisplayName = "실패")
};

UENUM(BlueprintType)
enum class EMD_RepeatQuestStatus : uint8
{
	Offered   UMETA(DisplayName = "제시"),
	Accepted  UMETA(DisplayName = "수락"),
	Declined  UMETA(DisplayName = "거절"),
	Completed UMETA(DisplayName = "완료"),
	Failed    UMETA(DisplayName = "실패")
};

UENUM(BlueprintType)
enum class EMD_QuestFailureReason : uint8
{
	None         UMETA(DisplayName = "없음"),
	NotDelivered UMETA(DisplayName = "미회수"),
	Destroyed    UMETA(DisplayName = "파괴")
};

USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_MainQuestState
{
	GENERATED_BODY()

	/** 메인 역할로 배치된 공통 퀘스트 원본을 저장·복제 환경에서 다시 찾기 위한 식별자 */
	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest")
	FPrimaryAssetId DefinitionId;

	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest")
	int32 UnlockDay = 1;

	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest")
	EMD_MainQuestStatus Status = EMD_MainQuestStatus::Locked;

	/** 메인 역할로 배치된 다단계 퀘스트의 현재 목표 인덱스 */
	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest")
	int32 CurrentPhaseIndex = 0;

	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest")
	int32 CurrentProgress = 0;

	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest")
	EMD_QuestFailureReason FailureReason = EMD_QuestFailureReason::None;

	bool IsActive() const { return Status == EMD_MainQuestStatus::Active; }

	/** 원본 메인 퀘스트를 조회할 수 있는 식별자인지 확인하는 함수 */
	bool HasValidDefinitionId() const { return DefinitionId.IsValid(); }
};

USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_RepeatQuestState
{
	GENERATED_BODY()

	/** 반복 역할로 배치된 공통 퀘스트 원본을 저장·복제 환경에서 다시 찾기 위한 식별자 */
	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest")
	FPrimaryAssetId DefinitionId;

	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest")
	EMD_RepeatQuestStatus Status = EMD_RepeatQuestStatus::Offered;

	/** 다중 페이즈 반복 퀘스트에서 현재 활성 회수 목표의 인덱스 */
	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest")
	int32 CurrentPhaseIndex = 0;

	/** 현재 페이즈 안에서 제출 완료된 회수물 수량 */
	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest")
	int32 CurrentProgress = 0;

	/** 진행 중이거나 보상 수령을 기다리는 수락 상태인지 확인하는 함수 */
	bool IsActive() const
	{
		bool bOutActive = Status == EMD_RepeatQuestStatus::Accepted;
		return bOutActive;
	}

	/** 원본 반복 퀘스트를 조회할 수 있는 식별자인지 확인하는 함수 */
	bool HasValidDefinitionId() const
	{
		bool bOutValid = DefinitionId.IsValid();
		return bOutValid;
	}
};
