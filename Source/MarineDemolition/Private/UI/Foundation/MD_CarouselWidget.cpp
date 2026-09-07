#include "UI/Foundation/MD_CarouselWidget.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
// 저장 시 Enum 표시 문구와 실제 값의 패키징 보존
#include "UObject/ObjectSaveContext.h"

// 런타임 빌드에서 에디터 변경 처리 제외
#if WITH_EDITOR
void UMD_CarouselWidget::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	// 배열 요소 편집을 포함한 변경 프로퍼티 식별
	FName PropertyName = PropertyChangedEvent.GetMemberPropertyName();
	// 수동 선택 인덱스 변경 처리
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UMD_CarouselWidget, SelectedIndex))
	{
		// 입력 범위 보정 및 무이벤트 표시 갱신
		SetSelectedIndex(SelectedIndex);
	}
	// Enum 타입과 값 및 옵션 목록 변경 처리
	else
	{
		// 새 Enum 항목과 선택 상태의 동기화
		RebuildEnumOptions();
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

void UMD_CarouselWidget::PostEditUndo()
{
	Super::PostEditUndo();

	// 복원한 Enum 타입과 선택값의 표시 갱신
	RebuildEnumOptions();
}

void UMD_CarouselWidget::PreSave(FObjectPreSaveContext SaveContext)
{
	// 메타데이터 제거 전 표시 문구와 실제 값의 직렬화 목록 확정
	RebuildEnumOptions();

	Super::PreSave(SaveContext);
}
#endif

void UMD_CarouselWidget::NativePreConstruct()
{
	Super::NativePreConstruct();

#if WITH_EDITOR
	// 디자이너에서 Enum 에셋 수정과 이름 변경의 재반영
	RebuildEnumOptions();
#endif

	// 변경 이벤트 없는 에디터 기본값 보정 및 화면 초기화
	SetSelectedIndex(SelectedIndex);
}

void UMD_CarouselWidget::SetOptionsEnum(UEnum* InOptionsEnum)
{
	// 자동 목록을 구성할 Enum 타입 보관
	OptionsEnum = InOptionsEnum;
	// 타입 전환에 따른 목록과 선택 상태 갱신
	RebuildEnumOptions();
}

bool UMD_CarouselWidget::SetSelectedEnumValue(int64 InEnumValue, bool bBroadcastChange)
{
	// 실제 Enum 값에 대응하는 표시 인덱스 조회
	int32 OptionIndex = EnumValues.Find(InEnumValue);
	// 숨김 또는 미등록 값의 선택 차단
	bool bFound = OptionIndex != INDEX_NONE;
	// 유효한 Enum 항목에 한정한 선택 적용
	if (bFound == true)
	{
		// 순환 선택과 동일한 이벤트 경로 사용
		SetSelectedIndex(OptionIndex, bBroadcastChange);
	}
	// Enum 값 선택 성공 여부 반환
	return bFound;
}

void UMD_CarouselWidget::RebuildEnumOptions()
{
	// Enum 미지정 시 수동 목록 유지
	if (IsValid(OptionsEnum) == false)
	{
		// 기존 자동 목록의 존재 여부 확인
		int32 PreviousEnumCount = EnumValues.Num();
		// Enum 해제 시 이전 자동 목록 제거
		if (PreviousEnumCount > 0)
		{
			// 이전 Enum 표시 문구 제거
			Options.Reset();
		}
		// 이전 Enum 값 대응 제거
		EnumValues.Reset();
		// 수동 목록의 선택과 표시 갱신
		SetSelectedIndex(SelectedIndex);
		return;
	}

	// 새 Enum 타입 기준의 표시 목록 초기화
	Options.Reset();
	// 표시 순서에 대응할 실제 값 목록 초기화
	EnumValues.Reset();
	// 선언된 Enum 항목 수 확인
	int32 EnumCount = OptionsEnum->NumEnums();
	// 선언 순서에 따른 옵션 구성
	for (int32 EnumIndex = 0; EnumIndex < EnumCount; ++EnumIndex)
	{
// 에디터 또는 메타데이터 지원 빌드의 숨김 항목 검사
#if WITH_METADATA
		// 숨김 지정 항목 제외
		if (OptionsEnum->HasMetaData(TEXT("Hidden"), EnumIndex) == true)
		{
			continue;
		}
		// 구분용 항목 제외
		if (OptionsEnum->HasMetaData(TEXT("Spacer"), EnumIndex) == true)
		{
			continue;
		}
#endif
		// 종료용 항목 판별에 사용할 원본 이름 조회
		FString EntryName = OptionsEnum->GetNameStringByIndex(EnumIndex);
		// 명시적 종료 항목 제외
		if (EntryName == TEXT("MAX"))
		{
			continue;
		}
		// 엔진 자동 생성 종료 항목 제외
		if (EntryName.EndsWith(TEXT("_MAX")) == true)
		{
			continue;
		}
		// Enum 표시 이름과 현지화 정보 보관
		FText DisplayName = OptionsEnum->GetDisplayNameTextByIndex(EnumIndex);
		// 불연속 값과 음수 값을 유지할 실제 값 조회
		int64 EnumValue = OptionsEnum->GetValueByIndex(EnumIndex);
		// 화면에 사용할 표시 문구 등록
		Options.Add(DisplayName);
		// 동일 표시 인덱스의 실제 값 등록
		EnumValues.Add(EnumValue);
	}
	// 기존 실제 값 기준의 선택 복원
	int32 OptionIndex = EnumValues.Find(SelectedEnumValue);
	// 새 목록에 없는 값의 첫 항목 보정 및 무이벤트 표시 갱신
	SetSelectedIndex(OptionIndex);
}

void UMD_CarouselWidget::SetOptions(TArray<FText> InOptions, int32 InSelectedIndex)
{
	// Enum 자동 목록의 수동 덮어쓰기 차단
	if (IsValid(OptionsEnum) == true)
	{
		return;
	}
	// 옵션 목록의 소유권 이동을 통한 추가 복사 방지
	Options = MoveTemp(InOptions);

	// 새 목록 크기에 맞춘 초기 선택 및 표시의 무이벤트 갱신
	SetSelectedIndex(InSelectedIndex);
}

int32 UMD_CarouselWidget::AddOption(FText InOption)
{
	// Enum 자동 목록의 수동 항목 추가 차단
	if (IsValid(OptionsEnum) == true)
	{
		// 자동 목록 추가 실패값 준비
		int32 InvalidIndex = INDEX_NONE;
		// 지원하지 않는 수동 추가 결과 반환
		return InvalidIndex;
	}
	// 중복 문구도 별도 인덱스로 관리하는 목록 끝 추가
	int32 AddedIndex = Options.Add(MoveTemp(InOption));

	// 기존 선택 유지 및 최초 옵션 추가 시 첫 항목 선택
	SetSelectedIndex(SelectedIndex);

	// 외부 인덱스 기반 선택에 사용할 등록 위치 반환
	return AddedIndex;
}

bool UMD_CarouselWidget::RemoveOption(FText InOption)
{
	// 같은 문구가 여러 개일 때 첫 일치 항목의 제거 위치 조회
	int32 OptionIndex = FindOptionIndex(InOption);

	// 미등록 문구의 실패 처리와 인덱스 제거 경로 공유
	bool bRemoved = RemoveOptionAtIndex(OptionIndex);

	// 외부 목록 관리에 사용할 제거 성공 여부 반환
	return bRemoved;
}

bool UMD_CarouselWidget::RemoveOptionAtIndex(int32 InOptionIndex)
{
	// Enum 값 대응을 손상시키는 수동 항목 제거 차단
	if (IsValid(OptionsEnum) == true)
	{
		// 자동 목록 제거 실패값 준비
		bool bRemoved = false;
		// 지원하지 않는 수동 제거 결과 반환
		return bRemoved;
	}
	// 배열 범위 밖 제거 요청의 상태 변경 방지
	bool bValidIndex = Options.IsValidIndex(InOptionIndex);
	if (bValidIndex == false)
	{
		// 잘못된 인덱스의 제거 실패 반환
		return bValidIndex;
	}

	// 제거 후에도 유지할 선택 항목의 위치 계산
	int32 NextSelectedIndex = SelectedIndex;
	// 선택 항목 앞의 제거로 인한 인덱스 이동 보정
	if (InOptionIndex < SelectedIndex)
	{
		// 기존 선택 항목을 가리키는 새 위치 적용
		--NextSelectedIndex;
	}

	// 지정 항목만 제거하고 나머지 옵션 순서 유지
	Options.RemoveAt(InOptionIndex);

	// 선택 항목 제거 시 다음 항목 선택 및 마지막 항목 제거 시 이전 항목 선택
	SetSelectedIndex(NextSelectedIndex);

	// 유효한 인덱스의 제거 성공 반환
	return bValidIndex;
}

void UMD_CarouselWidget::ClearOptions()
{
	// 자동 목록 초기화는 Enum 타입 해제로 제한
	if (IsValid(OptionsEnum) == true)
	{
		return;
	}
	// 외부 목록 재등록을 위한 전체 옵션 제거
	Options.Reset();

	// 빈 선택 문구와 양쪽 이동 버튼 비활성 상태 반영
	SetSelectedIndex(INDEX_NONE);
}

int32 UMD_CarouselWidget::FindOptionIndex(FText InOption)
{
	// 미등록 문구에 사용할 기본 검색 결과 준비
	int32 FoundIndex = INDEX_NONE;
	// 현재 등록된 옵션의 검색 범위 확인
	int32 OptionCount = Options.Num();
	// 등록 순서에 따른 첫 일치 항목 검색
	for (int32 OptionIndex = 0; OptionIndex < OptionCount; ++OptionIndex)
	{
		// 표시 문구의 일치 여부 비교
		bool bMatchingOption = Options[OptionIndex].EqualTo(InOption);
		// 최초 일치 항목 발견 시 검색 종료
		if (bMatchingOption == true)
		{
			// 외부 선택과 제거에 사용할 검색 위치 보관
			FoundIndex = OptionIndex;
			// 중복 문구의 첫 항목 우선 처리
			break;
		}
	}

	// 일치 항목의 인덱스 또는 INDEX_NONE 반환
	return FoundIndex;
}

FText UMD_CarouselWidget::GetOptionAtIndex(int32 InOptionIndex)
{
	// 잘못된 인덱스에 사용할 빈 텍스트 반환값 준비
	FText Option = FText::GetEmpty();
	// 외부 인덱스 접근의 유효 범위 검사
	bool bValidIndex = Options.IsValidIndex(InOptionIndex);
	// 유효한 인덱스에 한정한 문구 조회
	if (bValidIndex == true)
	{
		// 등록 당시 표시 문구 보관
		Option = Options[InOptionIndex];
	}

	// 검증을 마친 옵션 문구 반환
	return Option;
}

bool UMD_CarouselWidget::SetSelectedOption(FText InOption, bool bBroadcastChange)
{
	// 외부 문구 입력에 대응하는 첫 등록 인덱스 조회
	int32 OptionIndex = FindOptionIndex(InOption);
	// 미등록 문구에 대한 선택 변경 차단
	bool bOptionFound = OptionIndex != INDEX_NONE;
	// 등록된 문구에 한정한 선택 적용
	if (bOptionFound == true)
	{
		// 기존 인덱스 선택과 변경 이벤트 정책 공유
		SetSelectedIndex(OptionIndex, bBroadcastChange);
	}

	// 외부 호출자가 확인할 문구 선택 성공 여부 반환
	return bOptionFound;
}

void UMD_CarouselWidget::SetSelectedIndex(int32 InSelectedIndex, bool bBroadcastChange)
{
	// 빈 목록의 선택 항목 부재 표시
	int32 NewIndex = INDEX_NONE;

	// 인덱스 범위 제한에 사용할 옵션 수 확인
	int32 OptionCount = Options.Num();
	if (OptionCount > 0)
	{
		// 잘못된 외부 설정 값으로 인한 배열 범위 이탈 방지
		NewIndex = FMath::Clamp(InSelectedIndex, 0, OptionCount - 1);
	}

	// 이전 선택과 동일한 값의 중복 변경 이벤트 방지
	bool bSelectionChanged = SelectedIndex != NewIndex;

	// 이벤트 수신자가 조회할 선택 상태의 선행 확정
	SelectedIndex = NewIndex;
	// 유효한 Enum 선택이 없는 상태의 기본값 설정
	SelectedEnumValue = 0;
	// 실제 값 조회에 사용할 인덱스 검사
	bool bValidEnumIndex = EnumValues.IsValidIndex(SelectedIndex);
	// 선택한 표시 항목의 실제 Enum 값 동기화
	if (bValidEnumIndex == true)
	{
		// 표시 순서와 독립적인 실제 값 보관
		SelectedEnumValue = EnumValues[SelectedIndex];
	}

	// 동일 인덱스의 옵션 문구 교체를 반영하는 표시 갱신
	RefreshDisplay();

	// 저장값 로딩 등 명시적 알림 요청이 없는 갱신의 이벤트 생략
	if (bBroadcastChange == false)
	{
		return;
	}

	// 경계 이동 실패 및 동일 옵션 재선택의 이벤트 생략
	if (bSelectionChanged == false)
	{
		return;
	}

	// 선택값과 문구 전달 및 외부 화면의 실제 설정 적용 처리
	FText SelectedOption = GetSelectedOption();
	// 외부 이벤트의 재진입 전 Enum 타입 보관
	UEnum* SelectedEnum = OptionsEnum;
	// 외부 이벤트의 재진입 전 실제 선택값 보관
	int64 EnumValue = SelectedEnumValue;
	OnSelectionChanged.Broadcast(SelectedIndex, SelectedOption);
	// 유효한 Enum 선택의 전용 이벤트 전달
	if (bValidEnumIndex == true)
	{
		// 실제 Enum 값과 표시 문구 전달
		OnEnumSelectionChanged.Broadcast(SelectedEnum, EnumValue, SelectedOption);
	}
}

FText UMD_CarouselWidget::GetSelectedOption()
{
	// 외부 인덱스 조회와 동일한 경계 검사로 현재 선택 문구 조회
	FText SelectedOption = GetOptionAtIndex(SelectedIndex);

	// 검증을 마친 선택 문구 반환
	return SelectedOption;
}

void UMD_CarouselWidget::SelectPreviousOption()
{
	// Blueprint 이전 버튼 입력의 공통 이동 경로 전달
	MoveSelection(false);
}

void UMD_CarouselWidget::SelectNextOption()
{
	// Blueprint 다음 버튼 입력의 공통 이동 경로 전달
	MoveSelection(true);
}

void UMD_CarouselWidget::SetWrapSelection(bool bInWrapSelection)
{
	// 외부에서 지정한 순환 이동 정책 보관
	bWrapSelection = bInWrapSelection;

	// 새 정책에 맞춘 현재 선택 및 양 끝 버튼 상태 갱신
	SetSelectedIndex(SelectedIndex);
}

void UMD_CarouselWidget::MoveSelection(bool bForward)
{
	// 빈 목록과 단일 항목의 이동 및 변경 이벤트 생략
	int32 OptionCount = Options.Num();
	if (OptionCount <= 1)
	{
		return;
	}

	// 잘못된 초기 인덱스를 고려한 안전한 이동 기준 계산
	int32 CurrentIndex = FMath::Clamp(SelectedIndex, 0, OptionCount - 1);

	// 사용자 요청 방향에 따른 다음 인덱스 계산
	int32 NextIndex = CurrentIndex + (bForward ? 1 : -1);

	// 순환 설정 활성화 시 목록 반대편으로 이동
	if (bWrapSelection == true)
	{
		// 첫 항목의 이전 이동을 마지막 항목으로 연결
		if (NextIndex < 0)
		{
			NextIndex = OptionCount - 1;
		}

		// 마지막 항목의 다음 이동을 첫 항목으로 연결
		if (NextIndex >= OptionCount)
		{
			NextIndex = 0;
		}
	}

	// 비순환 경계 제한 및 실제 변경된 사용자 선택의 이벤트 전달
	SetSelectedIndex(NextIndex, true);
}

void UMD_CarouselWidget::RefreshDisplay()
{
	// 디자이너 폰트와 정렬 설정을 유지한 선택 문구 갱신
	if (IsValid(Txt_Value) == true)
	{
		// 바인딩된 중앙 텍스트에 표시할 선택 문구 준비
		FText SelectedOption = GetSelectedOption();
		// 현재 선택 문구 또는 빈 목록의 빈 텍스트 표시
		Txt_Value->SetText(SelectedOption);
	}

	// 빈 목록과 단일 항목의 양쪽 이동 버튼 비활성화
	bool bCanSelectPrevious = false;
	// 각 버튼의 독립적인 경계 상태 계산
	bool bCanSelectNext = false;
	// 현재 표시 가능한 옵션 수 확인
	int32 OptionCount = Options.Num();
	if (OptionCount > 1)
	{
		// 순환 목록의 양방향 이동 허용
		if (bWrapSelection == true)
		{
			// 첫 항목에서 마지막 항목으로 이전 이동 허용
			bCanSelectPrevious = true;
			// 마지막 항목에서 첫 항목으로 다음 이동 허용
			bCanSelectNext = true;
		}
		// 비순환 목록의 인접 항목 존재 여부에 따른 이동 제한
		else
		{
			// 첫 항목의 이전 이동 차단
			bCanSelectPrevious = SelectedIndex > 0;
			// 마지막 항목의 다음 이동 차단
			bCanSelectNext = SelectedIndex < OptionCount - 1;
		}
	}

	// 이전 버튼이 없는 레이아웃 허용
	if (IsValid(Btn_Previous) == true)
	{
		// 현재 목록과 선택 상태에 따른 이전 버튼 입력 가능 여부 반영
		Btn_Previous->SetIsEnabled(bCanSelectPrevious);
	}

	// 다음 버튼이 없는 레이아웃 허용
	if (IsValid(Btn_Next) == true)
	{
		// 현재 목록과 선택 상태에 따른 다음 버튼 입력 가능 여부 반영
		Btn_Next->SetIsEnabled(bCanSelectNext);
	}
}
