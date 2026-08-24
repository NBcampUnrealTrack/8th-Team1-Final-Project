#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "MD_ScoreTypes.generated.h"

// 샘플 등급
UENUM(BlueprintType)
enum class EMD_SampleGrade : uint8
{
	C UMETA(DisplayName = "C"),
	B UMETA(DisplayName = "B"),
	A UMETA(DisplayName = "A"),
	S UMETA(DisplayName = "S"),
};

// 샘플 점수 발생 상태 (Intact/손상 = 납품 시, 미회수/파괴 = 감점)
UENUM(BlueprintType)
enum class EMD_SampleScoreType : uint8
{
	Intact      UMETA(DisplayName = "Intact 납품"),
	Damaged     UMETA(DisplayName = "손상 납품"),
	Unrecovered UMETA(DisplayName = "미회수"),
	Destroyed   UMETA(DisplayName = "파괴"),
};

// Day2/3/4 점검 판정 결과
UENUM(BlueprintType)
enum class EMD_InspectionResult : uint8
{
	NotApplicable      UMETA(DisplayName = "점검 없음"),
	Passed             UMETA(DisplayName = "통과"),
	ContractCleared    UMETA(DisplayName = "계약 클리어"),
	WarningIssued      UMETA(DisplayName = "경고"),
	EmergencyDayOpened UMETA(DisplayName = "비상 4일차 개방"),
	GameOver           UMETA(DisplayName = "게임오버"),
};

// 점수와 크레딧 일일 원장이 공통으로 사용하는 정산 분류
UENUM(BlueprintType)
enum class EMD_DailyReportCategory : uint8
{
	Retainer UMETA(DisplayName = "착수금"),
	Categorizing UMETA(DisplayName = "분류"),
	Return UMETA(DisplayName = "복귀"),
	Accident UMETA(DisplayName = "사고"),
	Evaluation UMETA(DisplayName = "평가"),
	Quest UMETA(DisplayName = "퀘스트"),
	Other UMETA(DisplayName = "기타"),
};

// Map 원장의 한 항목을 RPC와 복제가 지원하는 배열 요소로 전달하는 구조체
USTRUCT(BlueprintType)
struct FMD_DailyReportValue
{
	GENERATED_BODY()

	// 원장에서 사용한 정산 분류
	UPROPERTY(BlueprintReadOnly, Category = "Daily Report")
	EMD_DailyReportCategory Category = EMD_DailyReportCategory::Other;

	// 해당 정산 분류의 합계
	UPROPERTY(BlueprintReadOnly, Category = "Daily Report")
	int32 Amount = 0;
};

// 등급 하나의 샘플 단가 4종
USTRUCT(BlueprintType)
struct FMD_SampleScoreRow
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Score")
	int32 Intact = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Score")
	int32 Damaged = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Score")
	int32 Unrecovered = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Score")
	int32 Destroyed = 0;
};

// 점검 결과 통지 페이로드. GameState가 복제해 전 클라이언트 UI에 전달
USTRUCT(BlueprintType)
struct FMD_InspectionReport
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Score")
	bool bValid = false;

	UPROPERTY(BlueprintReadOnly, Category = "Score")
	int32 Day = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Score")
	EMD_InspectionResult Result = EMD_InspectionResult::NotApplicable;

	// 판정에 사용된 점수 (경고 감점 적용 전)
	UPROPERTY(BlueprintReadOnly, Category = "Score")
	int32 Score = 0;

	// 일일 정산 화면에서 누적 점수와 분리해 표시할 해당 Day의 점수 증감 합계
	UPROPERTY(BlueprintReadOnly, Category = "Score")
	int32 DailyScore = 0;

	// 서버 점수 Map을 RPC와 복제가 가능한 항목 배열로 전달
	UPROPERTY(BlueprintReadOnly, Category = "Score")
	TArray<FMD_DailyReportValue> DailyScoreTotals;

	// 서버 크레딧 Map을 RPC와 복제가 가능한 항목 배열로 전달
	UPROPERTY(BlueprintReadOnly, Category = "Credit")
	TArray<FMD_DailyReportValue> DailyCreditTotals;

	UPROPERTY(BlueprintReadOnly, Category = "Score")
	int32 Threshold = 0;
};

// 서버 원장 로그 1건. 정산 화면 연결 전까지는 서버에만 보관
USTRUCT()
struct FMD_ScoreEntry
{
	GENERATED_BODY()

	UPROPERTY()
	FGameplayTag EventTag;

	UPROPERTY()
	int32 Delta = 0;

	UPROPERTY()
	int32 Day = 0;
};
