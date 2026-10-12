// Copyright Epic Games, Inc. All Rights Reserved.

#include "MarineDemolition.h"
#include "Modules/ModuleManager.h"

#if WITH_EDITOR
#include "PropertyEditorModule.h"
#include "UI/Foundation/Editor/MD_CarouselWidgetCustomization.h"
#include "UI/Foundation/Editor/MD_FilledSliderCustomization.h"
#include "World/Wall/Editor/MD_PowerConduitCustomization.h"
#endif

// 기존 게임 모듈 동작에 에디터 전용 캐러셀 디테일 등록 추가
class FMarineDemolitionModule : public FDefaultGameModuleImpl
{
public:
	// 기본 모듈 초기화 및 에디터 전용 디테일 등록
	virtual void StartupModule() override;
	// 에디터 전용 디테일 해제 및 기본 모듈 종료
	virtual void ShutdownModule() override;
};

void FMarineDemolitionModule::StartupModule()
{
	// 기본 게임 모듈 초기화 유지
	FDefaultGameModuleImpl::StartupModule();
// 에디터에서만 Enum 선택 UI 등록
#if WITH_EDITOR
	// 디테일 편집 모듈 준비
	FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
	// 캐러셀 타입의 전용 디테일 구성 등록
	PropertyEditor.RegisterCustomClassLayout(TEXT("MD_CarouselWidget"), FOnGetDetailCustomizationInstance::CreateStatic(&FMD_CarouselWidgetCustomization::MakeInstance));
	// 기존 Slider Style 내부의 채움 이미지 편집 행 등록
	PropertyEditor.RegisterCustomClassLayout(TEXT("MD_FilledSlider"), FOnGetDetailCustomizationInstance::CreateStatic(&FMD_FilledSliderCustomization::MakeInstance));
	// 배치 전선의 다중 액터 드롭 등록 영역 연결
	PropertyEditor.RegisterCustomClassLayout(TEXT("MD_PowerConduit"), FOnGetDetailCustomizationInstance::CreateStatic(&FMD_PowerConduitCustomization::MakeInstance));
#endif
}

void FMarineDemolitionModule::ShutdownModule()
{
// 모듈 종료 시 디테일 델리게이트 잔존 방지
#if WITH_EDITOR
	// 종료 중 모듈 재로드 방지
	bool bPropertyEditorLoaded = FModuleManager::Get().IsModuleLoaded(TEXT("PropertyEditor"));
	// 사용 가능한 디테일 모듈에 한정한 등록 해제
	if (bPropertyEditorLoaded == true)
	{
		// 로드된 디테일 모듈 조회
		FPropertyEditorModule& PropertyEditor = FModuleManager::GetModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
		// 캐러셀 디테일 등록 해제
		PropertyEditor.UnregisterCustomClassLayout(TEXT("MD_CarouselWidget"));
		// 채움 슬라이더 디테일 등록 해제
		PropertyEditor.UnregisterCustomClassLayout(TEXT("MD_FilledSlider"));
		// 전선 액터 드롭 디테일 등록 해제
		PropertyEditor.UnregisterCustomClassLayout(TEXT("MD_PowerConduit"));
	}
#endif
	// 기본 게임 모듈 종료 유지
	FDefaultGameModuleImpl::ShutdownModule();
}

// 캐러셀 에디터 확장을 포함한 기본 게임 모듈 등록
IMPLEMENT_PRIMARY_GAME_MODULE(FMarineDemolitionModule, MarineDemolition, "MarineDemolition");
