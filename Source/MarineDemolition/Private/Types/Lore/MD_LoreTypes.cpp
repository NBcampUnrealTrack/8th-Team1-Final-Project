#include "Types/Lore/MD_LoreTypes.h"

#include "GameplayTags/MD_GameplayTags.h"

const FGameplayTag FMD_LoreData::DataFieldKey = MDGameplayTags::Save_Data_Lore;

bool FMD_LoreData::IsStructurallyValid() const
{
	// 하나의 로어가 발견 목록에 중복 저장되는 손상 상태를 차단하기 위한 식별자 집합
	TSet<FPrimaryAssetId> UniqueLoreAssetIds;
	for (FPrimaryAssetId LoreAssetId : DiscoveredLoreAssetIds)
	{
		if (LoreAssetId.IsValid() == false)
		{
			return false;
		}

		if (UniqueLoreAssetIds.Contains(LoreAssetId) == true)
		{
			return false;
		}

		UniqueLoreAssetIds.Add(LoreAssetId);
	}

	return true;
}
