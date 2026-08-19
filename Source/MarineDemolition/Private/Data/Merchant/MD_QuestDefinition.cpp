#include "Data/Merchant/MD_QuestDefinition.h"

#include "Engine/AssetManager.h"

FPrimaryAssetId UMD_QuestDefinition::GetPrimaryAssetId() const
{
	// 별도 식별자 없이 자산 이름을 저장과 조회의 단일 기준으로 사용하는 처리
	FPrimaryAssetId OutPrimaryAssetId(QuestPrimaryAssetType, GetFName());
	return OutPrimaryAssetId;
}

UMD_QuestDefinition* UMD_QuestDefinition::LoadDefinition(const FPrimaryAssetId& DefinitionId)
{
	if (DefinitionId.IsValid() == false)
	{
		return nullptr;
	}

	// 이미 로드된 원본을 우선 사용하고 필요한 경우에만 패키지를 동기 로드한다.
	UAssetManager& AssetManager = UAssetManager::Get();
	UMD_QuestDefinition* OutDefinition = Cast<UMD_QuestDefinition>(AssetManager.GetPrimaryAssetObject(DefinitionId));
	if (IsValid(OutDefinition) == false)
	{
		const FSoftObjectPath DefinitionPath = AssetManager.GetPrimaryAssetPath(DefinitionId);
		OutDefinition = Cast<UMD_QuestDefinition>(DefinitionPath.TryLoad());
	}

	// 다른 타입 또는 다른 ID로 등록된 에셋이 상태 데이터에 연결되지 않도록 검증한다.
	return IsValid(OutDefinition) == true && OutDefinition->GetPrimaryAssetId() == DefinitionId
		? OutDefinition
		: nullptr;
}
