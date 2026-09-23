// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Merchant/MD_MerchantMenuWidget.h"

#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"

void UMD_MerchantMenuWidget::NativeOnActivated()
{
	Super::NativeOnActivated();

	RefreshDisplay();
}

void UMD_MerchantMenuWidget::SetMenuInfo(const FMD_MerchantMenuInfo& InInfo)
{
	MenuInfo = InInfo;

	// 대사 목록이 갈렸을 수 있으니 재생 위치를 처음으로 되돌린다.
	TalkLineIndex = INDEX_NONE;

	RefreshDisplay();
}

void UMD_MerchantMenuWidget::RefreshDisplay()
{
	// 새 레이아웃의 상인 이름 영역 유효성 확인
	if (IsValid(Txt_NpcName) == true)
	{
		// 초상화 아래 이름에 현재 상인 정보 반영
		Txt_NpcName->SetText(MenuInfo.DisplayName);
	}

	// 별도 라벨 옆 호감도 수치 영역 유효성 확인
	if (IsValid(Txt_Affinity) == true)
	{
		// 호감도 라벨을 중복하지 않고 부호와 소수점 한 자리 수치 표시
		Txt_Affinity->SetText(FText::FromString(FString::Printf(TEXT("%+.1f"), MenuInfo.Affinity)));
	}

	// 대사창은 인사말로 시작한다. '대화'를 누르면 TalkLines로 넘어간다.
	ShowDialogueLine(MenuInfo.Greeting);
}

void UMD_MerchantMenuWidget::CloseMenu()
{
	// Common UI Stack의 표준 비활성화 경로로 상인 메뉴를 닫는다.
	DeactivateWidget();
}

void UMD_MerchantMenuWidget::ShowDialogueLine(const FText& Line)
{
	// 현재 대사를 표시할 본문 영역 유효성 확인
	if (IsValid(Txt_Dialogue) == true)
	{
		// 별도 상인 이름 영역과 중복되지 않도록 대사 본문만 표시
		Txt_Dialogue->SetText(Line);
	}

	// 새 레이아웃의 대사 스크롤 영역 유효성 확인
	if (IsValid(SB_Dialogue) == true)
	{
		// 이전 대사의 스크롤 위치가 다음 대사의 시작을 가리지 않도록 초기화
		SB_Dialogue->SetScrollOffset(0.f);
	}
}

void UMD_MerchantMenuWidget::HandleTalkClicked()
{
	// 누를 때마다 다음 줄로 넘기고 끝에서 처음으로 돈다.
	// 대사가 비어 있으면 인사말을 다시 띄운다 — 빈 대사창이 뜨는 것보다 낫다.
	if (MenuInfo.TalkLines.Num() > 0)
	{
		TalkLineIndex = (TalkLineIndex + 1) % MenuInfo.TalkLines.Num();
		ShowDialogueLine(MenuInfo.TalkLines[TalkLineIndex]);
	}
	else
	{
		ShowDialogueLine(MenuInfo.Greeting);
	}

	OnTalkRequested.Broadcast();
}

void UMD_MerchantMenuWidget::HandleExitClicked()
{
	OnExitRequested.Broadcast();
	CloseMenu();
}
