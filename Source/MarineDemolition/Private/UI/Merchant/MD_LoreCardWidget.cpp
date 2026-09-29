// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/Merchant/MD_LoreCardWidget.h"
#include "Components/TextBlock.h"

void UMD_LoreCardWidget::SetLoreEntry(int32 InLoreIndex, FText EntryName, bool bIsAcquired)
{
	// 번호 위젯이 있는 구성에서만 표시 문자열 계산
	if (IsValid(Txt_Index) == true)
	{
		// 카탈로그 순서를 최소 두 자리 번호로 표시
		FString IndexString = FString::Printf(TEXT("%02d"), InLoreIndex + 1);
		// 동일 번호의 FText 생성과 표시 갱신 생략
		if (Txt_Index->GetText().ToString() != IndexString)
		{
			// 변경된 번호만 현재 위젯에 반영
			Txt_Index->SetText(FText::FromString(IndexString));
		}
	}

	// 미획득 기록물의 제목을 숨기면서 획득한 제목의 지역화 정보 유지
	FText DisplayName = bIsAcquired == true ? EntryName : NSLOCTEXT("MDLoreCard", "UnknownTitle", "미발견 기록");
	// 저장된 제목이나 실제 표시가 달라진 경우만 공통 버튼 갱신
	if (GetContentText().IdenticalTo(DisplayName) == false
		|| (IsValid(Txt_ButtonText) == true && Txt_ButtonText->GetText().IdenticalTo(DisplayName) == false))
	{
		// 재구성에 사용할 제목과 현재 표시를 함께 동기화
		SetContentText(DisplayName);
	}

	// 상태 문구와 완료 표시 및 잠금 이미지에 현재 획득 여부 전달
	SetLoreIsFound(bIsAcquired);
}
