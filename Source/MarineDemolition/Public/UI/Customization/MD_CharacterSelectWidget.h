#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "UI/Foundation/MD_CommonGameMenuWidget.h"
#include "Types/Customization/MD_CharacterAppearanceTypes.h"
#include "MD_CharacterSelectWidget.generated.h"

class AMD_Character;
class AMD_CustomizationConsole;
class UMD_CharacterRosterData;
class UMD_CommonButtonBase;
class UCommonButtonGroupBase;
class UPanelWidget;
class UTextBlock;

/**
 * 거울 콘솔에서 열리는 캐릭터·메인 색 선택 화면.
 * 아래 줄에 로스터 캐릭터 버튼, 오른쪽에 팔레트 색 버튼을 로스터 개수만큼 생성한다.
 * 선택 즉시 적용·서버 전송·로컬 저장한다. 닫기/Back은 선택을 유지한다.
 */
UCLASS(Abstract, Blueprintable)
class MARINEDEMOLITION_API UMD_CharacterSelectWidget : public UMD_CommonGameMenuWidget
{
	GENERATED_BODY()

public:
	UMD_CharacterSelectWidget(const FObjectInitializer& ObjectInitializer);

	// BP가 제공한 목록과 보조 위젯 참조로 화면 구성
	UFUNCTION(BlueprintCallable, Category = "MD|Customization")
	void ConfigureLayout(UPanelWidget* InCharacterPanel, UPanelWidget* InColorPanel,
		UWidget* InCharacterListViewport, UTextBlock* InCharacterName);

	// 화면을 연 콘솔. 닫힐 때 카메라·메시 표시를 되돌리기 위해 필요하다
	void SetOwningConsole(AMD_CustomizationConsole* InConsole) { OwningConsole = InConsole; }

	UFUNCTION(BlueprintCallable, Category = "MD|Customization")
	void SelectCharacter(int32 CharacterIndex);

	UFUNCTION(BlueprintCallable, Category = "MD|Customization")
	void SelectColor(int32 ColorIndex);

	// 키와 하단 버튼의 누름 및 해제를 공유하는 연속 회전 요청
	UFUNCTION(BlueprintCallable, Category = "MD|Customization")
	void TurnCharacter(bool bTurnRight, bool bPressed = true);

	// 기존 Blueprint 호출 호환용. 현재 선택을 저장하고 닫는다.
	UFUNCTION(BlueprintCallable, Category = "MD|Customization")
	void Confirm();

	// 선택을 유지하고 닫는다. Back 액션도 같은 경로다.
	UFUNCTION(BlueprintCallable, Category = "MD|Customization")
	void Cancel();

	UFUNCTION(BlueprintPure, Category = "MD|Customization")
	const FMD_CharacterAppearance& GetPendingAppearance() const { return Pending; }

protected:
	virtual void NativeOnActivated() override;
	virtual void NativeOnDeactivated() override;
	virtual void NativeDestruct() override;
	// 활성화 시 키보드 포커스가 이 위젯 안에 있어야 Q/E가 들어온다
	virtual UWidget* NativeGetDesiredFocusTarget() const override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply NativeOnKeyUp(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual void NativeOnRemovedFromFocusPath(const FFocusEvent& InFocusEvent) override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// 선택이 바뀔 때 BP 연출용
	UFUNCTION(BlueprintImplementableEvent, Category = "MD|Customization")
	void OnSelectionChanged(const FMD_CharacterAppearance& NewAppearance);

	// 기존 팔레트 BP의 표시 변수와 위젯을 로스터 값으로 초기화
	UFUNCTION(BlueprintImplementableEvent, Category = "MD|Customization")
	void InitializeColorPalette(UUserWidget* PaletteWidget, const FText& ColorName, FLinearColor PreviewColor);

	// 활성화 시 BP에서 실제 목록 위젯 참조 연결
	UFUNCTION(BlueprintImplementableEvent, Category = "MD|Customization")
	void InitializeLayout();

	// 팔레트 BP가 사용하는 실제 선택 버튼 참조 반환
	UFUNCTION(BlueprintImplementableEvent, Category = "MD|Customization")
	UMD_CommonButtonBase* GetPaletteButton(UUserWidget* PaletteWidget);

	// 포커스 이탈과 화면 종료 시 모든 회전 입력 해제
	void StopCharacterTurn();

	// 캐릭터 버튼 클래스. 초상화가 있으면 이미지, 없으면 이름 텍스트를 넣는다
	UPROPERTY(EditDefaultsOnly, Category = "MD|Customization")
	TSubclassOf<UMD_CommonButtonBase> CharacterButtonClass;

	// 색 버튼 클래스. 이미지 틴트에 팔레트 색을 넣는다
	UPROPERTY(EditDefaultsOnly, Category = "MD|Customization")
	TSubclassOf<UMD_CommonButtonBase> ColorButtonClass;

	// 이름과 색상 버튼을 포함하는 새 팔레트 항목 클래스
	UPROPERTY(EditDefaultsOnly, Category = "MD|Customization")
	TSubclassOf<UUserWidget> ColorPaletteClass;

	// 캐릭터 카드 사이의 디자이너 지정 간격
	UPROPERTY(EditDefaultsOnly, Category = "MD|Customization|Layout")
	FMargin CharacterButtonPadding = FMargin(6.f, 0.f);

	// 가로 목록에서 캐릭터 카드의 세로 정렬 기준
	UPROPERTY(EditDefaultsOnly, Category = "MD|Customization|Layout")
	TEnumAsByte<EVerticalAlignment> CharacterButtonVerticalAlignment = VAlign_Fill;

	// 초상화 유무에 따른 이름 표시를 C++에서 관리할지 여부
	UPROPERTY(EditDefaultsOnly, Category = "MD|Customization|Layout")
	bool bManageCharacterNameVisibility = true;

	// 그리드 패널에 사용할 팔레트 열 개수
	UPROPERTY(EditDefaultsOnly, Category = "MD|Customization|Layout", meta = (ClampMin = "1"))
	int32 PaletteColumnCount = 2;

	// 선택적으로 연결한 Canvas 목록 영역의 최대 너비
	UPROPERTY(EditDefaultsOnly, Category = "MD|Customization|Layout", meta = (ClampMin = "0"))
	float CharacterListMaxWidth = 640.f;

	// Canvas 목록 영역 계산에 사용할 카드별 점유 크기
	UPROPERTY(EditDefaultsOnly, Category = "MD|Customization|Layout")
	FVector2D CharacterListEntrySize = FVector2D(116.f, 116.f);

	// 누르고 있는 동안 캐릭터를 왼쪽으로 회전하는 키
	UPROPERTY(EditDefaultsOnly, Category = "MD|Customization|Input")
	FKey TurnLeftKey = EKeys::Q;

	UPROPERTY(EditDefaultsOnly, Category = "MD|Customization|Input")
	FKey TurnRightKey = EKeys::E;

	// 선택을 유지한 채 화면을 닫는 키. CommonUI 기본 Back 액션이 비어 있어도 ESC로 나갈 수 있게 직접 처리한다
	UPROPERTY(EditDefaultsOnly, Category = "MD|Customization|Input")
	FKey CloseKey = EKeys::Escape;

	// 키와 버튼을 동시에 눌렀을 때 유지할 왼쪽 입력 수
	int32 TurnLeftHoldCount = 0;

	// 키와 버튼을 동시에 눌렀을 때 유지할 오른쪽 입력 수
	int32 TurnRightHoldCount = 0;

	// 로스터 카드의 단일 선택과 선택 변경을 관리하는 그룹
	UPROPERTY(Transient)
	TObjectPtr<UCommonButtonGroupBase> CharacterButtonGroup;

	// 캐릭터 선택과 독립적으로 팔레트의 단일 선택을 관리하는 그룹
	UPROPERTY(Transient)
	TObjectPtr<UCommonButtonGroupBase> ColorButtonGroup;

	// BP에서 전달한 캐릭터 목록 참조
	UPROPERTY(Transient)
	TObjectPtr<UPanelWidget> CharacterPanel;

	// BP에서 전달한 팔레트 목록 참조
	UPROPERTY(Transient)
	TObjectPtr<UPanelWidget> ColorPanel;

	// 카드 개수에 따라 너비를 조절할 선택적 목록 영역
	UPROPERTY(Transient)
	TObjectPtr<UWidget> CharacterListViewport;

	// 현재 캐릭터 이름을 표시할 선택적 텍스트 참조
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> CharacterName;

private:
	AMD_Character* GetOwningCharacter() const;
	void BuildButtons();
	void ClearButtons();
	void RefreshSelection();
	void ApplyPreview();
	void CommitSelection();
	void RestoreGameplayHUD();
	// 포커스가 게임 뷰포트로 빠지거나 비면 위젯 안으로 되찾는다. 다른 UI가 가진 포커스는 건드리지 않는다
	void EnsureKeyboardFocus();

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMD_CommonButtonBase>> CharacterButtons;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMD_CommonButtonBase>> ColorButtons;

	UPROPERTY(Transient)
	TObjectPtr<AMD_CustomizationConsole> OwningConsole;

	UPROPERTY(Transient)
	TObjectPtr<UMD_CharacterRosterData> Roster;

	FMD_CharacterAppearance Pending;
	bool bGameplayHUDHidden = false;

};
