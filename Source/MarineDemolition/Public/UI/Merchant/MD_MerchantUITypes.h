// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

#include "MD_MerchantUITypes.generated.h"

/**
 * 퀘스트 화면 3탭 (`04` §5.5). 탭마다 데이터 원천·리스트 형태·상호작용 모델이
 * 완전히 달라 하나의 카드 컴포넌트로 묶을 수 없다는 게 기획 확정 내용이다.
 */
UENUM(BlueprintType)
enum class EMD_QuestTab : uint8
{
	Main   UMETA(DisplayName = "메인퀘"),
	Repeat UMETA(DisplayName = "반복퀘"),
	Lore   UMETA(DisplayName = "로어퀘")
};

// 상인 메뉴와 상점 및 대출 화면에서 공유하는 표시 정보
USTRUCT(BlueprintType)
struct FMD_MerchantMenuInfo
{
	GENERATED_BODY()

	/** `06` §4 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Merchant")
	FText DisplayName = FText::FromString(TEXT("노라 브릭스"));

	/** 인터랙트 직후 화면 하단에 뜨는 첫 줄. 톤 가이드는 `06` §4 대사 방향 표. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Merchant")
	FText Greeting = FText::FromString(TEXT("목록이나 봐."));

	// 대화 버튼을 누를 때마다 순환해서 표시하는 상인 잡담 목록
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Merchant", meta = (MultiLine = "false"))
	TArray<FText> TalkLines = {
		FText::FromString(TEXT("그건 일 끝나고. 지금은 목록이나 봐.")),
		FText::FromString(TEXT("나도 저기 있었어. 물자 담당으로. …됐다, 다음 얘기 하자."))
	};

	/** 보유 크레딧 (`07` §8: "오늘 착수금 / 보유 크레딧") */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Credit")
	int32 Credits = 0;

	/** 오늘 착수금. 표준 500, 경고 시 ×0.7, Day4는 0 (`07` §9) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Credit")
	int32 DailyAdvance = 500;

	/** 미상환 잔액 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Loan")
	int32 DebtBalance = 0;

	/** 고정 이자의 기준 원금 (착수금 + 추가 대출, 이자 제외) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Loan")
	int32 LoanPrincipal = 0;

	// 기본 대출 한도 2,000이며 원장 갱신 시 서버 공통 기준(CalculateLoanLimit) 반영
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Loan")
	int32 LoanLimit = 2000;

	/** 주기 고정 이자율. 0.02 = 원금의 2% (대출·경제 회의 계산기) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Loan")
	float DailyInterestRate = 0.02f;

};
