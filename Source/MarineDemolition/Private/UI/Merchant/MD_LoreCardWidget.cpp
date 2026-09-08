// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/Merchant/MD_LoreCardWidget.h"
#include "Components/TextBlock.h"

void UMD_LoreCardWidget::SetLoreEntry(int32 InLoreIndex, FText EntryName, bool bIsAcquired)
{
	// 번호 표시 대상의 유효성 확인
	if (IsValid(TXT_LoreNumber) == true)
	{
		// 카드 번호 갱신
		TXT_LoreNumber->SetText(FText::FromString(FString::Printf(TEXT("%02d"), InLoreIndex + 1)));
	}

	// 제목 표시 대상의 유효성 확인
	if (IsValid(TXT_LoreTitle) == true)
	{
		// 획득한 기록물의 제목을 기본 표시값으로 사용
		FText DisplayName = EntryName;
		// 미획득 기록물의 실제 제목 노출 방지
		if (bIsAcquired == false)
		{
			// 미획득 제목 대체 문구 적용
			DisplayName = FText::FromString(TEXT("???"));
		}
		// 획득 여부에 따른 제목 갱신
		TXT_LoreTitle->SetText(DisplayName);
	}

	// 상태 표시 대상의 유효성 확인
	if (IsValid(TXT_LoreState) == true)
	{
		// 카드 획득 상태 갱신
		TXT_LoreState->SetText((bIsAcquired == true ? FText::FromString(TEXT("획득")) : FText::FromString(TEXT("미획득"))));
		// 현재 획득 여부에 대응하는 에디터 지정 색상 선택
		FLinearColor StateTextColor = bIsAcquired == true ? AcquiredStateTextColor : UnacquiredStateTextColor;
		// 상태 문구와 동일한 획득 기준으로 색상 갱신
		TXT_LoreState->SetColorAndOpacity(FSlateColor(StateTextColor));
	}
}
