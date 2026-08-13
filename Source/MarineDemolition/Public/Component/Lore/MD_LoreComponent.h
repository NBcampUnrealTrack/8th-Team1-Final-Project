#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Data/Lore/MD_LoreCatalog.h"
#include "Types/Lore/MD_LoreTypes.h"
#include "MD_LoreComponent.generated.h"

class UMD_LoreSubsystem;

/** 복제된 로어 진행 상태가 갱신되었음을 UI와 월드 표현에 알리는 이벤트 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMD_OnLoreProgressUpdated);

/** 로어 관리 GameState 컴포넌트 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class MARINEDEMOLITION_API UMD_LoreComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMD_LoreComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	/** 리슨 서버와 클라이언트가 동일한 갱신 시점에 반응하기 위한 이벤트 */
	UPROPERTY(BlueprintAssignable, Category = "MD|Lore")
	FMD_OnLoreProgressUpdated OnLoreProgressUpdated;

protected:
	/** 코덱스와 월드 수집물 표현에 제공하는 복제된 현장별 진행 목록 */
	UPROPERTY(ReplicatedUsing = OnRep_LoreProgress)
	TArray<FMD_LoreSiteProgress> SiteProgressList;

	/** 컴포넌트 종료 시 정확한 서버 원장 이벤트를 해제하기 위한 구독 핸들 */
	FDelegateHandle SubsystemChangedHandle;

public:
	/** 현장의 진행 상태를 조회하는 함수 */
	UFUNCTION(BlueprintPure, Category = "MD|Lore")
	bool GetSiteProgress(FName SiteId, FMD_LoreSiteProgress& OutSiteProgress) const;

	/** 복제된 발견 목록과 카탈로그 선행 조건으로 공개 상태를 계산하는 함수 */
	UFUNCTION(BlueprintPure, Category = "MD|Lore")
	EMD_LoreEntryState GetLoreState(const UMD_LoreCatalog* LoreCatalog, FName LoreId) const;

	/** 서버의 원장에서 지정한 로어의 최초 발견 여부를 확인하는 함수 */
	UFUNCTION(BlueprintPure, Category = "MD|Lore")
	bool HasDiscoveredLore(FName SiteId, FName LoreId) const;

protected:
	/** 서버 원장 전체를 복제 배열로 복사하고 즉시 전파하는 함수 */
	void RefreshFromSubsystem();

	/** 현재 게임 인스턴스의 로어 원장을 조회하는 함수 */
	UMD_LoreSubsystem* GetLoreSubsystem() const;

	/** 클라이언트 복제 완료 이후 로어 UI 갱신을 알리는 함수 */
	UFUNCTION()
	void OnRep_LoreProgress();
};
