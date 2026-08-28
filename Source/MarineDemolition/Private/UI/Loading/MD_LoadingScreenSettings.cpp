#include "UI/Loading/MD_LoadingScreenSettings.h"

#include "Engine/Texture2D.h"

#define LOCTEXT_NAMESPACE "MD_LoadingScreen"

const UMD_LoadingScreenSettings* UMD_LoadingScreenSettings::Get()
{
	return GetDefault<UMD_LoadingScreenSettings>();
}

#if WITH_EDITOR
FText UMD_LoadingScreenSettings::GetSectionText() const
{
	return LOCTEXT("LoadingScreenSettingsSection", "MD Loading Screen");
}

FText UMD_LoadingScreenSettings::GetSectionDescription() const
{
	return LOCTEXT("LoadingScreenSettingsDescription", "맵 전환 로딩 화면이 사용하는 에셋");
}
#endif

UTexture2D* UMD_LoadingScreenSettings::GetBubbleTexture() const
{
	return BubbleTexture.LoadSynchronous();
}

UTexture2D* UMD_LoadingScreenSettings::GetShipTexture() const
{
	return ShipTexture.LoadSynchronous();
}

UTexture2D* UMD_LoadingScreenSettings::GetWaveTexture() const
{
	return WaveTexture.LoadSynchronous();
}

#undef LOCTEXT_NAMESPACE
