#include "Data/Lore/MD_LoreDefinition.h"

#include "Engine/AssetManager.h"

FPrimaryAssetId UMD_LoreDefinition::GetPrimaryAssetId() const
{
	// 실제 로어 에셋 이름을 Primary Asset 이름으로 사용해 식별자를 구성한다.
	FPrimaryAssetId PrimaryAssetId = FPrimaryAssetId(AssetType, GetFName());
	return PrimaryAssetId;
}

bool UMD_LoreDefinition::IsValidDefinition() const
{
	// Asset Manager에서 조회할 분류 타입이 설정되어야 한다.
	if (AssetType.IsValid() == false)
	{
		return false;
	}

	// 코덱스에 공개할 이름이 입력되어야 한다.
	if (EntryName.IsEmpty() == true)
	{
		return false;
	}

	return true;
}

UMD_LoreDefinition* UMD_LoreDefinition::LoadDefinition(FPrimaryAssetId LoreAssetId)
{
	// 비어 있는 Asset ID는 조회 가능한 기록물을 나타내지 않는다.
	if (LoreAssetId.IsValid() == false)
	{
		return nullptr;
	}

	// 등록된 Primary Asset 경로를 제공할 전역 Asset Manager를 조회한다.
	UAssetManager* AssetManager = UAssetManager::GetIfInitialized();
	if (IsValid(AssetManager) == false)
	{
		return nullptr;
	}

	// 이미 메모리에 있는 정의 에셋을 먼저 재사용한다.
	UObject* PrimaryAssetObject = AssetManager->GetPrimaryAssetObject(LoreAssetId);
	if (IsValid(PrimaryAssetObject) == false)
	{
		// 아직 로드되지 않은 정의는 등록 경로를 통해 동기 로드한다.
		FSoftObjectPath PrimaryAssetPath = AssetManager->GetPrimaryAssetPath(LoreAssetId);
		if (PrimaryAssetPath.IsValid() == false)
		{
			return nullptr;
		}

		PrimaryAssetObject = PrimaryAssetPath.TryLoad();
	}

	// 다른 Primary Asset 타입이나 다른 ID로 등록된 에셋은 로어 정의로 사용하지 않는다.
	UMD_LoreDefinition* LoreDefinition = Cast<UMD_LoreDefinition>(PrimaryAssetObject);
	if (IsValid(LoreDefinition) == false)
	{
		return nullptr;
	}

	if (LoreDefinition->GetPrimaryAssetId() != LoreAssetId)
	{
		return nullptr;
	}

	return LoreDefinition;
}
