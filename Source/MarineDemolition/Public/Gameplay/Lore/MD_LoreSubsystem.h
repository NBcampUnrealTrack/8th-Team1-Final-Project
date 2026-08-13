#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Data/Lore/MD_LoreCatalog.h"
#include "Types/Lore/MD_LoreTypes.h"
#include "MD_LoreSubsystem.generated.h"

/** 서버 로어 원장 변경을 복제 미러와 저장 시스템에 알리는 이벤트 */
DECLARE_MULTICAST_DELEGATE(FMD_OnLoreProgressChanged);

/** 계약과 맵 이동을 넘어 영구 누적되는 팀 공유 로어 진행의 서버 권위 관리자 */
UCLASS()
class MARINEDEMOLITION_API UMD_LoreSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** 로어 발견 또는 저장 복원 이후 복제 미러 갱신을 요청하는 이벤트 */
	FMD_OnLoreProgressChanged OnLoreProgressChanged;

protected:
	/** 모든 현장의 발견 기록과 완료 보상 지급 여부를 보관하는 서버 원장 */
	UPROPERTY()
	TArray<FMD_LoreSiteProgress> SiteProgressList;

public:
	/** 기록물의 발견 사실을 기록하고 개별·완성 크레딧을 지급하는 함수 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "MD|Lore")
	bool TryDiscoverLore(const UMD_LoreCatalog* LoreCatalog, FName LoreId);

	/** 현재 발견 목록과 카탈로그 선행 조건으로 코덱스 공개 상태를 계산하는 함수 */
	UFUNCTION(BlueprintPure, Category = "MD|Lore")
	EMD_LoreEntryState GetLoreState(const UMD_LoreCatalog* LoreCatalog, FName LoreId) const;

	/** 지정한 현장의 발견 기록과 완료 보상 상태를 복사하는 조회 함수 */
	UFUNCTION(BlueprintPure, Category = "MD|Lore")
	bool GetSiteProgress(FName SiteId, FMD_LoreSiteProgress& OutSiteProgress) const;

	/** 지정한 현장의 현재 발견 로어 개수를 반환하는 조회 함수 */
	UFUNCTION(BlueprintPure, Category = "MD|Lore")
	int32 GetDiscoveredLoreCount(FName SiteId) const;

	/** 지정한 현장과 로어 식별자가 서버 원장에 최초 발견으로 기록되었는지 확인하는 함수 */
	UFUNCTION(BlueprintPure, Category = "MD|Lore")
	bool HasDiscoveredLore(FName SiteId, FName LoreId) const;

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
	const TArray<FMD_LoreSiteProgress>& GetSiteProgressList() const;

protected:
	/** 클라이언트가 로컬 원장을 변경하지 못하도록 현재 실행 권한을 확인하는 함수 */
	bool HasServerAuthority() const;

	/** 최초 발견을 기록할 현장 진행 상태를 조회하거나 생성하는 함수 */
	FMD_LoreSiteProgress* FindOrAddSiteProgress(FName SiteId);

	/** 기존 상태를 변경하지 않고 현장 진행 상태를 조회하는 함수 */
	const FMD_LoreSiteProgress* FindSiteProgress(FName SiteId) const;

	/** 새 발견을 포함했을 때 카탈로그 전체가 완성되는지 확인하는 함수 */
	bool WillCompleteCatalog(const UMD_LoreCatalog* LoreCatalog, const FMD_LoreSiteProgress& SiteProgress, FName NewLoreId) const;
};
