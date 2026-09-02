#include "Data/Lore/MD_LoreCatalog.h"

#include "Data/Lore/MD_LoreDefinition.h"

UMD_LoreDefinition* UMD_LoreCatalog::GetLoreDefinition(int32 LoreIndex) const
{
	// 카탈로그 범위를 벗어난 UI 인덱스가 Asset Manager 조회로 이어지지 않게 한다.
	if (LoreEntries.IsValidIndex(LoreIndex) == false)
	{
		return nullptr;
	}

	// 저장된 Asset ID를 공통 조회 경로에서 정의 에셋으로 해석한다.
	FPrimaryAssetId LoreAssetId = LoreEntries[LoreIndex];
	UMD_LoreDefinition* LoreDefinition = FindLoreByAssetId(LoreAssetId);
	return LoreDefinition;
}

UMD_LoreDefinition* UMD_LoreCatalog::FindLoreByAssetId(FPrimaryAssetId LoreAssetId) const
{
	// 모든 호출부가 동일한 Asset Manager 조회 규칙을 사용하도록 정의 클래스의 공통 함수를 호출한다.
	UMD_LoreDefinition* LoreDefinition = UMD_LoreDefinition::LoadDefinition(LoreAssetId);
	return LoreDefinition;
}

EMD_LoreEntryState UMD_LoreCatalog::ResolveLoreState(FPrimaryAssetId LoreAssetId, const TArray<FPrimaryAssetId>& DiscoveredLoreAssetIds) const
{
	// 수집되지 않은 에셋은 코덱스에서 이름과 본문을 숨기는 미발견 상태로 처리한다.
	if (LoreAssetId.IsValid() == false)
	{
		return EMD_LoreEntryState::Undiscovered;
	}

	if (DiscoveredLoreAssetIds.Contains(LoreAssetId) == false)
	{
		return EMD_LoreEntryState::Undiscovered;
	}

	// 발견된 태그가 유효한 정의 에셋과 연결되지 않으면 공개하지 않는다.
	UMD_LoreDefinition* FoundLore = FindLoreByAssetId(LoreAssetId);
	if (IsValid(FoundLore) == false)
	{
		return EMD_LoreEntryState::Undiscovered;
	}

	if (FoundLore->IsValidDefinition() == false)
	{
		return EMD_LoreEntryState::Undiscovered;
	}

	// 선행 Asset ID가 하나도 없으면 발견 즉시 전체 내용을 공개한다.
	if (FoundLore->DecryptionPrerequisiteLoreIds.Num() <= 0)
	{
		return EMD_LoreEntryState::Decrypted;
	}

	// 선행 로어 Asset ID가 하나라도 발견되지 않았으면 본문을 계속 보호한다.
	for (FPrimaryAssetId PrerequisiteLoreAssetId : FoundLore->DecryptionPrerequisiteLoreIds)
	{
		UMD_LoreDefinition* PrerequisiteLore = FindLoreByAssetId(PrerequisiteLoreAssetId);
		if (IsValid(PrerequisiteLore) == false)
		{
			return EMD_LoreEntryState::Encrypted;
		}

		if (DiscoveredLoreAssetIds.Contains(PrerequisiteLoreAssetId) == false)
		{
			return EMD_LoreEntryState::Encrypted;
		}
	}

	return EMD_LoreEntryState::Decrypted;
}

bool UMD_LoreCatalog::ValidateCatalog(TArray<FText>& OutErrors) const
{
	// 이전 검증 결과가 다음 호출에 섞이지 않도록 출력 배열을 초기화한다.
	OutErrors.Reset();

	// 코덱스 화면 제목을 안정적으로 제공하기 위한 현장 메타데이터를 검사한다.
	if (SiteDisplayName.IsEmpty() == true)
	{
		OutErrors.Add(NSLOCTEXT("MDLoreValidation", "MissingSiteDisplayName", "로어 카탈로그의 현장 이름이 비어 있습니다."));
	}

	if (LoreEntries.Num() <= 0)
	{
		OutErrors.Add(NSLOCTEXT("MDLoreValidation", "EmptyLoreEntries", "로어 카탈로그에 기록물이 없습니다."));
	}

	// 중복 Asset ID를 검증하기 위한 집합을 구성한다.
	TSet<FPrimaryAssetId> KnownLoreAssetIds;
	for (FPrimaryAssetId LoreAssetId : LoreEntries)
	{
		if (LoreAssetId.IsValid() == false)
		{
			OutErrors.Add(NSLOCTEXT("MDLoreValidation", "MissingLoreAssetId", "로어 카탈로그에 비어 있는 기록물 Asset ID가 있습니다."));
			continue;
		}

		if (KnownLoreAssetIds.Contains(LoreAssetId) == true)
		{
			OutErrors.Add(FText::Format(NSLOCTEXT("MDLoreValidation", "DuplicateLoreAssetId", "중복된 로어 Asset ID입니다: {0}"), FText::FromString(LoreAssetId.ToString())));
			continue;
		}

		KnownLoreAssetIds.Add(LoreAssetId);

		UMD_LoreDefinition* LoreEntry = FindLoreByAssetId(LoreAssetId);
		if (IsValid(LoreEntry) == false)
		{
			OutErrors.Add(FText::Format(NSLOCTEXT("MDLoreValidation", "MissingLoreDefinition", "등록된 Asset ID의 로어 정의를 찾을 수 없습니다: {0}"), FText::FromString(LoreAssetId.ToString())));
			continue;
		}

		if (LoreEntry->IsValidDefinition() == false)
		{
			OutErrors.Add(FText::Format(NSLOCTEXT("MDLoreValidation", "InvalidLoreDefinition", "필수 데이터가 잘못된 로어 기록물입니다: {0}"), FText::FromString(LoreAssetId.ToString())));
			continue;
		}

	}

	// 각 선행 Asset ID가 같은 카탈로그의 다른 로어 정의를 가리키는지 검사한다.
	for (FPrimaryAssetId LoreAssetId : LoreEntries)
	{
		UMD_LoreDefinition* LoreEntry = FindLoreByAssetId(LoreAssetId);
		if (IsValid(LoreEntry) == false)
		{
			continue;
		}

		TSet<FPrimaryAssetId> KnownPrerequisiteAssetIds;
		for (FPrimaryAssetId PrerequisiteLoreAssetId : LoreEntry->DecryptionPrerequisiteLoreIds)
		{
			if (PrerequisiteLoreAssetId.IsValid() == false)
			{
				OutErrors.Add(FText::Format(NSLOCTEXT("MDLoreValidation", "MissingDecryptionPrerequisite", "비어 있는 암호화 선행 Asset ID가 있습니다: {0}"), FText::FromString(LoreAssetId.ToString())));
				continue;
			}

			if (PrerequisiteLoreAssetId == LoreAssetId)
			{
				OutErrors.Add(FText::Format(NSLOCTEXT("MDLoreValidation", "SelfDecryptionPrerequisite", "자기 자신을 암호화 선행 기록으로 지정했습니다: {0}"), FText::FromString(LoreAssetId.ToString())));
				continue;
			}

			if (KnownPrerequisiteAssetIds.Contains(PrerequisiteLoreAssetId) == true)
			{
				OutErrors.Add(FText::Format(NSLOCTEXT("MDLoreValidation", "DuplicateDecryptionPrerequisite", "중복된 암호화 선행 Asset ID입니다: {0} -> {1}"), FText::FromString(LoreAssetId.ToString()), FText::FromString(PrerequisiteLoreAssetId.ToString())));
				continue;
			}

			KnownPrerequisiteAssetIds.Add(PrerequisiteLoreAssetId);
			if (KnownLoreAssetIds.Contains(PrerequisiteLoreAssetId) == false)
			{
				OutErrors.Add(FText::Format(NSLOCTEXT("MDLoreValidation", "UnknownDecryptionPrerequisite", "카탈로그에 없는 암호화 선행 Asset ID입니다: {0} -> {1}"), FText::FromString(LoreAssetId.ToString()), FText::FromString(PrerequisiteLoreAssetId.ToString())));
			}
		}
	}

	// 검증 오류가 하나도 없을 때만 카탈로그를 사용할 수 있도록 결과를 반환한다.
	bool bIsValidCatalog = OutErrors.Num() == 0;
	return bIsValidCatalog;
}
