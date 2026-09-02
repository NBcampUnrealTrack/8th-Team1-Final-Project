#include "Gameplay/Settings/MD_SettingCategoryBase.h"

#include "Gameplay/Settings/MD_SettingsManager.h"

void UMD_SettingCategoryBase::InitializeCategory(UMD_SettingsManager* InSettingsManager)
{
	// 카테고리의 수명과 저장을 관리할 설정 매니저를 보관한다.
	SettingsManager = InSettingsManager;
	// 매니저 저장 이벤트가 발생하면 현재 카테고리 데이터를 등록하도록 연결한다.
	SettingsManager->OnSaveRequested.AddUObject(this, &UMD_SettingCategoryBase::HandleSaveRequested);
}

void UMD_SettingCategoryBase::HandleSaveRequested()
{
	// 저장 데이터를 받을 설정 매니저가 유효한지 확인한다.
	if (IsValid(SettingsManager) == false)
	{
		return;
	}
	// 현재 인스턴스의 카테고리 식별자를 가져온다.
	EMD_SettingsCategory Category = GetCategory();
	// 현재 카테고리 설정을 실제 구조체 타입과 함께 생성한다.
	FInstancedStruct SettingData = MakeSaveData();
	// 생성한 설정 데이터를 매니저의 카테고리 Map에 등록한다.
	SettingsManager->RegisterSettingData(Category, MoveTemp(SettingData));
}
