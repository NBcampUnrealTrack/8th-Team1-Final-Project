#include "Data/Merchant/MD_QuestDefinition.h"

FPrimaryAssetId UMD_QuestDefinition::GetPrimaryAssetId() const
{
	// 별도 식별자 없이 자산 이름을 저장과 조회의 단일 기준으로 사용하는 처리
	FPrimaryAssetId OutPrimaryAssetId(QuestPrimaryAssetType, GetFName());
	return OutPrimaryAssetId;
}
