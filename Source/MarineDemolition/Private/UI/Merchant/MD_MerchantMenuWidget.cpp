// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Merchant/MD_MerchantMenuWidget.h"

#include "Component/Merchant/MD_MerchantComponent.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Engine/World.h"
#include "Game/MD_GameStateBase.h"

void UMD_MerchantMenuWidget::NativeOnActivated()
{
	Super::NativeOnActivated();

	// 서버와 클라이언트가 함께 사용하는 현재 월드의 상인 복제 상태 조회
	UWorld* World = GetWorld();
	// 메뉴가 속한 월드의 팀 공유 원장 조회
	AMD_GameStateBase* GameState = IsValid(World) == true ? World->GetGameState<AMD_GameStateBase>() : nullptr;
	// 호감도 변경을 알리는 복제 컴포넌트 조회
	UMD_MerchantComponent* MerchantComponent = IsValid(GameState) == true ? GameState->GetMerchantComponent() : nullptr;
	// 재사용된 메뉴에 남아 있는 이전 원장 연결 조회
	UMD_MerchantComponent* PreviousMerchantComponent = BoundMerchantComponent.Get();
	// 원장 교체 시 이전 월드의 상태 알림 연결 해제
	if (PreviousMerchantComponent != MerchantComponent && IsValid(PreviousMerchantComponent) == true)
	{
		// 이전 컴포넌트가 현재 메뉴 정보를 덮어쓰는 상황 방지
		PreviousMerchantComponent->OnMerchantStateChanged.RemoveDynamic(this, &UMD_MerchantMenuWidget::HandleMerchantStateChanged);
	}
	// 메뉴가 실제로 구독하는 컴포넌트의 수명 추적
	BoundMerchantComponent = MerchantComponent;
	// 상인 복제 컴포넌트가 준비된 경우에만 변경 알림 연결
	if (IsValid(MerchantComponent) == true)
	{
		// 메뉴 재활성화 시 중복 없이 호감도와 상인 정보의 즉시 갱신 구독
		MerchantComponent->OnMerchantStateChanged.AddUniqueDynamic(this, &UMD_MerchantMenuWidget::HandleMerchantStateChanged);
	}
	// 다시 열린 메뉴에 현재 원장 상태를 즉시 반영
	HandleMerchantStateChanged();
}

void UMD_MerchantMenuWidget::NativeDestruct()
{
	// 메뉴 제거 전 실제로 구독했던 컴포넌트 조회
	UMD_MerchantComponent* MerchantComponent = BoundMerchantComponent.Get();
	// 제거된 메뉴로 상태 변경 알림이 전달되지 않도록 연결 해제
	if (IsValid(MerchantComponent) == true)
	{
		// 다른 상인 화면의 상태 구독을 유지하고 이 메뉴의 연결만 제거
		MerchantComponent->OnMerchantStateChanged.RemoveDynamic(this, &UMD_MerchantMenuWidget::HandleMerchantStateChanged);
	}
	// 재사용 시 새 원장을 연결할 수 있도록 이전 참조 정리
	BoundMerchantComponent.Reset();

	Super::NativeDestruct();
}

void UMD_MerchantMenuWidget::HandleMerchantStateChanged()
{
	// 현재 구독 중인 서버 또는 클라이언트 복제 상태 조회
	UMD_MerchantComponent* MerchantComponent = BoundMerchantComponent.Get();
	// 유효한 원장이 있으면 대사 유지 규칙을 적용하며 최신 정보 반영
	if (IsValid(MerchantComponent) == true)
	{
		// 호감도만 변경된 경우 현재 대사와 스크롤 위치 보존
		SetMenuInfo(MerchantComponent->BuildMenuInfo());
	}
	else
	{
		// 원장이 없는 미리보기 등에서는 직접 지정한 메뉴 정보 표시
		RefreshDisplay();
	}
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
