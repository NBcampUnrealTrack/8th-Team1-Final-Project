#include "Gameplay/Lore/MD_LoreSubsystem.h"
#include "Engine/GameInstance.h"

#include "Data/Lore/MD_LoreDefinition.h"
#include "Util/MD_LogUtility.h"

bool UMD_LoreSubsystem::TryDiscoverLore(FPrimaryAssetId LoreAssetId)
{
	// 서버 권한과 유효한 Asset ID가 모두 있어야 발견 원장을 변경한다.
	if (HasServerAuthority() == false)
	{
		return false;
	}

	if (LoreAssetId.IsValid() == false)
	{
		return false;
	}

	// 액터에 할당된 Asset ID가 실제 로어 정의로 해석되는지 확인한다.
	UMD_LoreDefinition* LoreDefinition = GetLoreDefinition(LoreAssetId);
	if (IsValid(LoreDefinition) == false)
	{
		return false;
	}

	if (LoreDefinition->IsValidDefinition() == false)
	{
		return false;
	}
	// 이미 발견한 로어는 전역 원장에 중복 기록하지 않는다.
	if (DiscoveredLoreAssetIds.Contains(LoreAssetId) == true)
	{
		return false;
	}

	// 카탈로그와 관계없이 전역 발견 사실을 기록한다.
	DiscoveredLoreAssetIds.Add(LoreAssetId);

	// 액터 파괴 이벤트보다 먼저 서버의 확정 결과를 남기기 위한 수집 로그
	UE_LOG(MarineDemolition, Log, TEXT("%s [로어] 기록물 수집 완료: 에셋=%s, 전체 진행=%d"), *UE_CALL_LOCATIONINFO, *LoreAssetId.ToString(), DiscoveredLoreAssetIds.Num());
	OnLoreProgressChanged.Broadcast();

	return true;
}

UMD_LoreDefinition* UMD_LoreSubsystem::GetLoreDefinition(FPrimaryAssetId LoreAssetId) const
{
	// 정의 클래스의 공통 Asset Manager 조회 경로를 사용한다.
	UMD_LoreDefinition* LoreDefinition = UMD_LoreDefinition::LoadDefinition(LoreAssetId);
	return LoreDefinition;
}

EMD_LoreEntryState UMD_LoreSubsystem::GetLoreState(const UMD_LoreCatalog* LoreCatalog, FPrimaryAssetId LoreAssetId) const
{
	if (IsValid(LoreCatalog) == false || LoreAssetId.IsValid() == false)
	{
		return EMD_LoreEntryState::Undiscovered;
	}

	EMD_LoreEntryState LoreState = LoreCatalog->ResolveLoreState(LoreAssetId, DiscoveredLoreAssetIds);
	return LoreState;
}

bool UMD_LoreSubsystem::HasDiscoveredLore(FPrimaryAssetId LoreAssetId) const
{
	if (LoreAssetId.IsValid() == false)
	{
		return false;
	}

	bool bIsDiscovered = DiscoveredLoreAssetIds.Contains(LoreAssetId);
	return bIsDiscovered;
}

bool UMD_LoreSubsystem::IsCatalogComplete(const UMD_LoreCatalog* LoreCatalog) const
{
	if (IsValid(LoreCatalog) == false || LoreCatalog->LoreEntries.Num() <= 0)
	{
		return false;
	}

	// 정적 카탈로그의 모든 식별자가 발견 원장에 존재하는지 확인하는 완성 판정
	for (int32 LoreIndex = 0; LoreIndex < LoreCatalog->GetLoreCount(); ++LoreIndex)
	{
		// 카탈로그 Asset ID를 완성 판정에 사용할 정의 에셋으로 해석한다.
		UMD_LoreDefinition* LoreDefinition = LoreCatalog->GetLoreDefinition(LoreIndex);
		if (IsValid(LoreDefinition) == false)
		{
			return false;
		}

		FPrimaryAssetId LoreAssetId = LoreCatalog->LoreEntries[LoreIndex];
		if (DiscoveredLoreAssetIds.Contains(LoreAssetId) == false)
		{
			return false;
		}
	}

	return true;
}

bool UMD_LoreSubsystem::HasServerAuthority() const
{
	const UWorld* World = GetWorld();
	return IsValid(World) == true && World->GetNetMode() != ENetMode::NM_Client;
}

void UMD_LoreSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
}

void UMD_LoreSubsystem::Deinitialize()
{
    Super::Deinitialize();
}
