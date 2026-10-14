#include "Gameplay/Interaction/MD_InteractionPresentationLibrary.h"

#include "Component/Interaction/MD_InteractionPromptComponent.h"
#include "Engine/DataTable.h"
#include "GameFramework/Actor.h"
#include "Gameplay/Interaction/MD_Interactable.h"
#include "Gameplay/Interaction/MD_InteractionSettings.h"
#include "Gameplay/Settings/MD_ControlSettingCategory.h"
#include "Gameplay/Settings/MD_SettingsManager.h"
#include "Types/Interaction/MD_InteractionTypes.h"

FName UMD_InteractionPresentationLibrary::MakeShortInteractionId(const UClass* TargetClass)
{
	if (!IsValid(TargetClass))
	{
		return NAME_None;
	}

	FString ClassName = TargetClass->GetName();
	ClassName.RemoveFromStart(TEXT("MD_"));
	return FName(*ClassName);
}

namespace
{
const UDataTable* GetValidatedInteractionPresentationTable()
{
	const UMD_InteractionSettings* Settings = UMD_InteractionSettings::Get();
	const UDataTable* Table = IsValid(Settings)
		? Settings->GetInteractionPresentationTable()
		: nullptr;
	return IsValid(Table)
		&& IsValid(Table->GetRowStruct())
		&& Table->GetRowStruct()->IsChildOf(FMD_InteractionPresentationRow::StaticStruct())
		? Table : nullptr;
}

bool FindDisplayNameInValidatedTable(
	const UDataTable* Table, const FName InteractionId, FText& OutDisplayName,
	bool* bOutCanReuseTable = nullptr)
{
	if (InteractionId.IsNone())
	{
		return false;
	}

	static const FString Context(TEXT("MD_InteractionPresentation"));
	const FMD_InteractionPresentationRow* Row =
		Table->FindRow<FMD_InteractionPresentationRow>(InteractionId, Context, false);
	if (Row == nullptr)
	{
		if (bOutCanReuseTable != nullptr) *bOutCanReuseTable = true;
		return false;
	}
	// Reading text can rebuild a history and invoke external code. Revalidate
	// the table before another lookup whenever a row was read.
	if (Row->DisplayName.IsEmpty())
	{
		return false;
	}

	OutDisplayName = Row->DisplayName;
	return true;
}
}

bool UMD_InteractionPresentationLibrary::FindDisplayName(const FName InteractionId, FText& OutDisplayName)
{
	OutDisplayName = FText::GetEmpty();
	if (InteractionId.IsNone())
	{
		return false;
	}

	const UDataTable* Table = GetValidatedInteractionPresentationTable();
	return Table != nullptr && FindDisplayNameInValidatedTable(Table, InteractionId, OutDisplayName);
}

FText UMD_InteractionPresentationLibrary::FindActionPrompt(
	const FName InteractionId, UObject* WorldContextObject)
{
	if (InteractionId.IsNone()) return FText::GetEmpty();
	const UDataTable* Table = GetValidatedInteractionPresentationTable();
	if (Table == nullptr) return FText::GetEmpty();

	static const FString Context(TEXT("MD_InteractionPresentation"));
	const FMD_InteractionPresentationRow* Row =
		Table->FindRow<FMD_InteractionPresentationRow>(InteractionId, Context, false);
	if (Row == nullptr) return FText::GetEmpty();

	// Keep the text value rather than a row pointer while formatting can invoke external code.
	const FText ActionPrompt = Row->ActionPrompt;
	if (ActionPrompt.IsEmpty()) return FText::GetEmpty();

	// 유형 설정과 같은 경로를 사용하는 입력 키 결합
	return FormatActionPrompt(ActionPrompt, WorldContextObject);
}

FText UMD_InteractionPresentationLibrary::ResolveInteractionDisplayName(const UObject* Target)
{
	if (!IsValid(Target)
		|| !Target->GetClass()->ImplementsInterface(UMD_Interactable::StaticClass()))
	{
		return FText::GetEmpty();
	}

	const FText TargetOwnedName = IMD_Interactable::Execute_GetInteractionDisplayName(Target);
	if (!TargetOwnedName.IsEmpty())
	{
		return TargetOwnedName;
	}

	const UDataTable* Table = nullptr;
	bool bCanReuseTable = false;
	auto FindFallbackDisplayName = [&Table, &bCanReuseTable](const FName Id, FText& DisplayName)
	{
		DisplayName = FText::GetEmpty();
		if (Id.IsNone()) return false;
		if (!bCanReuseTable) Table = GetValidatedInteractionPresentationTable();
		bCanReuseTable = false;
		return Table != nullptr
			&& FindDisplayNameInValidatedTable(Table, Id, DisplayName, &bCanReuseTable);
	};

	for (const UClass* TargetClass = Target->GetClass(); IsValid(TargetClass); TargetClass = TargetClass->GetSuperClass())
	{
		FText DisplayName;
		if (FindFallbackDisplayName(TargetClass->GetFName(), DisplayName))
		{
			return DisplayName;
		}

		const FName ShortId = MakeShortInteractionId(TargetClass);
		if (ShortId != TargetClass->GetFName() && FindFallbackDisplayName(ShortId, DisplayName))
		{
			return DisplayName;
		}
	}

	return FText::GetEmpty();
}

FText UMD_InteractionPresentationLibrary::FormatActionPrompt(FText ActionPrompt, UObject* WorldContextObject)
{
	// 비표시 유형의 빈 문구에 입력 키만 남는 상황 방지
	if (ActionPrompt.IsEmpty() == true)
	{
		// 행동 문구가 없는 유형의 안내 생략
		return FText::GetEmpty();
	}
	// 설정 관리자 준비 전 사용할 기본 상호작용 키
	static FKey DefaultInteractionKey = FMD_ControlSettings().InteractKey;
	// 기본 설정 배열의 반복 생성을 피한 적용 키 초기화
	FKey InteractionKey = DefaultInteractionKey;
	// 현재 플레이어의 적용된 입력 설정 조회
	UMD_SettingsManager* Settings = UMD_SettingsManager::Get(WorldContextObject);
	// 실제 게임 설정이 준비된 경우에만 재지정 키 조회
	if (IsValid(Settings) == true)
	{
		// 아직 적용하지 않은 편집값과 구분할 컨트롤 설정 참조
		UMD_ControlSettingCategory* Controls = Settings->GetControlSettings();
		// 입력 카테고리 준비 여부 확인
		if (IsValid(Controls) == true)
		{
			// 사용자가 현재 적용한 상호작용 키 보관
			FKey AppliedKey = Controls->GetAppliedInteractionKey();
			// 유효한 재지정 키만 기본값에 우선 적용
			if (AppliedKey.IsValid() == true)
			{
				// 화면과 실제 입력의 키 일치
				InteractionKey = AppliedKey;
			}
		}
	}
	// 기존 HUD의 이름 및 키와 행동 분리 형식을 유지한 문구 반환
	return FText::Format(NSLOCTEXT("MDInteraction", "KeyActionPromptFormat", "[{0}] {1}"), InteractionKey.GetDisplayName(false), ActionPrompt);
}

FText UMD_InteractionPresentationLibrary::FindActionPromptByType(EMD_InteractionPromptType PromptType, UObject* WorldContextObject)
{
	// 공용 유형별 문구를 소유한 프로젝트 설정 조회
	const UMD_InteractionSettings* Settings = UMD_InteractionSettings::Get();
	// 설정 객체 준비 실패 시 안내 생략
	if (IsValid(Settings) == false)
	{
		// 잘못된 설정 참조 접근 방지
		return FText::GetEmpty();
	}
	// 유형별 문구에 실제 적용 키를 붙여 반환
	return FormatActionPrompt(Settings->GetPromptText(PromptType), WorldContextObject);
}

FText UMD_InteractionPresentationLibrary::FindActorActionPrompt(const AActor* Target, UObject* WorldContextObject)
{
	// 제거된 액터의 컴포넌트 조회 방지
	if (IsValid(Target) == false)
	{
		// 대상이 없는 경우의 안내 생략
		return FText::GetEmpty();
	}
	// 액터 또는 Blueprint에서 선택한 프롬프트 유형 조회
	UMD_InteractionPromptComponent* Prompt = Target->FindComponentByClass<UMD_InteractionPromptComponent>();
	// 명시적으로 프롬프트를 구성한 액터만 공용 문구 사용
	if (IsValid(Prompt) == false)
	{
		// 유형 컴포넌트가 없는 기존 액터의 기본 동작 유지
		return FText::GetEmpty();
	}
	// 컴포넌트에서 선택한 유형과 현재 입력 설정을 함께 적용
	return Prompt->GetActionPrompt(WorldContextObject);
}
