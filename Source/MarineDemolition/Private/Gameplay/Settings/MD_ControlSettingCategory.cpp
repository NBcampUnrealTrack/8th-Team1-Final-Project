#include "Gameplay/Settings/MD_ControlSettingCategory.h"

#include "EnhancedInputLibrary.h"
#include "InputAction.h"
#include "InputMappingContext.h"

void UMD_ControlSettingCategory::ApplySettings()
{
	// 무효 키와 잘못된 핫바 목록을 프로젝트 기본 입력으로 보정
	NormalizeSettings();
	// 마우스 감도 배율을 게임에서 지원하는 범위로 제한한다.
	Settings.MouseSensitivity = FMath::Clamp(Settings.MouseSensitivity, 0.1f, 10.0f);
	// 검증이 끝난 조작 설정을 실제 입력에서 사용할 값으로 반영한다.
	AppliedSettings = Settings;
	// 이미 플레이 중인 로컬 플레이어의 런타임 입력 매핑 갱신
	RefreshInputMappingContext();
}

void UMD_ControlSettingCategory::ResetToDefaults()
{
	// 조작 설정값을 구조체에 정의된 프로젝트 기본값으로 교체한다.
	Settings = FMD_ControlSettings();
}

FInstancedStruct UMD_ControlSettingCategory::MakeSaveData()
{
	// 현재 조작 설정과 실제 구조체 타입을 포함하는 InstancedStruct를 생성한다.
	FInstancedStruct InstancedSaveData = FInstancedStruct::Make(Settings);
	// 생성한 저장 데이터를 설정 매니저에 전달한다.
	return InstancedSaveData;
}

bool UMD_ControlSettingCategory::LoadSaveData(FInstancedStruct SaveData)
{
	// 저장 데이터가 조작 설정 형식인지 확인한다.
	bool bIsControlData = SaveData.GetScriptStruct() == FMD_ControlSettings::StaticStruct();
	// 다른 카테고리의 저장 데이터이면 복원을 중단한다.
	if (bIsControlData == false)
	{
		return false;
	}
	// 검증한 InstancedStruct에서 조작 설정 데이터를 읽는다.
	FMD_ControlSettings* ControlSaveData = SaveData.GetMutablePtr<FMD_ControlSettings>();
	// 저장 데이터 주소를 얻지 못하면 복원을 중단한다.
	if (ControlSaveData == nullptr)
	{
		return false;
	}
	// 저장된 조작 설정을 현재 값으로 복원한다.
	Settings = *ControlSaveData;
	// 조작 설정 복원이 완료되었음을 반환한다.
	return true;
}

EMD_SettingsCategory UMD_ControlSettingCategory::GetCategory()
{
	// 이 인스턴스가 조작 설정을 담당함을 직접 반환한다.
	return EMD_SettingsCategory::Controls;
}

UInputMappingContext* UMD_ControlSettingCategory::PrepareInputMappingContext(UInputMappingContext* InputMappingContext)
{
	// 원본 유지 또는 런타임 복제 성공에 따라 교체할 반환 컨텍스트
	UInputMappingContext* PreparedContext = InputMappingContext;
	// 플레이어 캐릭터에 설정된 원본 입력 컨텍스트 확인
	if (IsValid(InputMappingContext) == false)
	{
		return PreparedContext;
	}
	// 아직 런타임 복제본이 없는 상태 확인
	bool bNeedsRuntimeContext = IsValid(RuntimeMappingContext) == false;
	// 다른 원본 입력 컨텍스트가 전달된 경우의 복제본 교체 준비
	if (bNeedsRuntimeContext == false)
	{
		// 기존 복제본과 연결된 원본의 동일성 확인
		if (SourceMappingContext != InputMappingContext)
		{
			// 새로운 원본에 대응하는 복제본 생성 요청
			bNeedsRuntimeContext = true;
		}
	}
	// 최초 준비 또는 원본 교체에 한정한 런타임 복제본 생성
	if (bNeedsRuntimeContext == true)
	{
		// 현재 플레이어 캐릭터 입력 컨텍스트를 원본으로 보관
		SourceMappingContext = InputMappingContext;
		// 원본 에셋의 트리거와 모디파이어를 보존하는 런타임 복제본 생성
		RuntimeMappingContext = DuplicateObject<UInputMappingContext>(InputMappingContext, this);
	}
	// 복제에 실패한 경우 기존 원본 컨텍스트로 안전하게 대체
	if (IsValid(RuntimeMappingContext) == false)
	{
		// 기본 키를 유지하는 원본 반환
		return PreparedContext;
	}
	// 현재 적용 설정에 맞춘 런타임 복제본의 비이동 키 갱신
	RefreshInputMappingContext();
	// 사용자 선택 키가 반영된 컨텍스트로 반환값 교체
	PreparedContext = RuntimeMappingContext;
	// 플레이어 서브시스템에 등록할 런타임 복제본 반환
	return PreparedContext;
}

void UMD_ControlSettingCategory::NormalizeSettings()
{
	// 무효 설정의 대체 기준으로 사용할 프로젝트 기본 조작 설정
	FMD_ControlSettings DefaultSettings;
	// 현재 점프 키의 유효성 확인
	bool bJumpKeyValid = Settings.JumpKey.IsValid();
	// 점프 키가 비어 있는 경우의 기본 키 복원
	if (bJumpKeyValid == false)
	{
		Settings.JumpKey = DefaultSettings.JumpKey;
	}
	// 현재 상호작용 키의 유효성 확인
	bool bInteractKeyValid = Settings.InteractKey.IsValid();
	// 상호작용 키가 비어 있는 경우의 기본 키 복원
	if (bInteractKeyValid == false)
	{
		Settings.InteractKey = DefaultSettings.InteractKey;
	}
	// 현재 능력 키의 유효성 확인
	bool bAbilityKeyValid = Settings.AbilityKey.IsValid();
	// 능력 키가 비어 있는 경우의 기본 키 복원
	if (bAbilityKeyValid == false)
	{
		Settings.AbilityKey = DefaultSettings.AbilityKey;
	}
	// 현재 장비 교체 키의 유효성 확인
	bool bSwapEquipmentKeyValid = Settings.SwapEquipmentKey.IsValid();
	// 장비 교체 키가 비어 있는 경우의 기본 키 복원
	if (bSwapEquipmentKeyValid == false)
	{
		Settings.SwapEquipmentKey = DefaultSettings.SwapEquipmentKey;
	}
	// 현재 음성 채팅 키의 유효성 확인
	bool bVoiceChatKeyValid = Settings.VoiceChatKey.IsValid();
	// 음성 채팅 키가 비어 있는 경우의 기본 키 복원
	if (bVoiceChatKeyValid == false)
	{
		Settings.VoiceChatKey = DefaultSettings.VoiceChatKey;
	}
	// 프로젝트가 제공하는 핫바 슬롯 개수 조회
	int32 HotbarSlotCount = DefaultSettings.HotbarSlotKeys.Num();
	// 저장 배열의 누락 또는 초과 항목을 현재 슬롯 개수로 보정
	Settings.HotbarSlotKeys.SetNum(HotbarSlotCount);
	// 모든 핫바 슬롯 키의 유효성 순회
	for (int32 SlotIndex = 0; SlotIndex < HotbarSlotCount; ++SlotIndex)
	{
		// 현재 핫바 슬롯 키의 유효성 확인
		bool bHotbarKeyValid = Settings.HotbarSlotKeys[SlotIndex].IsValid();
		// 유효한 사용자 선택 키의 보존
		if (bHotbarKeyValid == true)
		{
			continue;
		}
		// 비어 있는 슬롯 키의 프로젝트 기본값 복원
		Settings.HotbarSlotKeys[SlotIndex] = DefaultSettings.HotbarSlotKeys[SlotIndex];
	}
}

void UMD_ControlSettingCategory::RefreshInputMappingContext()
{
	// 플레이어에게 등록할 런타임 복제본의 준비 상태 확인
	if (IsValid(RuntimeMappingContext) == false)
	{
		return;
	}
	// 원본 구조를 보존한 런타임 매핑 개수 조회
	int32 MappingCount = RuntimeMappingContext->GetMappings().Num();
	// 이동과 시점을 포함한 현재 입력 매핑 순회
	for (int32 MappingIndex = 0; MappingIndex < MappingCount; ++MappingIndex)
	{
		// 트리거와 모디파이어를 유지한 현재 매핑 참조
		FEnhancedActionKeyMapping& Mapping = RuntimeMappingContext->GetMapping(MappingIndex);
		// 현재 매핑이 참조하는 입력 액션 조회
		const UInputAction* InputAction = Mapping.Action.Get();
		// 액션이 없는 손상된 매핑의 기존 키 보존
		if (IsValid(InputAction) == false)
		{
			continue;
		}
		// 이동과 시점은 기존 키를 유지하고 나머지 액션만 설정 키로 변환
		FKey ConfiguredKey = GetConfiguredKey(InputAction->GetFName(), Mapping.Key);
		// 현재 액션의 런타임 선택 키 반영
		Mapping.Key = ConfiguredKey;
	}
	// 이미 등록된 런타임 컨텍스트의 키 변경을 로컬 입력에 즉시 반영
	UEnhancedInputLibrary::RequestRebuildControlMappingsUsingContext(RuntimeMappingContext, true);
}

FKey UMD_ControlSettingCategory::GetConfiguredKey(FName ActionName, FKey PreviousKey)
{
	// 등록 대상이 아닌 이동과 시점 액션에서 보존할 기존 키
	FKey ConfiguredKey = PreviousKey;
	// 점프 입력 액션의 사용자 선택 키 대응
	if (ActionName == TEXT("IA_Jump"))
	{
		ConfiguredKey = AppliedSettings.JumpKey;
	}
	// 상호작용 입력 액션의 사용자 선택 키 대응
	else if (ActionName == TEXT("IA_Interact"))
	{
		ConfiguredKey = AppliedSettings.InteractKey;
	}
	// 능력 입력 액션의 사용자 선택 키 대응
	else if (ActionName == TEXT("IA_Ability"))
	{
		ConfiguredKey = AppliedSettings.AbilityKey;
	}
	// 장비 교체 입력 액션의 사용자 선택 키 대응
	else if (ActionName == TEXT("IA_SwapEquipment"))
	{
		ConfiguredKey = AppliedSettings.SwapEquipmentKey;
	}
	// 변경 불가능한 일시정지 입력의 Esc 고정
	else if (ActionName == TEXT("IA_Pause"))
	{
		ConfiguredKey = EKeys::Escape;
	}
	// 음성 채팅 입력 액션의 사용자 선택 키 대응
	else if (ActionName == TEXT("IA_VoiceChat"))
	{
		ConfiguredKey = AppliedSettings.VoiceChatKey;
	}
	// 핫바 슬롯 입력 액션의 사용자 선택 키 탐색
	else
	{
		// 액션 이름에서 슬롯 번호를 분리할 접두사
		FString HotbarPrefix = TEXT("IA_HotbarSlot");
		// 현재 액션 이름의 문자열 표현
		FString ActionNameString = ActionName.ToString();
		// 핫바 액션이 아닌 경우의 기존 키 유지
		bool bIsHotbarAction = ActionNameString.StartsWith(HotbarPrefix);
		if (bIsHotbarAction == true)
		{
			// 접두사를 제외한 1부터 시작하는 슬롯 번호 문자열
			FString SlotNumberString = ActionNameString.RightChop(HotbarPrefix.Len());
			// 배열 인덱스로 사용할 0부터 시작하는 슬롯 번호 변환
			int32 SlotIndex = FCString::Atoi(*SlotNumberString) - 1;
			// 현재 설정이 보유한 핫바 슬롯 범위 확인
			bool bValidSlot = AppliedSettings.HotbarSlotKeys.IsValidIndex(SlotIndex);
			if (bValidSlot == true)
			{
				// 현재 핫바 슬롯에 지정된 사용자 키 대응
				ConfiguredKey = AppliedSettings.HotbarSlotKeys[SlotIndex];
			}
		}
	}
	// 현재 입력 액션에 대응하는 사용자 키 또는 기존 키 반환
	return ConfiguredKey;
}
