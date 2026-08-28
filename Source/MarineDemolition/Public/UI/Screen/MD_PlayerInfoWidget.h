#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MD_PlayerInfoWidget.generated.h"

class APlayerState;
class UHorizontalBox;
class UImage;
class UMD_CharacterVOIPTalker;
class UOverlay;
class USlider;
class UTextBlock;
class UVerticalBox;

// 플레이어별 음성 볼륨 변경을 외부 화면에 전달하는 이벤트
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMD_OnPlayerInfoVolumeChanged, float, Volume);

/** 일시 정지 화면에서 플레이어 이름과 개별 음성 볼륨을 표시하는 위젯 */
UCLASS()
class MARINEDEMOLITION_API UMD_PlayerInfoWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativePreConstruct() override;

	virtual void NativeConstruct() override;

	virtual void NativeDestruct() override;

protected:
	// 플레이어 정보 카드의 전체 요소를 겹쳐 배치하는 최상위 영역
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UOverlay> Overlay_Root;

	// 플레이어 정보 카드의 헤더와 볼륨 조절부를 세로로 배치하는 영역
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UVerticalBox> VB_Root;

	// 호스트 상태, 플레이어 이름과 음성 아이콘을 가로로 배치하는 영역
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UHorizontalBox> HB_Header;

	// 호스트 왕관 또는 강퇴 상태 이미지
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UImage> Img_HostorKick;

	// 플레이어 이름 본문과 그림자를 겹쳐 표시하는 영역
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UOverlay> Ov_PlayerName;

	// 플레이어 이름 본문
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_PlayerName;

	// 플레이어 이름의 외곽 그림자
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_PlayerName_Shadow;

	// 현재 음성 출력 상태 아이콘 영역
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UHorizontalBox> HB_Icon_Volume;

	// 음성 출력 아이콘
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UImage> Img_Icon_Volume;

	// 음소거 상태 아이콘
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UImage> Img_Icon_Mute;

	// 음성 볼륨 이미지와 음소거 이미지를 겹쳐 표시하는 영역
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UOverlay> Overlay;

	// 음성 아이콘 영역의 간격 이미지
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UImage> Img_Spacer;

	// 음성 크기 단계 이미지를 나열하는 영역
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UHorizontalBox> HB_VoumeIndicator;

	// 첫 번째 음성 크기 단계
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UImage> Img_Icon_VolumeLine_1;

	// 두 번째 음성 크기 단계
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UImage> Img_Icon_VolumeLine_2;

	// 세 번째 음성 크기 단계
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UImage> Img_Icon_VolumeLine_3;

	// 음성 볼륨 Slider를 카드 하단에 배치하는 영역
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UHorizontalBox> HB_Footer;

	// 개별 플레이어의 음성 출력 크기 조절
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<USlider> Sd_Volume;

	// 카드에 할당된 음성 채팅 대상 플레이어
	UPROPERTY(BlueprintReadOnly, Transient, Category = "MD|Player Info")
	TObjectPtr<APlayerState> AssignedPlayerState;

	// 본문과 그림자에 함께 표시할 플레이어 이름
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Player Info")
	FText PlayerName;

	// Slider와 음성 처리에 전달할 정규화된 볼륨 값
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Player Info", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Volume = 1.0f;

	// Slider에서 변경된 볼륨 값을 외부 플레이어 목록에 전달하는 이벤트
	UPROPERTY(BlueprintAssignable, Category = "MD|Player Info")
	FMD_OnPlayerInfoVolumeChanged OnVolumeChanged;

public:
	// 현재 플레이어 이름 반환
	UFUNCTION(BlueprintPure, Category = "MD|Player Info")
	FORCEINLINE FText GetPlayerName() { return PlayerName; }

	// 현재 정규화된 음성 볼륨 반환
	UFUNCTION(BlueprintPure, Category = "MD|Player Info")
	FORCEINLINE float GetVolume() { return Volume; }

	// 음성 채팅과 이름 표시에 사용할 플레이어 할당
	UFUNCTION(BlueprintCallable, Category = "MD|Player Info")
	void SetPlayerState(APlayerState* InPlayerState);

	// 플레이어 이름의 본문과 그림자 동시 적용
	UFUNCTION(BlueprintCallable, Category = "MD|Player Info")
	void SetPlayerName(FText InPlayerName);

	// 정규화된 음성 볼륨의 저장과 Slider 및 실제 음성 재생 적용
	UFUNCTION(BlueprintCallable, Category = "MD|Player Info")
	void SetVolume(float InVolume);

protected:
	// 저장된 플레이어 이름의 바인딩된 텍스트 요소 적용
	void ApplyPlayerName();

	// 정규화된 볼륨의 할당된 플레이어 음성 Talker 적용
	void ApplyAssignedVoiceVolume();

	// 할당된 PlayerState에 등록된 프로젝트 음성 Talker 조회
	UMD_CharacterVOIPTalker* GetAssignedVoiceTalker();

	// 사용자가 Slider를 조작한 볼륨 값 저장과 외부 알림
	UFUNCTION()
	void HandleVolumeChanged(float InVolume);
};
