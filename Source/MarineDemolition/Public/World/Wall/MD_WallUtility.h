#pragma once

#include "CoreMinimal.h"
#include "Gameplay/Interaction/MD_Interactable.h"
#include "Gameplay/Scanning/MD_Scannable.h"
#include "GameplayTagAssetInterface.h"
#include "Gameplay/Scanning/MD_ScanTypes.h"
#include "MD_WallUtility.generated.h"

class UBoxComponent;
class UStaticMeshComponent;
class AMD_CuttableWall;

UENUM(BlueprintType)
enum class EMD_UtilityState : uint8
{
	Intact,
	Broken,
	Repaired
};

// 벽 속 인프라(수도관·배선)의 공통 베이스. 겹친 벽이 철거되면 Broken으로 전이한다.
// 절단이 벽 액터 통째라, LinkTrigger로 겹치는 CuttableWall을 찾아 철거 델리게이트를 구독한다.
// 인프라 메시는 벽이 사라져도 남아 수리 대상이 된다.
UCLASS(Abstract)
class MARINEDEMOLITION_API AMD_WallUtility : public AActor, public IMD_Interactable,
	public IGameplayTagAssetInterface, public IMD_Scannable
{
	GENERATED_BODY()

public:
	AMD_WallUtility();

	void SetQuestHighlighted(AActor* Source, bool bHighlighted);

	void StopScanFor(AActor* Scanner);

	// Scan actual infrastructure independently of physics-based host-wall links.
	UStaticMeshComponent* GetScanMesh() const { return Mesh.Get(); }
	virtual void GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const override;
	virtual void OnScanned_Implementation(float Duration, AActor* Scanner) override;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintPure, Category = "MD|Utility")
	EMD_UtilityState GetUtilityState() const { return UtilityState; }

	/**
	 * 치트·디버그용. 장착 도구 검사를 건너뛰고 파손 상태를 곧바로 수리로 넘긴다.
	 * 정상 경로는 IMD_Interactable::Interact 이며 그쪽 규칙(RequiredRepairToolTag)은 그대로다.
	 * 파손 상태가 아니면 아무 일도 하지 않는다 — SetRepaired 가 그 조건을 이미 검사한다.
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "MD|Utility|Debug")
	void DebugForceRepair(AActor* Repairer);

	/**
	 * 발사체 등 외부 시스템의 원격 수리 진입점 (폼건 착탄 밀봉).
	 * Interact 경로는 "현재 장착 도구"를 검사하지만, 발사체는 착탄 시점에 장착
	 * 상태를 보장할 수 없으므로 호출측이 도구 태그를 직접 증명한다.
	 * 파손 상태 + 태그 일치일 때만 수리하고 성공 시 true. 서버 전용
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "MD|Utility")
	bool TryExternalRepair(AActor* Repairer, FGameplayTag RepairToolTag);

	FVector GetBreakLocation() const { return BreakLocation; }
	FVector GetBreakNormal() const { return BreakNormal; }

	/**
	 * 장착 도구로 수리한다. 상호작용(F)과 도구 입력(LMB) 양쪽이 공유하는 서버 진입점.
	 * 파손 상태 + 장착 도구 태그 일치 + (소모형이면) 핫바 차감 성공일 때만 수리하고 true. 서버 전용
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "MD|Utility")
	bool TryRepairWithEquippedTool(AActor* Repairer);

	bool RepairRequiresToolInput() const { return bRepairRequiresToolInput; }

	// HUD 조회 전용. E 상호작용을 허용하거나 수리/수량 차감을 실행하지 않는다.
	bool CanRepairWithToolInput(AActor* Repairer) const
	{
		return bRepairRequiresToolInput
			&& UtilityState == EMD_UtilityState::Broken
			&& CanRepairWith(Repairer);
	}

	// IMD_Interactable — 수리는 서버에서 검증
	virtual void SetInteractionHighlighted_Implementation(bool bHighlighted) override;
	virtual bool CanInteract_Implementation(AActor* Interactor) const override;
	virtual bool RequiresServerInteraction_Implementation() const override { return true; }
	virtual void Interact_Implementation(AActor* Interactor) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	// 겹친 벽의 철거 델리게이트가 호출 (서버). 조준선이 메시를 관통했을 때만 파손
	UFUNCTION()
	void HandleHostWallCut(AMD_CuttableWall* Wall, AActor* Cutter);

	// 절단자 시점 조준 레이가 이 인프라 메시를 관통했는지 (파이프 회피 컷 판정)
	// 관통 시 OutHit에 관통 지점/법선을 담는다 (파손 VFX 스폰 지점)
	bool WasCutRayThroughMesh(AActor* Cutter, FHitResult& OutHit) const;

	// 겹친 벽 스캔 시 호출. 스캐너가 로컬 조종 중인 머신에서만 실루엣을 Duration 초간 노출
	UFUNCTION()
	void HandleHostWallScanned(AMD_CuttableWall* Wall, float Duration, AActor* Scanner);

	// 스캔 실루엣 on/off (CustomDepth 렌더 토글). 후처리 머티리얼이 스텐실로 엑스레이 처리
	void SetScanSilhouette(bool bVisible);
	void EndScanSilhouette();

	// 파생 클래스가 실제 사고(분출/소등)를 구현. 서버에서만 호출된다
	virtual void OnBroken(AActor* Cutter) {}

	// 파생 클래스가 수리 결과(봉합/재점등)를 구현. 서버에서만 호출된다
	virtual void OnRepaired(AActor* Repairer) {}

	// 현재 장착 도구의 ToolAbilityTag가 RequiredRepairToolTag와 정확히 일치하는지 확인
	virtual bool CanRepairWith(AActor* Interactor) const;

	// 상태 전이 진입점 (중복 방지 + 복제 + 파생 훅 호출). 서버 전용
	void SetBroken(AActor* Cutter);
	void SetRepaired(AActor* Repairer);

	UFUNCTION()
	void OnRep_UtilityState();

	// BP 연출 훅 (파티클·소리·조명). 각 머신에서 상태 반영 시 호출
	UFUNCTION(BlueprintImplementableEvent, Category = "MD|Utility")
	void OnUtilityStateChangedVisual(EMD_UtilityState NewState);

	/**
	 * 상태가 각 머신에 반영될 때 호출된다 (서버는 직접, 클라이언트는 OnRep 경로).
	 *
	 * 파티클·조명처럼 아트가 잡는 연출은 위 BP 이벤트가 맡고,
	 * 사운드처럼 코드가 소유해야 하는 것은 여기서 처리한다 —
	 * 사운드는 데이터 에셋+태그를 거쳐야 하고(프로젝트 규칙) 그 진입점이 C++ 에만 있다.
	 */
	virtual void OnUtilityStateChangedLocal(EMD_UtilityState NewState) {}

	UPROPERTY(VisibleAnywhere, Category = "MD|Utility")
	TObjectPtr<USceneComponent> Root;

	// 벽 파괴 후에도 남는 인프라 메시 (수리 대상)
	UPROPERTY(VisibleAnywhere, Category = "MD|Utility")
	TObjectPtr<UStaticMeshComponent> Mesh;

	// 겹치는 벽을 찾는 트리거. 벽 두께에 맞춰 배치
	UPROPERTY(VisibleAnywhere, Category = "MD|Utility")
	TObjectPtr<UBoxComponent> LinkTrigger;

	// 이 인프라 수리에 필요한 도구 태그 (파생 클래스에서 지정)
	UPROPERTY(EditDefaultsOnly, Category = "MD|Utility")
	FGameplayTag RequiredRepairToolTag;

	// 상호작용 수리 성공 시 장착 도구 1개를 소모할지 (절연테이프 = 1회용, 기획 08 §4.1)
	UPROPERTY(EditDefaultsOnly, Category = "MD|Utility")
	bool bConsumesRepairTool = false;

	// true면 상호작용 키(F)로는 수리하지 않고 도구 입력(LMB)으로만 수리한다 (절연테이프)
	UPROPERTY(EditDefaultsOnly, Category = "MD|Utility")
	bool bRepairRequiresToolInput = false;

	// 컷 조준 레이가 이 인프라 메시를 관통했는지 검사할 최대 거리
	// (절단자 시점에서 재구성. 벽 두께만큼 여유를 둔다)
	UPROPERTY(EditDefaultsOnly, Category = "MD|Utility")
	float CutRayDistance = 400.f;

	// 스캔 실루엣용 CustomDepth 스텐실 값. 후처리(PP_Highlight) 엑스레이 분기와 맞춰야 함
	// (상호작용 하이라이트 250과 구분되는 별도 값)
	UPROPERTY(EditDefaultsOnly, Category = "MD|Utility", meta = (ClampMin = "0", ClampMax = "255"))
	int32 ScanSilhouetteStencil = 251;

	// 이 설비가 겹친 파괴 가능 벽을 찾아 철거/스캔 델리게이트를 구독할지.
	// 독립 설비(예: Day3 냉각 펌프)는 false로 두어 벽 철거 시스템과 완전히 분리한다.
	UPROPERTY(EditDefaultsOnly, Category = "MD|Utility")
	bool bLinkHostWalls = true;

	// 레벨 시작 시 이미 파손된 상태로 시작할지. 벽 없이 인프라만 고장난 상태를 표현한다
	// (감점 없음, 절단 원샷 연출 없음 — BP 연출도 이 값으로 분기)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Utility")
	bool bStartBroken = false;

	// 수리 완료 시 잠금을 해제할 문 액터. BP_Door의 bLocked를 false로 세팅한다.
	// 레벨에서 WaterPipe/PowerConduit ↔ BP_Door를 연결하는 게이트 메커니즘
	UPROPERTY(EditAnywhere, Category = "MD|Utility|Gate")
	TSoftObjectPtr<AActor> LinkedGateActor;

	// 시작 파손 지점 (액터 로컬). 미지정(0)이면 메시 중심
	UPROPERTY(EditAnywhere, Category = "MD|Utility", meta = (EditCondition = "bStartBroken"))
	FVector StartBreakLocalOffset = FVector::ZeroVector;

	// 시작 분출 방향 (액터 로컬, VFX +X=분출 규약). 미지정(0)이면 액터 전방
	UPROPERTY(EditAnywhere, Category = "MD|Utility", meta = (EditCondition = "bStartBroken"))
	FVector StartBreakLocalNormal = FVector::ForwardVector;

	UPROPERTY(ReplicatedUsing = OnRep_UtilityState)
	EMD_UtilityState UtilityState = EMD_UtilityState::Intact;

	// 절단 레이가 메시를 관통한 지점 (서버가 파손 시 기록, BP가 파손 VFX 스폰 위치로 사용)
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "MD|Utility")
	FVector_NetQuantize BreakLocation = FVector_NetQuantize(ForceInitToZero);

	// 관통 지점의 표면 법선 (VFX +X=분출 규약에 맞춰 MakeRotFromX으로 회전 구성)
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "MD|Utility")
	FVector_NetQuantizeNormal BreakNormal = FVector_NetQuantizeNormal(0.f, 0.f, 1.f);

	// 수리 완료 시 LinkedGateActor의 bLocked를 false로 해제 (서버)
	void UnlockLinkedGate();

private:
	// LinkTrigger와 겹치는 CuttableWall을 찾아 철거 델리게이트 구독 (서버)
	void LinkOverlappingWalls();

	// 구독한 벽들 (중복 트리거 방지·해제용)
	UPROPERTY(Transient)
	TArray<TObjectPtr<AMD_CuttableWall>> LinkedWalls;

	// 스캔 실루엣 지속시간 타이머
	FTimerHandle ScanSilhouetteTimer;
	TWeakObjectPtr<AActor> ActiveScanner;
	FMD_ScanMeshState HighlightState;
	bool bScanHighlighted = false;
	bool bInteractionHighlighted = false;
	bool bQuestHighlighted = false;
	TSet<TWeakObjectPtr<AActor>> QuestHighlightSources;
	void RefreshHighlight();
};
