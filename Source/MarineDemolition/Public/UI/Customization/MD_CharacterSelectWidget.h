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

	// 화면을 연 콘솔. 닫힐 때 카메라·메시 표시를 되돌리기 위해 필요하다
	void SetOwningConsole(AMD_CustomizationConsole* InConsole) { OwningConsole = InConsole; }

	UFUNCTION(BlueprintCallable, Category = "MD|Customization")
	void SelectCharacter(int32 CharacterIndex);

	UFUNCTION(BlueprintCallable, Category = "MD|Customization")
	void SelectColor(int32 ColorIndex);

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
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// 선택이 바뀔 때 BP 연출용
	UFUNCTION(BlueprintImplementableEvent, Category = "MD|Customization")
	void OnSelectionChanged(const FMD_CharacterAppearance& NewAppearance);

	// 캐릭터 버튼 클래스. 초상화가 있으면 이미지, 없으면 이름 텍스트를 넣는다
	UPROPERTY(EditDefaultsOnly, Category = "MD|Customization")
	TSubclassOf<UMD_CommonButtonBase> CharacterButtonClass;

	// 색 버튼 클래스. 이미지 틴트에 팔레트 색을 넣는다
	UPROPERTY(EditDefaultsOnly, Category = "MD|Customization")
	TSubclassOf<UMD_CommonButtonBase> ColorButtonClass;

	// 캐릭터를 왼쪽/오른쪽으로 돌려 거울 방향과 카메라 방향을 오가는 키
	UPROPERTY(EditDefaultsOnly, Category = "MD|Customization|Input")
	FKey TurnLeftKey = EKeys::Q;

	UPROPERTY(EditDefaultsOnly, Category = "MD|Customization|Input")
	FKey TurnRightKey = EKeys::E;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UPanelWidget> Panel_Characters;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UPanelWidget> Panel_Colors;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_CharacterName;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UMD_CommonButtonBase> Btn_Close;

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

	FDelegateHandle CloseClickedHandle;
};
