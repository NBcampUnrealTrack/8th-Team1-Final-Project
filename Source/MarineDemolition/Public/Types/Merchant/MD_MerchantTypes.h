
#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "MD_MerchantTypes.generated.h"

class UWorld;

UENUM(BlueprintType)
enum class EMD_QuestKind : uint8
{
	Main UMETA(DisplayName = "메인 퀘스트"),
	Repeat UMETA(DisplayName = "반복퀘스트")
};

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

/** SiteDefinition.MainQuestLine / RepeatQuestPool 원소. 개별 퀘스트 DA 불필요. */
USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_QuestDefinitionData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Quest")
	FName QuestId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Quest")
	FText QuestName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Quest")
	FText Description;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Quest")
	EMD_QuestKind Kind = EMD_QuestKind::Repeat;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Quest|Objective")
	EMD_QuestObjectiveType ObjectiveType = EMD_QuestObjectiveType::RecoverIntact;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Quest|Objective")
	FGameplayTag TargetGradeTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Quest|Objective")
	int32 RequiredCount = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Quest|Reward")
	int32 RewardCredits = 80;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Quest")
	TSoftObjectPtr<UWorld> QuestMap;

	bool IsValidDefinition() const { return !QuestId.IsNone(); }
};

inline bool MD_QuestDefinitionsEqual(
	const FMD_QuestDefinitionData& A,
	const FMD_QuestDefinitionData& B)
{
	return A.QuestId == B.QuestId && !A.QuestId.IsNone();
}

USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_MainQuestState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest")
	FMD_QuestDefinitionData Definition;

	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest")
	int32 SourceIndex = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest")
	int32 UnlockDay = 1;

	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest")
	EMD_MainQuestStatus Status = EMD_MainQuestStatus::Locked;

	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest")
	int32 CurrentProgress = 0;

	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest")
	EMD_QuestFailureReason FailureReason = EMD_QuestFailureReason::None;

	bool IsActive() const { return Status == EMD_MainQuestStatus::Active; }
};

USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_RepeatQuestState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest")
	FMD_QuestDefinitionData Definition;

	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest")
	EMD_RepeatQuestStatus Status = EMD_RepeatQuestStatus::Offered;

	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest")
	int32 CurrentProgress = 0;

	bool IsActive() const { return Status == EMD_RepeatQuestStatus::Accepted; }
};
