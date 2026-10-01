// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Trash/MD_TrashLibrary.h"

#include "Engine/DataTable.h"
#include "Engine/StaticMesh.h"
#include "Math/RandomStream.h"
#include "Gameplay/Trash/MD_TrashSettings.h"
#include "Gameplay/Trash/MD_TrashSortable.h"
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

	const FMD_TrashCategoryRow* FindTrashCategoryRowInternal(
		const FGameplayTag Category,
		const bool bWarnIfMissing = true)
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

		if (bWarnIfMissing)
		{
			WarnOnce(
				FName(*FString::Printf(TEXT("TrashCategoryRow:%s"), *Category.ToString())),
				FString::Printf(TEXT("칸 테이블에 '%s' 행이 없다."), *Category.ToString()));
		}

		return nullptr;
	}

	/** 기존 DataTable도 동작하도록 비어 있는 신규 정책 필드를 안전한 호환값으로 채운다. */
	bool CompleteSortDescriptor(FMD_TrashSortDescriptor& Descriptor)
	{
		const FMD_TrashCategoryRow* CategoryRow =
			FindTrashCategoryRowInternal(Descriptor.Category);
		if (!CategoryRow)
		{
			return false;
		}

		if (!Descriptor.CorrectSortScoreTag.IsValid())
		{
			Descriptor.CorrectSortScoreTag = CategoryRow->CorrectSortScoreTag.IsValid()
				? CategoryRow->CorrectSortScoreTag
				: CategoryRow->Category;
		}

		if (!Descriptor.MisSortScoreTag.IsValid())
		{
			if (CategoryRow->MisSortScoreTag.IsValid())
			{
				Descriptor.MisSortScoreTag = CategoryRow->MisSortScoreTag;
			}
			else
			{
				// 신규 열이 비어 있는 기존 asset의 호환 규칙. 데이터가 채워지면 이 분기는 타지 않는다.
				Descriptor.MisSortScoreTag = Descriptor.Category.MatchesTagExact(
					MDGameplayTags::Score_Sorting_Contaminated)
					? MDGameplayTags::Score_Penalty_MissortContaminated
					: MDGameplayTags::Score_Penalty_Missort;
			}
		}

		if (Descriptor.RefundCredits == INDEX_NONE)
		{
			Descriptor.RefundCredits = FMath::Max(0, CategoryRow->RefundCredits);
		}
		else
		{
			Descriptor.RefundCredits = FMath::Max(0, Descriptor.RefundCredits);
		}

		return Descriptor.IsResolved();
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

TArray<FName> UMD_TrashLibrary::GetTrashVariantIds(const FName TrashKindId)
{
	const FMD_TrashKindRow* Row = FindTrashKindRowInternal(TrashKindId);
	return Row ? Row->GetVariantIds() : TArray<FName>();
}

FName UMD_TrashLibrary::GetDefaultTrashVariantId(const FName TrashKindId)
{
	const FMD_TrashKindRow* Row = FindTrashKindRowInternal(TrashKindId);
	return Row ? Row->GetDefaultVariantId() : NAME_None;
}

bool UMD_TrashLibrary::ResolveTrashKindMesh(
	const FName TrashKindId, const FName VariantId, TSoftObjectPtr<UStaticMesh>& OutMesh)
{
	OutMesh.Reset();
	const FMD_TrashKindRow* Row = FindTrashKindRowInternal(TrashKindId);
	return Row && Row->ResolveMesh(VariantId, OutMesh);
}

bool UMD_TrashLibrary::SelectRandomTrashVariant(const FName TrashKindId, const int32 Seed, FName& OutVariantId)
{
	OutVariantId = NAME_None;
	const FMD_TrashKindRow* Row = FindTrashKindRowInternal(TrashKindId);
	if (!Row) return false;
	const TArray<FName> Ids = Row->GetVariantIds();
	if (Ids.IsEmpty()) return false;
	FRandomStream RandomStream(Seed);
	OutVariantId = Ids[RandomStream.RandRange(0, Ids.Num() - 1)];
	return true;
}

FGameplayTag UMD_TrashLibrary::GetTrashKindCategory(const FName TrashKindId)
{
	const FMD_TrashKindRow* Row = FindTrashKindRowInternal(TrashKindId);
	return Row ? Row->Category : FGameplayTag();
}

int32 UMD_TrashLibrary::GetTrashKindRefundCredits(const FName TrashKindId)
{
	FMD_TrashSortDescriptor Descriptor;
	return GetTrashKindSortDescriptor(TrashKindId, Descriptor)
		? Descriptor.RefundCredits
		: 0;
}

bool UMD_TrashLibrary::GetTrashKindSortDescriptor(
	const FName TrashKindId,
	FMD_TrashSortDescriptor& OutDescriptor)
{
	OutDescriptor = FMD_TrashSortDescriptor();

	const FMD_TrashKindRow* KindRow = FindTrashKindRowInternal(TrashKindId);
	if (!KindRow)
	{
		return false;
	}

	OutDescriptor.Category = KindRow->Category;
	OutDescriptor.BonusScoreTag = KindRow->ScoreBonusTag;
	if (!CompleteSortDescriptor(OutDescriptor))
	{
		OutDescriptor = FMD_TrashSortDescriptor();
		return false;
	}

	// 산재 쓰레기는 칸 기본 환급에 종류별 가산을 더한다. 음수 데이터는 경제 차감으로 번지지 않는다.
	OutDescriptor.RefundCredits += FMath::Max(0, KindRow->RefundBonusCredits);
	return true;
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
	return Row ? FMath::Max(0, Row->RefundCredits) : 0;
}

bool UMD_TrashLibrary::IsTrashCategory(const FGameplayTag Category)
{
	return FindTrashCategoryRowInternal(Category, /*bWarnIfMissing=*/false) != nullptr;
}

FGameplayTag UMD_TrashLibrary::GetCorrectSortScoreTag(const FGameplayTag Category)
{
	const FMD_TrashCategoryRow* Row = FindTrashCategoryRowInternal(Category);
	if (!Row)
	{
		return FGameplayTag();
	}

	return Row->CorrectSortScoreTag.IsValid()
		? Row->CorrectSortScoreTag
		: Row->Category;
}

FGameplayTag UMD_TrashLibrary::GetMisplacedSortScoreTag(const FGameplayTag CorrectCategory)
{
	const FMD_TrashCategoryRow* Row = FindTrashCategoryRowInternal(CorrectCategory);
	if (!Row)
	{
		return FGameplayTag();
	}

	if (Row->MisSortScoreTag.IsValid())
	{
		return Row->MisSortScoreTag;
	}

	// 신규 열이 비어 있는 기존 asset의 호환 규칙.
	return CorrectCategory.MatchesTagExact(MDGameplayTags::Score_Sorting_Contaminated)
		? MDGameplayTags::Score_Penalty_MissortContaminated
		: MDGameplayTags::Score_Penalty_Missort;
}

bool UMD_TrashLibrary::ResolveSortDescriptor(
	UObject* SortableObject,
	FMD_TrashSortDescriptor& OutDescriptor)
{
	OutDescriptor = FMD_TrashSortDescriptor();
	if (!IsValid(SortableObject) || !SortableObject->Implements<UMD_TrashSortable>())
	{
		return false;
	}

	OutDescriptor = IMD_TrashSortable::Execute_GetTrashSortDescriptor(SortableObject);

	// 기존 WallPiece/오염물/BP 구현은 descriptor 추가 전 계약만 구현해도 계속 동작한다.
	if (!OutDescriptor.Category.IsValid())
	{
		OutDescriptor.Category = IMD_TrashSortable::Execute_GetTrashCategory(SortableObject);
	}
	if (!OutDescriptor.BonusScoreTag.IsValid())
	{
		OutDescriptor.BonusScoreTag =
			IMD_TrashSortable::Execute_GetSortScoreBonusTag(SortableObject);
	}

	if (!CompleteSortDescriptor(OutDescriptor))
	{
		OutDescriptor = FMD_TrashSortDescriptor();
		return false;
	}

	return true;
}

FMD_TrashSortResult UMD_TrashLibrary::EvaluateSort(
	const FMD_TrashSortDescriptor& Descriptor,
	const FGameplayTag AcceptedCategory)
{
	FMD_TrashSortResult Result;
	if (!Descriptor.IsResolved()
		|| !IsTrashCategory(AcceptedCategory))
	{
		return Result;
	}

	Result.bCorrectSort = Descriptor.Category.MatchesTagExact(AcceptedCategory);
	Result.PrimaryScoreTag = Result.bCorrectSort
		? Descriptor.CorrectSortScoreTag
		: Descriptor.MisSortScoreTag;
	if (!Result.PrimaryScoreTag.IsValid())
	{
		return FMD_TrashSortResult();
	}

	Result.bCanSort = true;
	if (Result.bCorrectSort)
	{
		Result.BonusScoreTag = Descriptor.BonusScoreTag;
		Result.RefundCredits = FMath::Max(0, Descriptor.RefundCredits);
	}

	return Result;
}
