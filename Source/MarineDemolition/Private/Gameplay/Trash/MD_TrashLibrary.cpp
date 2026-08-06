// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Trash/MD_TrashLibrary.h"

#include "Engine/DataTable.h"
#include "Gameplay/Trash/MD_TrashSettings.h"
#include "GameplayTags/MD_GameplayTags.h"
#include "Util/MD_LogUtility.h"

namespace
{
	const FString TrashLookupContext(TEXT("MD_TrashLibrary"));

	/** 데이터 누락 신호용 색 */
	const FLinearColor MissingDataColor = FLinearColor(1.0f, 0.0f, 1.0f);

	/** 조회는 매 프레임 일어날 수 있으므로 같은 누락은 한 번만 경고한다. 게임 스레드 전용 */
	void WarnOnce(const FName WarnKey, const FString& Message)
	{
		static TSet<FName> AlreadyWarned;
		if (AlreadyWarned.Contains(WarnKey))
		{
			return;
		}

		AlreadyWarned.Add(WarnKey);
		UE_LOG(MarineDemolition, Warning, TEXT("%s"), *Message);
	}

	/** 행 캐스팅 전 행 구조체를 확인한다 */
	const UDataTable* GetValidatedTable(
		const UDataTable* Table,
		const UScriptStruct* ExpectedRowStruct,
		const FName WarnKey,
		const FString& TableLabel)
	{
		if (!Table)
		{
			WarnOnce(WarnKey, FString::Printf(
				TEXT("%s이 지정되지 않았다. Project Settings > Game > MD Trash에서 지정할 것."),
				*TableLabel));
			return nullptr;
		}

		const UScriptStruct* RowStruct = Table->GetRowStruct();
		if (!RowStruct || !RowStruct->IsChildOf(ExpectedRowStruct))
		{
			WarnOnce(WarnKey, FString::Printf(
				TEXT("%s '%s'의 행 구조체가 %s가 아니다."),
				*TableLabel,
				*Table->GetName(),
				*ExpectedRowStruct->GetName()));
			return nullptr;
		}

		return Table;
	}

	const UDataTable* GetTrashKindTable()
	{
		const UMD_TrashSettings* Settings = UMD_TrashSettings::Get();
		if (!Settings)
		{
			return nullptr;
		}

		return GetValidatedTable(
			Settings->GetTrashKindTable(),
			FMD_TrashKindRow::StaticStruct(),
			TEXT("MD Trash.TrashKindTable"),
			TEXT("산재 쓰레기 종류 테이블"));
	}

	const UDataTable* GetTrashCategoryTable()
	{
		const UMD_TrashSettings* Settings = UMD_TrashSettings::Get();
		if (!Settings)
		{
			return nullptr;
		}

		return GetValidatedTable(
			Settings->GetTrashCategoryTable(),
			FMD_TrashCategoryRow::StaticStruct(),
			TEXT("MD Trash.TrashCategoryTable"),
			TEXT("쓰레기 분류 칸 테이블"));
	}

	const FMD_TrashKindRow* FindTrashKindRowInternal(const FName TrashKindId)
	{
		// 종류를 아직 고르지 않은 배치는 정상적인 중간 상태라 경고하지 않는다
		if (TrashKindId.IsNone())
		{
			return nullptr;
		}

		const UDataTable* Table = GetTrashKindTable();
		if (!Table)
		{
			return nullptr;
		}

		const FMD_TrashKindRow* Row =
			Table->FindRow<FMD_TrashKindRow>(TrashKindId, TrashLookupContext, /*bWarnIfRowMissing*/ false);
		if (!Row)
		{
			WarnOnce(
				FName(*FString::Printf(TEXT("TrashKindRow:%s"), *TrashKindId.ToString())),
				FString::Printf(TEXT("종류 테이블에 '%s' 행이 없다."), *TrashKindId.ToString()));
		}

		return Row;
	}

	const FMD_TrashCategoryRow* FindTrashCategoryRowInternal(const FGameplayTag Category)
	{
		if (!Category.IsValid())
		{
			return nullptr;
		}

		const UDataTable* Table = GetTrashCategoryTable();
		if (!Table)
		{
			return nullptr;
		}

		// 행 이름 대신 Category 컬럼으로 찾는다. 다섯 행이라 선형 탐색으로 충분하다
		for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
		{
			const FMD_TrashCategoryRow* Row = reinterpret_cast<const FMD_TrashCategoryRow*>(Pair.Value);
			if (Row && Row->Category.MatchesTagExact(Category))
			{
				return Row;
			}
		}

		WarnOnce(
			FName(*FString::Printf(TEXT("TrashCategoryRow:%s"), *Category.ToString())),
			FString::Printf(TEXT("칸 테이블에 '%s' 행이 없다."), *Category.ToString()));

		return nullptr;
	}
}

TArray<FName> UMD_TrashLibrary::GetTrashKindIds()
{
	const UDataTable* Table = GetTrashKindTable();
	return Table ? Table->GetRowNames() : TArray<FName>();
}

bool UMD_TrashLibrary::FindTrashKindRow(const FName TrashKindId, FMD_TrashKindRow& OutRow)
{
	if (const FMD_TrashKindRow* Row = FindTrashKindRowInternal(TrashKindId))
	{
		OutRow = *Row;
		return true;
	}

	OutRow = FMD_TrashKindRow();
	return false;
}

FGameplayTag UMD_TrashLibrary::GetTrashKindCategory(const FName TrashKindId)
{
	const FMD_TrashKindRow* Row = FindTrashKindRowInternal(TrashKindId);
	return Row ? Row->Category : FGameplayTag();
}

int32 UMD_TrashLibrary::GetTrashKindRefundCredits(const FName TrashKindId)
{
	const FMD_TrashKindRow* Row = FindTrashKindRowInternal(TrashKindId);
	if (!Row)
	{
		return 0;
	}

	// 기획이 "칸 기본 + 품목 가산" 구조라 합산값을 행에 적지 않고 계산한다
	return GetCategoryRefundCredits(Row->Category) + Row->RefundBonusCredits;
}

FGameplayTag UMD_TrashLibrary::GetTrashKindScoreBonusTag(const FName TrashKindId)
{
	const FMD_TrashKindRow* Row = FindTrashKindRowInternal(TrashKindId);
	return Row ? Row->ScoreBonusTag : FGameplayTag();
}

bool UMD_TrashLibrary::FindTrashCategoryRow(const FGameplayTag Category, FMD_TrashCategoryRow& OutRow)
{
	if (const FMD_TrashCategoryRow* Row = FindTrashCategoryRowInternal(Category))
	{
		OutRow = *Row;
		return true;
	}

	OutRow = FMD_TrashCategoryRow();
	return false;
}

FText UMD_TrashLibrary::GetCategoryDisplayName(const FGameplayTag Category)
{
	const FMD_TrashCategoryRow* Row = FindTrashCategoryRowInternal(Category);

	// 행이 없으면 태그 이름을 보여준다. 읽을 수 있으면서 데이터 누락도 드러난다
	return Row ? Row->DisplayName : FText::FromString(Category.ToString());
}

FLinearColor UMD_TrashLibrary::GetCategoryColor(const FGameplayTag Category)
{
	const FMD_TrashCategoryRow* Row = FindTrashCategoryRowInternal(Category);
	return Row ? FLinearColor::FromSRGBColor(Row->Color) : MissingDataColor;
}

int32 UMD_TrashLibrary::GetCategoryRefundCredits(const FGameplayTag Category)
{
	const FMD_TrashCategoryRow* Row = FindTrashCategoryRowInternal(Category);
	return Row ? Row->RefundCredits : 0;
}

bool UMD_TrashLibrary::IsTrashCategory(const FGameplayTag Category)
{
	// Bonus.* 가 칸 5개와 같은 가지에 있어 드롭다운에 섞인다. 명시적으로 나열해 걸러낸다
	return Category.MatchesTagExact(MDGameplayTags::Score_Sorting_Metal)
		|| Category.MatchesTagExact(MDGameplayTags::Score_Sorting_General)
		|| Category.MatchesTagExact(MDGameplayTags::Score_Sorting_Plastic)
		|| Category.MatchesTagExact(MDGameplayTags::Score_Sorting_Contaminated)
		|| Category.MatchesTagExact(MDGameplayTags::Score_Sorting_NonBurnable);
}

FGameplayTag UMD_TrashLibrary::GetMisplacedSortScoreTag(const FGameplayTag CorrectCategory)
{
	// 기획 05 S−02: 오염물·따개비·오염조각을 비오염 칸에 넣으면 −24 (그 외 −12)
	return CorrectCategory.MatchesTagExact(MDGameplayTags::Score_Sorting_Contaminated)
		? MDGameplayTags::Score_Penalty_MissortContaminated
		: MDGameplayTags::Score_Penalty_Missort;
}
