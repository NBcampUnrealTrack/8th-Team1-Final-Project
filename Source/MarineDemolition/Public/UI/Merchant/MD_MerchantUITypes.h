// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

#include "MD_MerchantUITypes.generated.h"

/**
 * 상인 화면이 공통으로 쓰는 정보 묶음.
 *
 * 메뉴와 상점이 크레딧·착수금·호감도를 같은 값으로 보여줘야 하므로 정의를 하나로 둔다.
 * 위젯 헤더가 아니라 이 헤더에 두는 이유: 상점 헤더가 이 구조체를 값으로 쓰는데,
 * 메뉴 위젯 헤더를 include하면 UMG 전체가 딸려 온다 (README §5.5).
 *
 * 수치 출처는 전부 기획서다 — 임의값을 넣지 않는다:
 *   착수금 500 / 대출 한도 800(호감 ±로 400~1200) / 일이자 5%  → `07` §5, §9
 *   호감도 범위 -4.0 ~ +5.0                                  → `02` §7
 *
 * 아직 경제·호감도 런타임 시스템이 없어서 **데이터 소스는 미연동**이다.
 * 기본값은 기획서 확정값이며, 시스템이 붙으면 SetMenuInfo()로 밀어 넣으면 된다.
 */
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

	/**
	 * '대화'를 누를 때마다 순서대로 보여줄 줄. 끝에 닿으면 처음으로 돌아간다.
	 *
	 * 기본값은 `06` §4의 잡담 대사 2줄을 그대로 옮긴 것이다. 원래 이 대사들은
	 * 호감도 티어에 따라 갈리는데(§4.1) 호감도 런타임이 아직 없어 순차 재생으로 둔다.
	 * 대사 추가·교체는 기획에서 이 배열을 채우면 된다 — 코드 수정 불필요.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Merchant", meta = (MultiLine = "false"))
	TArray<FText> TalkLines = {
		FText::FromString(TEXT("그건 일 끝나고. 지금은 목록이나 봐.")),
		FText::FromString(TEXT("나도 저기 있었어. 물자 담당으로. …됐다, 다음 얘기 하자."))
	};

	/** -4.0 ~ +5.0 (`02` §7) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Affinity", meta = (ClampMin = "-4.0", ClampMax = "5.0"))
	float Affinity = 0.f;

	/** 다음 해금까지 남은 호감도. `04` §3 UX 포인트: "호감도 옆에 `다음 해금까지 0.6` 표시" */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Affinity", meta = (ClampMin = "0.0"))
	float AffinityToNextUnlock = 0.f;

	/** 보유 크레딧 (`07` §8: "오늘 착수금 / 보유 크레딧") */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Credit")
	int32 Credits = 0;

	/** 오늘 착수금. 표준 500, 경고 시 ×0.7, Day4는 0 (`07` §9) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Credit")
	int32 DailyAdvance = 500;

	/** 미상환 잔액 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Loan")
	int32 DebtBalance = 0;

	/** 호감도 기반 한도: 기본 800 / ≥+2 → 1200 / ≤−2 → 400 (`07` §5) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Loan")
	int32 LoanLimit = 800;

	/** 일 이자율. 0.05 = 5% (`07` §5) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Loan")
	float DailyInterestRate = 0.05f;

	/** 계약 진행 일차 (`04` §5 미션 카드: `3 Day Contract`) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Contract")
	int32 ContractDay = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Contract")
	int32 ContractTotalDays = 3;

	/** 활성 퀘스트 보수 미리보기. 실패 시 크레딧 위약금은 없다 (`07` §3.2, §8) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Quest")
	int32 ActiveQuestReward = 0;
};
