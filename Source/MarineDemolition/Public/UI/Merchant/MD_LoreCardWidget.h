// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"

#include "MD_LoreCardWidget.generated.h"

class UButton;
class UTextBlock;
enum class EMD_LoreEntryState : uint8;

// 카드 선택 결과를 로어 탭에 전달하기 위한 이벤트
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMD_LoreCardSelected, int32, LoreIndex);

// 로어 기록물 한 건의 번호와 제목 및 수집 상태를 표시하는 카드 클래스
UCLASS()
class MARINEDEMOLITION_API UMD_LoreCardWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// 내부 버튼 클릭 연결을 초기화하기 위한 함수
	virtual void NativeConstruct() override;

	// 카드 선택 시 목록 인덱스를 로어 탭에 전달하기 위한 이벤트
	UPROPERTY(BlueprintAssignable, Category = "MD|Lore")
	FMD_LoreCardSelected OnLoreCardSelected;

protected:
	// 내부 버튼이 배치된 카드에서만 선택 입력을 연결하기 위한 선택적 버튼 참조
	UPROPERTY(BlueprintReadOnly, Category = "MD|Lore", meta = (BindWidgetOptional, AllowPrivateAccess = "true"))
	TObjectPtr<UButton> Btn_Select;

	// 카탈로그 배열과 상세 화면 선택을 연결하기 위한 목록 인덱스
	UPROPERTY(BlueprintReadOnly, Category = "MD|Lore")
	int32 LoreIndex = INDEX_NONE;

	// 목록에서 기록물 순서를 식별하기 위한 번호 텍스트 참조
	UPROPERTY(BlueprintReadOnly, Category = "MD|Lore", meta = (BindWidget, AllowPrivateAccess = "true"))
	TObjectPtr<UTextBlock> TXT_LoreNumber;

	// 목록에서 기록물의 표시 이름을 제공하기 위한 제목 텍스트 참조
	UPROPERTY(BlueprintReadOnly, Category = "MD|Lore", meta = (BindWidget, AllowPrivateAccess = "true"))
	TObjectPtr<UTextBlock> TXT_LoreTitle;

	// 해독 여부와 발견 여부를 구분하기 위한 상태 텍스트 참조
	UPROPERTY(BlueprintReadOnly, Category = "MD|Lore", meta = (BindWidget, AllowPrivateAccess = "true"))
	TObjectPtr<UTextBlock> TXT_LoreState;

public:
	// 정적 정의와 공개 상태를 카드의 번호와 제목 및 상태 문구에 적용하는 함수
	void SetLoreEntry(int32 InLoreIndex, const FText& EntryName, EMD_LoreEntryState EntryState);

protected:
	// 내부 버튼 클릭을 목록 선택 이벤트로 변환하기 위한 콜백
	UFUNCTION()
	void HandleCardClicked();
};
