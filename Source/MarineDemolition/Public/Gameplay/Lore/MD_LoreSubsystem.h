#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Data/Lore/MD_LoreCatalog.h"
#include "Types/Lore/MD_LoreTypes.h"
#include "MD_LoreSubsystem.generated.h"

struct FMD_GameData;

class UMD_LoreDefinition;

/** 서버 로어 원장 변경을 복제 미러와 저장 시스템에 알리는 이벤트 */
DECLARE_MULTICAST_DELEGATE(FMD_OnLoreProgressChanged);

/** 계약과 맵 이동을 넘어 영구 누적되는 팀 공유 로어 진행의 서버 권위 관리자 */
UCLASS()
class MARINEDEMOLITION_API UMD_LoreSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;

    virtual void Deinitialize() override;

public:
	/** 로어 발견 또는 저장 복원 이후 복제 미러 갱신을 요청하는 이벤트 */
	FMD_OnLoreProgressChanged OnLoreProgressChanged;

protected:
	/** 모든 카탈로그가 공유하는 전역 로어 발견 원장 */
	UPROPERTY()
	TArray<FPrimaryAssetId> DiscoveredLoreAssetIds;

public:
	/** 기록물의 최초 발견 사실을 서버 원장에 기록하는 함수 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "MD|Lore")
	bool TryDiscoverLore(FPrimaryAssetId LoreAssetId);

	/** Primary Asset ID에 할당된 로어 정의 에셋을 조회하는 함수 */
	UFUNCTION(BlueprintPure, Category = "MD|Lore")
	UMD_LoreDefinition* GetLoreDefinition(FPrimaryAssetId LoreAssetId) const;

	/** 현재 발견 목록과 카탈로그 선행 조건으로 코덱스 공개 상태를 계산하는 함수 */
	UFUNCTION(BlueprintPure, Category = "MD|Lore")
	EMD_LoreEntryState GetLoreState(const UMD_LoreCatalog* LoreCatalog, FPrimaryAssetId LoreAssetId) const;

	/** 전역 원장에 기록된 로어 개수를 반환하는 조회 함수 */
	UFUNCTION(BlueprintPure, Category = "MD|Lore")
	FORCEINLINE int32 GetDiscoveredLoreCount() const { return DiscoveredLoreAssetIds.Num(); }

	/** 로어 식별자가 서버 원장에 최초 발견으로 기록되었는지 확인하는 함수 */
	UFUNCTION(BlueprintPure, Category = "MD|Lore")
	bool HasDiscoveredLore(FPrimaryAssetId LoreAssetId) const;

	/** 지정한 카탈로그의 모든 기록물이 발견되었는지 확인하는 조회 함수 */
	UFUNCTION(BlueprintPure, Category = "MD|Lore")
	bool IsCatalogComplete(const UMD_LoreCatalog* LoreCatalog) const;

	/** 현재 서버 로어 원장을 저장 필드로 복사하는 함수 */
	FMD_LoreData GetLoreSaveData() const;

	/** 저장 필드를 변경하지 않고 서버 권한과 구조를 확인하는 함수 */
	bool CanApplyLoreSaveData(const FMD_LoreData& SaveData) const;

	/** 검증된 저장 필드로 서버 로어 원장을 교체하는 함수 */
	bool ApplyLoreSaveData(const FMD_LoreData& SaveData);

	/** 복제 미러가 서버 원장 전체를 동일한 순서로 복사하기 위한 조회 함수 */
	FORCEINLINE const TArray<FPrimaryAssetId>& GetDiscoveredLoreAssetIds() const { return DiscoveredLoreAssetIds; }

protected:
	/** 클라이언트가 로컬 원장을 변경하지 못하도록 현재 실행 권한을 확인하는 함수 */
	bool HasServerAuthority() const;

protected:
    // 소유한 기능 상태를 저장 이벤트의 구조체에 기록하는 함수
    UFUNCTION()
    void CollectGameData(UPARAM(ref) FMD_GameData& GameData, UPARAM(ref) bool& bWasSuccessful);

    // 전달된 저장 데이터에서 담당 태그의 필드를 찾아 직접 적용하는 함수
    UFUNCTION()
    void OverwriteGameData(const FMD_GameData& GameData, UPARAM(ref) bool& bWasSuccessful);

};
