#pragma once

#include "CoreMinimal.h"
#include "Data/Equipment/MD_ItemDefinition.h"
#include "Engine/AssetManagerTypes.h"
#include "MD_QuestItemDefinition.generated.h"

/**
 * 핫바 슬롯에 보관하는 퀘스트 회수물 정의 (연구 데이터 카트리지 등).
 *
 * 도구·소모품과 달리 사용 GA가 없어 슬롯만 차지한다. 지급은 빈 슬롯이 있을 때만 성공하며,
 * 지급 주체(데이터 덤프 단말 등)가 CanGrant 실패를 "슬롯 가득 참"으로 안내한다.
 * 물리 운반 퀘스트 아이템(AMD_QuestItem)과는 별개 경로다.
 */
UCLASS(BlueprintType, Blueprintable)
class MARINEDEMOLITION_API UMD_QuestItemDefinition : public UMD_ItemDefinition
{
	GENERATED_BODY()

public:
	virtual bool CanGrant_Implementation(FMD_ItemGrantContext Context) override;
	virtual bool Grant_Implementation(FMD_ItemGrantContext Context) override;

public:
	/** 이 회수물이 귀속된 퀘스트 DA 식별자. 납품 경로가 핫바 품목을 퀘스트와 대조할 때 쓴다 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item|Quest")
	FPrimaryAssetId QuestDefinitionId;
};
