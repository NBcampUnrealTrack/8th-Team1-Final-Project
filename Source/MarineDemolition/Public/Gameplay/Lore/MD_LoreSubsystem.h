#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Data/Lore/MD_LoreCatalog.h"
#include "MD_LoreSubsystem.generated.h"

class UMD_LoreDefinition;
class UMD_LoreProfileSubsystem;

// 기존 로어 API에서 개인 저장 원장과 정의 에셋으로 연결하는 조회 경로
UCLASS()
class MARINEDEMOLITION_API UMD_LoreSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// 현재 로컬 사용자가 조사한 기록의 개인 저장
	UFUNCTION(BlueprintCallable, Category = "MD|Lore")
	bool TryDiscoverLore(FPrimaryAssetId LoreAssetId);

	// 기존 카탈로그에서 사용하는 로어 정의 에셋 조회
	UFUNCTION(BlueprintPure, Category = "MD|Lore")
	UMD_LoreDefinition* GetLoreDefinition(FPrimaryAssetId LoreAssetId) const;

	// 개인 발견 이력과 선행 조건에 따른 해독 상태 조회
	UFUNCTION(BlueprintPure, Category = "MD|Lore")
	EMD_LoreEntryState GetLoreState(const UMD_LoreCatalog* LoreCatalog, FPrimaryAssetId LoreAssetId) const;

	// 본인 누적 수집 개수 조회
	UFUNCTION(BlueprintPure, Category = "MD|Lore")
	int32 GetDiscoveredLoreCount() const;

	// 다른 참가자와 무관한 개인 보유 여부 조회
	UFUNCTION(BlueprintPure, Category = "MD|Lore")
	bool HasDiscoveredLore(FPrimaryAssetId LoreAssetId) const;

	// 본인의 카탈로그 수집 완료 여부 조회
	UFUNCTION(BlueprintPure, Category = "MD|Lore")
	bool IsCatalogComplete(const UMD_LoreCatalog* LoreCatalog) const;

	// 개인 저장 원장에서 현재 발견 목록 조회
	TArray<FPrimaryAssetId> GetDiscoveredLoreAssetIds() const;

	// 현재 로컬 사용자에 대응하는 개인 원장 조회
	UMD_LoreProfileSubsystem* GetLocalProfile() const;
};
