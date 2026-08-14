// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/Merchant/MD_LoreCardWidget.h"

#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Data/Lore/MD_LoreCatalog.h"

void UMD_LoreCardWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 내부 버튼 클릭 연결
	if (IsValid(Btn_Select) == true)
	{
		Btn_Select->OnClicked.AddUniqueDynamic(this, &UMD_LoreCardWidget::HandleCardClicked);
	}
}

void UMD_LoreCardWidget::SetLoreEntry(int32 InLoreIndex, const FText& EntryName, EMD_LoreEntryState EntryState)
{
	// 카드 선택 결과와 카탈로그 항목을 일치시키기 위한 인덱스 저장
	LoreIndex = InLoreIndex;

	// 한 자리 순서도 동일한 너비로 정렬하기 위한 두 자리 번호 표시
	if (IsValid(TXT_LoreNumber) == true)
	{
		TXT_LoreNumber->SetText(FText::FromString(FString::Printf(TEXT("%02d"), LoreIndex + 1)));
	}

	// 미발견 항목의 실제 이름 노출을 방지하기 위한 제목 표시
	if (IsValid(TXT_LoreTitle) == true)
	{
		FText DisplayName = EntryState == EMD_LoreEntryState::Undiscovered ? FText::FromString(TEXT("???")) : EntryName;
		TXT_LoreTitle->SetText(DisplayName);
	}

	// 발견과 해독 단계를 목록에서 구분하기 위한 상태 문구 결정
	if (IsValid(TXT_LoreState) == true)
	{
		FText StateText;
		switch (EntryState)
		{
		case EMD_LoreEntryState::Undiscovered:
			StateText = FText::FromString(TEXT("미발견"));
			break;
		case EMD_LoreEntryState::Encrypted:
			StateText = FText::FromString(TEXT("암호화됨"));
			break;
		case EMD_LoreEntryState::Decrypted:
			StateText = FText::FromString(TEXT("해독됨"));
			break;
		default:
			StateText = FText::GetEmpty();
			break;
		}

		TXT_LoreState->SetText(StateText);
	}
}

void UMD_LoreCardWidget::HandleCardClicked()
{
	// 데이터가 배정된 카드만 상세 선택 대상으로 전달하는 처리
	if (LoreIndex != INDEX_NONE)
	{
		OnLoreCardSelected.Broadcast(LoreIndex);
	}
}
