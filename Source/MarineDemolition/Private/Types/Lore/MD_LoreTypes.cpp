#include "Types/Lore/MD_LoreTypes.h"

#include "GameplayTags/MD_GameplayTags.h"

const FGameplayTag FMD_LoreData::DataFieldKey = MDGameplayTags::Save_Data_Lore;

bool FMD_LoreSiteProgress::IsStructurallyValid() const
{
	if (SiteId.IsNone() == true)
	{
		return false;
	}

	// 발견 기록 없이 완료 보상만 지급된 손상 상태를 차단하기 위한 검사
	if (bCompletionRewardGranted == true && DiscoveredLoreIds.Num() <= 0)
	{
		return false;
	}

	// 하나의 로어가 여러 번 보상되는 손상 상태를 차단하기 위한 식별자 집합
	TSet<FName> UniqueLoreIds;
	for (const FName LoreId : DiscoveredLoreIds)
	{
		if (LoreId.IsNone() == true || UniqueLoreIds.Contains(LoreId) == true)
		{
			return false;
		}

		UniqueLoreIds.Add(LoreId);
	}

	return true;
}

bool FMD_LoreData::IsStructurallyValid() const
{
	// 같은 현장의 진행 상태가 분리되어 중복 보상되는 상황을 막기 위한 식별자 집합
	TSet<FName> UniqueSiteIds;
	for (const FMD_LoreSiteProgress& SiteProgress : SiteProgressList)
	{
		if (SiteProgress.IsStructurallyValid() == false || UniqueSiteIds.Contains(SiteProgress.SiteId) == true)
		{
			return false;
		}

		UniqueSiteIds.Add(SiteProgress.SiteId);
	}

	return true;
}
