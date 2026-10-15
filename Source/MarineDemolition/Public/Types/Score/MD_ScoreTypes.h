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

// 점수 일일 원장에 사용하는 정산 분류
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

// 계약 중 실제 잔액에 반영된 금액의 서버 원시 로그
USTRUCT(BlueprintType)
struct FMD_CreditLogEntry
{
    GENERATED_BODY()

    // 계약 시작 이후 금액이 반영된 게임 시간
    UPROPERTY(BlueprintReadOnly, Category = "Credit History")
    double ElapsedSeconds = 0.0;

    // 계약 내 고정 개인 기록 인덱스이며 INDEX_NONE은 개인에게 귀속되지 않는 시스템 금액
    UPROPERTY(BlueprintReadOnly, Category = "Credit History")
    int32 PlayerIndex = INDEX_NONE;

    // 잔액 상하한 적용 이후의 실제 증감액
    UPROPERTY(BlueprintReadOnly, Category = "Credit History")
    int32 Amount = 0;
};

// 개인별 이름과 누적 순수익 여섯 시점을 전달하는 그래프 기록
USTRUCT(BlueprintType)
struct FMD_PlayerCreditHistory
{
    GENERATED_BODY()

    // 미참여 슬롯과 실제로 순수익이 영점인 플레이어 구분
    UPROPERTY(BlueprintReadOnly, Category = "Credit History")
    bool bHasPlayer = false;

    // 동명이인 및 재접속을 구분하는 서버 확정 플레이어 키
    UPROPERTY(BlueprintReadOnly, Category = "Credit History")
    FString PlayerKey;

    // 접속 종료 후에도 정산에 남길 플레이어 표시 이름
    UPROPERTY(BlueprintReadOnly, Category = "Credit History")
    FString PlayerName;

    // 해당 플레이어가 발생시킨 양수 금액의 합계
    UPROPERTY(BlueprintReadOnly, Category = "Credit History")
    int64 CreditsGained = 0;

    // 해당 플레이어가 발생시킨 음수 금액의 절댓값 합계
    UPROPERTY(BlueprintReadOnly, Category = "Credit History")
    int64 CreditsSpent = 0;

    // 획득 금액에서 지출 금액을 뺀 최종 순수익
    UPROPERTY(BlueprintReadOnly, Category = "Credit History")
    int64 NetCredits = 0;

    // X는 계약 경과 초이며 Y는 해당 시점까지의 누적 순수익
    UPROPERTY(BlueprintReadOnly, Category = "Credit History")
    TArray<FVector2D> Samples;
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

    // 실제 정산 시점 또는 현재 조회 시점까지의 계약 경과 초
    UPROPERTY(BlueprintReadOnly, Category = "Credit History")
    double CreditHistoryDuration = 0.0;

    // 시작 영점과 종료 시점을 포함한 균등한 여섯 시각
    UPROPERTY(BlueprintReadOnly, Category = "Credit History")
    TArray<double> CreditSampleTimes;

    // 참가 순서를 유지하는 네 개인 슬롯이며 빈 슬롯은 bHasPlayer로 구분
    UPROPERTY(BlueprintReadOnly, Category = "Credit History")
    TArray<FMD_PlayerCreditHistory> PlayerCreditHistories;

    // 개인 그래프에서 제외하며 전체 집계에 포함할 시설 비용과 착수금 및 평가 보상 등의 시스템 증감
    UPROPERTY(BlueprintReadOnly, Category = "Credit History")
    int64 SharedCreditDelta = 0;

    // 공용 금액을 포함한 실제 원장의 전체 증감액
    UPROPERTY(BlueprintReadOnly, Category = "Credit History")
    int64 TotalCreditDelta = 0;

    // 개인 및 시스템 수입과 상쇄되기 이전의 모든 실제 차감액 절댓값 합계
    UPROPERTY(BlueprintReadOnly, Category = "Credit History")
    int64 TotalCreditsSpent = 0;

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
