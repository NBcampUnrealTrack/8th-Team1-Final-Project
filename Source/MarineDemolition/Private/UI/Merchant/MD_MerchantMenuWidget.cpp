// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Merchant/MD_MerchantMenuWidget.h"

#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"

void UMD_MerchantMenuWidget::NativeOnActivated()
{
	Super::NativeOnActivated();

	RefreshDisplay();
}

void UMD_MerchantMenuWidget::SetOwningMerchant(AMD_Merchant* InMerchant)
{
	// 같은 소유 상인의 재설정에 따른 대화 초기화 방지
	if (OwningMerchant == InMerchant)
	{
		// 현재 대화 진행 상태 유지
		return;
	}
	// 현재 메뉴 요청을 처리할 소유 상인 저장
	OwningMerchant = InMerchant;
	// 다른 상인의 인사말부터 표시할 상태 초기화
	bHasDialogueLine = false;
	// 다른 상인의 대화 순서를 첫 줄로 초기화
	TalkLineIndex = INDEX_NONE;
}

void UMD_MerchantMenuWidget::SetMenuInfo(const FMD_MerchantMenuInfo& InInfo)
{
	// 수치 갱신과 구분할 상인 및 대사 구성 변경 여부
	bool bDialogueChanged = MenuInfo.DisplayName.EqualTo(InInfo.DisplayName) == false
		|| MenuInfo.Greeting.EqualTo(InInfo.Greeting) == false || MenuInfo.TalkLines.Num() != InInfo.TalkLines.Num();
	// 길이가 같은 대사 목록의 실제 내용 변경 검사
	for (int32 Index = 0; bDialogueChanged == false && Index < MenuInfo.TalkLines.Num(); ++Index)
	{
		// 같은 순서의 대사 비교로 불필요한 대화 초기화 방지
		bDialogueChanged = MenuInfo.TalkLines[Index].EqualTo(InInfo.TalkLines[Index]) == false;
	}
	// 최신 상인 이름과 호감도 및 대사 구성 저장
	MenuInfo = InInfo;
	// 대사 구성이 바뀐 경우에만 인사말부터 다시 시작
	if (bDialogueChanged == true)
	{
		// 새 목록의 첫 대화를 선택할 순번 초기화
		TalkLineIndex = INDEX_NONE;
		// 새 인사말 표시를 위한 초기화 상태 해제
		bHasDialogueLine = false;
	}
	// 대화 진행 상태를 보존하면서 최신 상인 정보 반영
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

	// 상인 또는 대사 구성이 바뀐 최초 표시에서만 인사말 적용
	if (bHasDialogueLine == false)
	{
		// 정보 갱신과 하위 화면 복귀가 현재 대사를 덮어쓰는 상황 방지
		ShowDialogueLine(MenuInfo.Greeting);
	}
}

void UMD_MerchantMenuWidget::CloseMenu()
{
	// Common UI Stack의 표준 비활성화 경로로 상인 메뉴를 닫는다.
	DeactivateWidget();
}

void UMD_MerchantMenuWidget::ShowDialogueLine(const FText& Line)
{
	// 같은 대사 재표시로 텍스트와 스크롤 위치가 초기화되는 상황 방지
	if (bHasDialogueLine == true && CurrentDialogueLine.EqualTo(Line) == true)
	{
		// 현재 읽기 위치 유지
		return;
	}
	// 빈 문자열을 포함한 현재 표시 대사 보관
	CurrentDialogueLine = Line;
	// 다음 정보 갱신에서 인사말을 다시 표시하지 않는 초기화 상태
	bHasDialogueLine = true;
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
