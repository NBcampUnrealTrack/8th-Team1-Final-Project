#include "Data/Audio/MD_AudioCategoryData.h"
#include "Data/Audio/MD_AudioSettings.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

// ---------------------------------------------------------------- 카테고리 에셋

void UMD_AudioLibrary::CollectEntries(TArray<FMD_AudioEntry>& OutEntries) const
{
	for (const TObjectPtr<UMD_AudioCategoryData>& CategoryData : Categories)
	{
		if (!CategoryData)
		{
			continue;
		}

		OutEntries.Append(CategoryData->Entries);
	}
}

#if WITH_EDITOR

EDataValidationResult UMD_AudioCategoryData::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	for (int32 Index = 0; Index < Entries.Num(); ++Index)
	{
		const FMD_AudioEntry& Entry = Entries[Index];

		if (!Entry.EventTag.IsValid())
		{
			Context.AddError(FText::Format(
				NSLOCTEXT("MD", "AudioEntryNoTag", "[{0}] 항목의 EventTag 가 비어 있습니다."),
				FText::AsNumber(Index)));
			Result = EDataValidationResult::Invalid;
		}

		if (Entry.Sound.IsNull())
		{
			Context.AddError(FText::Format(
				NSLOCTEXT("MD", "AudioEntryNoSound", "[{0}] {1} 항목에 Sound 가 없습니다."),
				FText::AsNumber(Index), FText::FromName(Entry.EventTag.GetTagName())));
			Result = EDataValidationResult::Invalid;
		}

		// 루프인데 동시 재생 제한이 없으면 중첩으로 볼륨이 쌓인다
		if (Entry.bLooping && Entry.Concurrency.IsNull())
		{
			Context.AddWarning(FText::Format(
				NSLOCTEXT("MD", "AudioLoopNoConcurrency", "{0} 은 루프인데 Concurrency 가 비어 있습니다."),
				FText::FromName(Entry.EventTag.GetTagName())));
		}
	}

	return Result;
}

EDataValidationResult UMD_AudioLibrary::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	TSet<EMD_AudioCategory> Seen;
	for (const TObjectPtr<UMD_AudioCategoryData>& CategoryData : Categories)
	{
		if (!CategoryData)
		{
			Context.AddError(NSLOCTEXT("MD", "AudioLibraryNullCategory", "Categories 에 비어 있는 항목이 있습니다."));
			Result = EDataValidationResult::Invalid;
			continue;
		}

		bool bAlreadySeen = false;
		Seen.Add(CategoryData->Category, &bAlreadySeen);
		if (bAlreadySeen)
		{
			Context.AddWarning(FText::Format(
				NSLOCTEXT("MD", "AudioLibraryDupCategory", "카테고리 {0} 가 두 번 등록되어 있습니다."),
				FText::AsNumber(static_cast<int32>(CategoryData->Category))));
		}
	}

	return Result;
}

#endif // WITH_EDITOR

// ---------------------------------------------------------------------- 설정

UMD_AudioSettings::UMD_AudioSettings()
{
	CategoryName = TEXT("Game");
	SectionName = TEXT("MD Audio");
}

const UMD_AudioSettings& UMD_AudioSettings::Get()
{
	const UMD_AudioSettings* Settings = GetDefault<UMD_AudioSettings>();
	check(Settings);
	return *Settings;
}
