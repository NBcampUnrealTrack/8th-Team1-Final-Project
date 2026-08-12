#include "Data/Lore/MD_LoreCatalog.h"

bool FMD_LoreDefinition::IsValidDefinition() const
{
	return LoreId.IsNone() == false && EntryName.IsEmpty() == false && EntryContent.IsEmpty() == false;
}

bool UMD_LoreCatalog::FindLore(FName LoreId, FMD_LoreDefinition& OutLore) const
{
	// 유효한 정의만 외부로 전달하여 잘못 입력된 카탈로그 데이터의 런타임 확산 방지
	const FMD_LoreDefinition* FoundLore = FindLoreDefinition(LoreId);
	if (FoundLore == nullptr || FoundLore->IsValidDefinition() == false)
	{
		OutLore = FMD_LoreDefinition();
		return false;
	}

	OutLore = *FoundLore;
	return true;
}

int32 UMD_LoreCatalog::GetLoreCount() const
{
	return LoreEntries.Num();
}

EMD_LoreEntryState UMD_LoreCatalog::ResolveLoreState(FName LoreId, const TArray<FName>& DiscoveredLoreIds) const
{
	// 수집되지 않은 식별자는 코덱스에서 이름과 본문을 숨기는 미발견 상태
	if (LoreId.IsNone() == true || DiscoveredLoreIds.Contains(LoreId) == false)
	{
		return EMD_LoreEntryState::Undiscovered;
	}

	const FMD_LoreDefinition* FoundLore = FindLoreDefinition(LoreId);
	if (FoundLore == nullptr || FoundLore->IsValidDefinition() == false)
	{
		return EMD_LoreEntryState::Undiscovered;
	}

	// 암호화 조건이 없는 일반 기록물의 발견 즉시 전체 공개 상태
	if (FoundLore->bEncryptedUntilPrerequisitesComplete == false)
	{
		return EMD_LoreEntryState::Decrypted;
	}

	// 하나라도 빠진 선행 기록이 있으면 본문을 계속 보호하는 암호화 상태
	for (const FName PrerequisiteLoreId : FoundLore->DecryptionPrerequisiteLoreIds)
	{
		if (DiscoveredLoreIds.Contains(PrerequisiteLoreId) == false)
		{
			return EMD_LoreEntryState::Encrypted;
		}
	}

	return EMD_LoreEntryState::Decrypted;
}

bool UMD_LoreCatalog::ValidateCatalog(TArray<FText>& OutErrors) const
{
	OutErrors.Reset();

	// 저장 키와 화면 제목을 안정적으로 제공하기 위한 현장 메타데이터 검사
	if (SiteId.IsNone() == true)
	{
		OutErrors.Add(NSLOCTEXT("MDLoreValidation", "MissingSiteId", "로어 카탈로그의 SiteId가 비어 있습니다."));
	}
	if (SiteDisplayName.IsEmpty() == true)
	{
		OutErrors.Add(NSLOCTEXT("MDLoreValidation", "MissingSiteDisplayName", "로어 카탈로그의 현장 이름이 비어 있습니다."));
	}
	if (LoreEntries.Num() <= 0)
	{
		OutErrors.Add(NSLOCTEXT("MDLoreValidation", "EmptyLoreEntries", "로어 카탈로그에 기록물이 없습니다."));
	}

	// 선행 참조가 배열 순서와 무관하게 같은 카탈로그 항목을 가리키는지 확인하기 위한 식별자 집합
	TSet<FName> KnownLoreIds;
	for (const FMD_LoreDefinition& LoreEntry : LoreEntries)
	{
		if (LoreEntry.LoreId.IsNone() == true)
		{
			OutErrors.Add(NSLOCTEXT("MDLoreValidation", "MissingLoreId", "식별자가 비어 있는 로어 기록물이 있습니다."));
			continue;
		}

		if (KnownLoreIds.Contains(LoreEntry.LoreId) == true)
		{
			OutErrors.Add(FText::Format(NSLOCTEXT("MDLoreValidation", "DuplicateLoreId", "중복된 로어 식별자입니다: {0}"), FText::FromName(LoreEntry.LoreId)));
			continue;
		}

		KnownLoreIds.Add(LoreEntry.LoreId);
	}

	// 표시 데이터와 암호화 설정이 런타임 상태 계산에 사용 가능한지 확인하는 항목별 검사
	for (const FMD_LoreDefinition& LoreEntry : LoreEntries)
	{
		if (LoreEntry.IsValidDefinition() == false)
		{
			OutErrors.Add(FText::Format(NSLOCTEXT("MDLoreValidation", "InvalidLoreDefinition", "필수 데이터가 잘못된 로어 기록물입니다: {0}"), FText::FromName(LoreEntry.LoreId)));
		}

		if (LoreEntry.bEncryptedUntilPrerequisitesComplete == true && LoreEntry.DecryptionPrerequisiteLoreIds.Num() <= 0)
		{
			OutErrors.Add(FText::Format(NSLOCTEXT("MDLoreValidation", "MissingDecryptionPrerequisite", "암호화 선행 기록이 없는 로어 기록물입니다: {0}"), FText::FromName(LoreEntry.LoreId)));
		}

		if (LoreEntry.bEncryptedUntilPrerequisitesComplete == false && LoreEntry.DecryptionPrerequisiteLoreIds.Num() > 0)
		{
			OutErrors.Add(FText::Format(NSLOCTEXT("MDLoreValidation", "UnusedDecryptionPrerequisite", "암호화가 꺼져 있지만 선행 기록이 지정된 로어 기록물입니다: {0}"), FText::FromName(LoreEntry.LoreId)));
		}

		for (const FName PrerequisiteLoreId : LoreEntry.DecryptionPrerequisiteLoreIds)
		{
			if (PrerequisiteLoreId == LoreEntry.LoreId)
			{
				OutErrors.Add(FText::Format(NSLOCTEXT("MDLoreValidation", "SelfDecryptionPrerequisite", "자기 자신을 암호화 선행 기록으로 지정한 로어 기록물입니다: {0}"), FText::FromName(LoreEntry.LoreId)));
			}
			else if (KnownLoreIds.Contains(PrerequisiteLoreId) == false)
			{
				OutErrors.Add(FText::Format(NSLOCTEXT("MDLoreValidation", "UnknownDecryptionPrerequisite", "존재하지 않는 암호화 선행 기록입니다: {0} -> {1}"), FText::FromName(LoreEntry.LoreId), FText::FromName(PrerequisiteLoreId)));
			}
		}
	}

	return OutErrors.Num() == 0;
}

const FMD_LoreDefinition* UMD_LoreCatalog::FindLoreDefinition(FName LoreId) const
{
	// 빈 식별자는 어떤 기록물도 대표하지 않으므로 배열 순회 전 조기 종료
	if (LoreId.IsNone() == true)
	{
		return nullptr;
	}

	for (const FMD_LoreDefinition& LoreEntry : LoreEntries)
	{
		if (LoreEntry.LoreId == LoreId)
		{
			return &LoreEntry;
		}
	}

	return nullptr;
}
