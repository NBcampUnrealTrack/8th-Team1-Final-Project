#pragma once

#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "MD_LoreProfileSubsystem.generated.h"

DECLARE_MULTICAST_DELEGATE(FMD_OnPersonalLoreChanged);

// 로컬 사용자별 기록과 저장 파일의 수명 관리
UCLASS()
class MARINEDEMOLITION_API UMD_LoreProfileSubsystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:
	virtual void Deinitialize() override;

public:
	// 개인 기록 로드와 새 발견 이후 화면 갱신 알림
	FMD_OnPersonalLoreChanged OnPersonalLoreChanged;

protected:
	// 맵 이동과 컨트롤러 교체에도 유지하는 개인 발견 목록
	UPROPERTY()
	TArray<FPrimaryAssetId> DiscoveredLoreAssetIds;

	// 개인 파일 로드 전에 수신한 팀 습득의 맵 이동 간 보존
	UPROPERTY()
	TArray<FPrimaryAssetId> PendingLoreAssetIds;

	// 온라인 계정 또는 오프라인 로컬 사용자에 대응하는 고정 슬롯
	FString SaveSlotName;

	// 한 로컬 사용자 수명 동안 중복 로드 방지
	bool bLoaded = false;

	// 저장 실패 후 재시도할 메모리 변경 여부
	bool bDirty = false;

	// 손상되거나 지원하지 않는 기존 파일의 덮어쓰기 차단
	bool bCanSave = true;

public:
	// 로그인 정보가 준비된 개인 기록의 최초 복원
	bool EnsureLoaded();

	// 본인 또는 팀원이 새로 습득한 기록의 중복 없는 개인 저장
	bool RecordLore(FPrimaryAssetId LoreAssetId);

	// 실패한 저장을 포함한 미반영 기록의 파일 반영
	bool SavePending();

	// 개인 저장 복원이 완료된 상태 조회
	FORCEINLINE bool IsLoaded() { return bLoaded; }

	// 전체 목록 복사 없이 개인 기록의 보유 여부 확인
	FORCEINLINE bool HasDiscoveredLore(FPrimaryAssetId LoreAssetId) { return DiscoveredLoreAssetIds.Contains(LoreAssetId); }

	// 개인 코덱스에 사용할 기록 조회
	FORCEINLINE TArray<FPrimaryAssetId> GetDiscoveredLoreAssetIds() { return DiscoveredLoreAssetIds; }
};
