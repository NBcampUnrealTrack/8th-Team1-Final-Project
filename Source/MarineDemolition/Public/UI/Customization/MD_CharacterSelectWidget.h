#pragma once

#include "CoreMinimal.h"
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
 * 선택은 로컬 미리보기로 즉시 반영되고, 확정해야 서버로 전송·로컬 저장된다.
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

	// 서버 전송·로컬 저장 후 닫는다
	UFUNCTION(BlueprintCallable, Category = "MD|Customization")
	void Confirm();

	// 미리보기를 되돌리고 닫는다. Back 액션도 같은 경로다
	UFUNCTION(BlueprintCallable, Category = "MD|Customization")
	void Cancel();

	UFUNCTION(BlueprintPure, Category = "MD|Customization")
	const FMD_CharacterAppearance& GetPendingAppearance() const { return Pending; }

protected:
	virtual void NativeOnActivated() override;
	virtual void NativeOnDeactivated() override;

	// 선택이 바뀔 때 BP 연출용
	UFUNCTION(BlueprintImplementableEvent, Category = "MD|Customization")
	void OnSelectionChanged(const FMD_CharacterAppearance& NewAppearance);

	// 캐릭터 버튼 클래스. 초상화가 있으면 이미지, 없으면 이름 텍스트를 넣는다
	UPROPERTY(EditDefaultsOnly, Category = "MD|Customization")
	TSubclassOf<UMD_CommonButtonBase> CharacterButtonClass;

	// 색 버튼 클래스. 이미지 틴트에 팔레트 색을 넣는다
	UPROPERTY(EditDefaultsOnly, Category = "MD|Customization")
	TSubclassOf<UMD_CommonButtonBase> ColorButtonClass;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UPanelWidget> Panel_Characters;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UPanelWidget> Panel_Colors;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_CharacterName;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UMD_CommonButtonBase> Btn_Confirm;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UMD_CommonButtonBase> Btn_Close;

private:
	AMD_Character* GetOwningCharacter() const;
	void BuildButtons();
	void ClearButtons();
	void RefreshSelection();
	void ApplyPreview();

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMD_CommonButtonBase>> CharacterButtons;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMD_CommonButtonBase>> ColorButtons;

	UPROPERTY(Transient)
	TObjectPtr<AMD_CustomizationConsole> OwningConsole;

	UPROPERTY(Transient)
	TObjectPtr<UMD_CharacterRosterData> Roster;

	FMD_CharacterAppearance Pending;
	bool bConfirmed = false;

	FDelegateHandle ConfirmClickedHandle;
	FDelegateHandle CloseClickedHandle;
};
