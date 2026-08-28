#include "UI/Screen/MD_PlayerInfoWidget.h"

#include "Components/Slider.h"
#include "Components/TextBlock.h"

void UMD_PlayerInfoWidget::NativePreConstruct()
{
	Super::NativePreConstruct();

	// 에디터 미리보기와 런타임 생성 전에 저장된 플레이어 이름을 반영한다.
	ApplyPlayerName();
}

void UMD_PlayerInfoWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 현재 저장된 플레이어 이름을 본문과 그림자에 함께 반영한다.
	ApplyPlayerName();

	// 바인딩된 슬라이더에 현재 볼륨과 값 변경 이벤트를 적용한다.
	if (IsValid(Sd_Volume) == true)
	{
		Sd_Volume->SetValue(Volume);
		Sd_Volume->OnValueChanged.AddUniqueDynamic(this, &UMD_PlayerInfoWidget::HandleVolumeChanged);
	}
}

void UMD_PlayerInfoWidget::NativeDestruct()
{
	// 제거된 위젯이 슬라이더 값 변경 이벤트를 받지 않도록 연결을 정리한다.
	if (IsValid(Sd_Volume) == true)
	{
		Sd_Volume->OnValueChanged.RemoveDynamic(this, &UMD_PlayerInfoWidget::HandleVolumeChanged);
	}

	Super::NativeDestruct();
}

void UMD_PlayerInfoWidget::SetPlayerName(FText InPlayerName)
{
	// 새 플레이어 이름을 이후 위젯 재생성에도 유지되도록 저장한다.
	PlayerName = InPlayerName;
	// 변경된 플레이어 이름을 본문과 그림자에 함께 반영한다.
	ApplyPlayerName();
}

void UMD_PlayerInfoWidget::SetVolume(float InVolume)
{
	// 슬라이더 범위 안에서 사용할 수 있도록 새 볼륨 값을 보정한다.
	Volume = FMath::Clamp(InVolume, 0.0f, 1.0f);

	// 바인딩된 슬라이더에 보정된 볼륨 값을 반영한다.
	if (IsValid(Sd_Volume) == true)
	{
		Sd_Volume->SetValue(Volume);
	}
}

void UMD_PlayerInfoWidget::ApplyPlayerName()
{
	// 플레이어 이름 본문이 존재하면 저장된 이름을 적용한다.
	if (IsValid(Txt_PlayerName) == true)
	{
		Txt_PlayerName->SetText(PlayerName);
	}

	// 플레이어 이름 그림자가 존재하면 본문과 같은 이름을 적용한다.
	if (IsValid(Txt_PlayerName_Shadow) == true)
	{
		Txt_PlayerName_Shadow->SetText(PlayerName);
	}
}

void UMD_PlayerInfoWidget::HandleVolumeChanged(float InVolume)
{
	// 슬라이더에서 전달된 값을 정규화된 범위로 보정하여 저장한다.
	Volume = FMath::Clamp(InVolume, 0.0f, 1.0f);
	// 변경된 볼륨을 실제 음성 처리와 연결할 수 있도록 외부에 알린다.
	OnVolumeChanged.Broadcast(Volume);
}
