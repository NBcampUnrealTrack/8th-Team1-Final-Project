#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "Gameplay/Interaction/MD_Interactable.h"
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
class MARINEDEMOLITION_API AMD_WallUtility : public AActor, public IMD_Interactable
{
	GENERATED_BODY()

public:
	AMD_WallUtility();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintPure, Category = "MD|Utility")
	EMD_UtilityState GetUtilityState() const { return UtilityState; }

	// IMD_Interactable — 수리는 서버에서 검증
	virtual void SetInteractionHighlighted_Implementation(bool bHighlighted) override;
	virtual bool RequiresServerInteraction_Implementation() const override { return true; }
	virtual void Interact_Implementation(AActor* Interactor) override;

protected:
	virtual void BeginPlay() override;

	// 겹친 벽의 철거 델리게이트가 호출 (서버). 조준선이 메시를 관통했을 때만 파손
	UFUNCTION()
	void HandleHostWallCut(AMD_CuttableWall* Wall, AActor* Cutter);

	// 절단자 시점 조준 레이가 이 인프라 메시를 관통했는지 (파이프 회피 컷 판정)
	bool WasCutRayThroughMesh(AActor* Cutter) const;

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

	// 이 인프라를 수리할 수 있는 도구를 Interactor가 들고 있는지
	// TODO: 장비/핫바 시스템과 연결해 RequiredRepairToolTag 소지 여부 확인 (폼건/절연테이프)
	virtual bool CanRepairWith(AActor* Interactor) const;

	// 상태 전이 진입점 (중복 방지 + 복제 + 파생 훅 호출). 서버 전용
	void SetBroken(AActor* Cutter);
	void SetRepaired(AActor* Repairer);

	UFUNCTION()
	void OnRep_UtilityState();

	// BP 연출 훅 (파티클·소리·조명). 각 머신에서 상태 반영 시 호출
	UFUNCTION(BlueprintImplementableEvent, Category = "MD|Utility")
	void OnUtilityStateChangedVisual(EMD_UtilityState NewState);

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

	// 컷 조준 레이가 이 인프라 메시를 관통했는지 검사할 최대 거리
	// (절단자 시점에서 재구성. 벽 두께만큼 여유를 둔다)
	UPROPERTY(EditDefaultsOnly, Category = "MD|Utility")
	float CutRayDistance = 400.f;

	// 스캔 실루엣용 CustomDepth 스텐실 값. 후처리(PP_Highlight) 엑스레이 분기와 맞춰야 함
	// (상호작용 하이라이트 250과 구분되는 별도 값)
	UPROPERTY(EditDefaultsOnly, Category = "MD|Utility", meta = (ClampMin = "0", ClampMax = "255"))
	int32 ScanSilhouetteStencil = 251;

	UPROPERTY(ReplicatedUsing = OnRep_UtilityState)
	EMD_UtilityState UtilityState = EMD_UtilityState::Intact;

private:
	// LinkTrigger와 겹치는 CuttableWall을 찾아 철거 델리게이트 구독 (서버)
	void LinkOverlappingWalls();

	// 구독한 벽들 (중복 트리거 방지·해제용)
	UPROPERTY(Transient)
	TArray<TObjectPtr<AMD_CuttableWall>> LinkedWalls;

	// 스캔 실루엣 지속시간 타이머
	FTimerHandle ScanSilhouetteTimer;
};
