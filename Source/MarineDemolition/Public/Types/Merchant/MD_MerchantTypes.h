
#pragma once

#include "CoreMinimal.h"
#include "MD_MerchantTypes.generated.h"

class UMD_QuestDefinition;

//퀘스트 목록중 메인과 반복퀘스트. 로어퀘스트는 별도 취급
UENUM(BlueprintType)
enum class EMD_QuestKind : uint8
{
	Main UMETA(DisplayName = "메인 퀘스트"),
	Repeat UMETA(DisplayName = "반복퀘스트")
};

//퀘스트 로직은 회수형만 판정. 나머지2~3단계 호감도와 대출은 구조만 생성
UENUM(BlueprintType)
enum class EMD_QuestObjectiveType :uint8
{
	RecoverIntact UMETA(DisplayName = "회수"),
	RecoverDamaged UMETA(DisplayName = "손상 허용 회수"),
	Cleanup        UMETA(DisplayName = "청소 계약"),
	Sealing        UMETA(DisplayName = "마감 계약"),
	Composite      UMETA(DisplayName = "복합")
};

//메인퀘스트 상태(잠금, 활성화, 성공)
UENUM(BlueprintType)
enum class EMD_MainQuestStatus : uint8
{
	Locked    UMETA(DisplayName = "Locked"),
	Active    UMETA(DisplayName = "Active"),
	Completed UMETA(DisplayName = "Completed")
};

//반복퀘스트 상태(메인에 수락 취소 추가)
UENUM(BlueprintType)
enum class EMD_RepeatQuestStatus : uint8
{
	Offered   UMETA(DisplayName = "Offered"),
	Accepted  UMETA(DisplayName = "Accepted"),
	Declined  UMETA(DisplayName = "Declined"),
	Completed UMETA(DisplayName = "Completed"),
	Failed    UMETA(DisplayName = "Failed")
};

//실패사유
UENUM(BlueprintType)
enum class EMD_QuestFailureReason : uint8
{
	NotDelivered UMETA(DisplayName = "NotDelivered"),
	Destroyed    UMETA(DisplayName = "Destroyed")
};

//메인퀘스트 구조
USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_MainQuestState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest")
	TObjectPtr<UMD_QuestDefinition> Definition = nullptr;

	/* 메인 퀘스트가 열리는 Day. 애셋 배열 순서로 결정된다. */
	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest")
	int32 UnlockDay = 1;

	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest")
	EMD_MainQuestStatus Status = EMD_MainQuestStatus::Locked;

	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest")
	int32 CurrentProgress = 0;

	bool IsActive() const { return Status == EMD_MainQuestStatus::Active; }
};

//반복퀘스트 구조
USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_RepeatQuestState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest")
	TObjectPtr<UMD_QuestDefinition> Definition = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest")
	EMD_RepeatQuestStatus Status = EMD_RepeatQuestStatus::Offered;

	UPROPERTY(BlueprintReadOnly, Category = "MD|Quest")
	int32 CurrentProgress = 0;

	bool IsActive() const { return Status == EMD_RepeatQuestStatus::Accepted; }
};