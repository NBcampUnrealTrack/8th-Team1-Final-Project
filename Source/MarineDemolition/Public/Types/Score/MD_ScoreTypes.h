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

// 작업 정산 페이로드. 서버 원장 응답과 마감 보고서가 공유한다
USTRUCT(BlueprintType)
struct FMD_InspectionReport
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Score")
	bool bValid = false;

	// 계약 누적 점수
	UPROPERTY(BlueprintReadOnly, Category = "Score")
	int32 Score = 0;

    // 보고서 시점의 누적 점수 (Score와 동일, UI 호환용)
    UPROPERTY(BlueprintReadOnly, Category = "Score")
    int32 CumulativeScore = 0;

    // 일일 증감과 같은 시점에 서버가 확정한 보유 크레딧
    UPROPERTY(BlueprintReadOnly, Category = "Credit")
    int32 Credits = 0;

    // 보고서가 집계된 현장의 데이터 에셋 표시 이름
    UPROPERTY(BlueprintReadOnly, Category = "Daily Report")
    FText SiteName;

	// 정산 화면 합계 연출용 점수 (계약 누적 점수와 동일)
	UPROPERTY(BlueprintReadOnly, Category = "Score")
	int32 DailyScore = 0;

	// 서버 점수 Map을 RPC와 복제가 가능한 항목 배열로 전달
	UPROPERTY(BlueprintReadOnly, Category = "Score")
	TArray<FMD_DailyReportValue> DailyScoreTotals;

	// 서버 크레딧 Map을 RPC와 복제가 가능한 항목 배열로 전달
	UPROPERTY(BlueprintReadOnly, Category = "Credit")
	TArray<FMD_DailyReportValue> DailyCreditTotals;
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
};
