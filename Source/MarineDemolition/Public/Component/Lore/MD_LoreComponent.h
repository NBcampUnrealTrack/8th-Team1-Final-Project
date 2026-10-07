#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Data/Lore/MD_LoreCatalog.h"
#include "MD_LoreComponent.generated.h"

class UMD_LoreProfileSubsystem;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMD_OnLoreProgressUpdated);

// 기존 GameState 접근 경로에 로컬 개인 기록을 제공하는 UI 원장
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class MARINEDEMOLITION_API UMD_LoreComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMD_LoreComponent();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	// 개인 수집과 저장 복원 이후 로컬 화면 갱신 알림
	UPROPERTY(BlueprintAssignable, Category = "MD|Lore")
	FMD_OnLoreProgressUpdated OnLoreProgressUpdated;

protected:
	// 다른 참가자에게 복제하지 않는 로컬 사용자의 발견 목록
	UPROPERTY()
	TArray<FPrimaryAssetId> DiscoveredLoreAssetIds;

	// 맵 이동과 독립된 개인 저장 원장
	UPROPERTY()
	TObjectPtr<UMD_LoreProfileSubsystem> LocalProfile;

	// 이전 월드의 구독 해제용 핸들
	FDelegateHandle ProfileChangedHandle;

	// 로그인 준비와 파일 반영 실패의 재시도 주기
	FTimerHandle ProfileRetryHandle;

public:
	// 본인 발견 목록과 선행 조건에 따른 본문 공개 상태 조회
	UFUNCTION(BlueprintPure, Category = "MD|Lore")
	EMD_LoreEntryState GetLoreState(const UMD_LoreCatalog* LoreCatalog, FPrimaryAssetId LoreAssetId) const;

	// 해당 사용자의 개인 기록 보유 여부 조회
	UFUNCTION(BlueprintPure, Category = "MD|Lore")
	bool HasDiscoveredLore(FPrimaryAssetId LoreAssetId) const;

	// 기존 코덱스에 전달할 개인 발견 목록 조회
	UFUNCTION(BlueprintPure, Category = "MD|Lore")
	FORCEINLINE TArray<FPrimaryAssetId> GetDiscoveredLoreAssetIds() const { return DiscoveredLoreAssetIds; }

protected:
	// 로컬 사용자 준비와 미저장 발견 기록의 주기적 재시도
	void UpdateLocalProfile();

	// 개인 기록 변화에 따른 화면용 목록 동기화
	void RefreshLocalProfile();
};
