#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MD_FoamGunComponent.generated.h"

class AMD_FoamProjectile;
class UMD_EquipHotbarComponent;
class UMD_ToolDefinition;
class UNiagaraComponent;
class UNiagaraSystem;

// 소수 충전량을 포함한 Hotbar 잔량 변경 전달
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMD_OnFoamChargeChanged, float, NewCharge);

/**
 * 폼건 상태·탄도 정본 (기획 08 §4.2).
 *
 * 세 가지를 한곳에서 관리한다:
 *  1) 충전 게이지 — ToolDefinition 최대값 및 Hotbar에 적용된 사용 비용과 잔량 공유.
 *  2) 탄도 수치 정본 — 발사속도·중력 배율을 궤적 예측과 실발사체가 공유해야
 *     예측선과 실제 탄도가 일치한다 (기획 §4.2의 변수 공유 요구).
 *  3) 조준 궤적 프리뷰 — 폼건을 든 동안 소유 클라이언트에서만 NS_Foam_Trajectory를
 *     띄우고 매 틱 예측 결과를 유저 파라미터로 넘긴다.
 */
UCLASS(ClassGroup = (MD), meta = (BlueprintSpawnableComponent))
class MARINEDEMOLITION_API UMD_FoamGunComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMD_FoamGunComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
	                           FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// 실제 사격과 충전물이 공유하는 Hotbar 잔량 조회
	UFUNCTION(BlueprintPure, Category = "MD|FoamGun")
	float GetFoamCharge() const;

	// 장착한 도구 정의의 최대 충전량 조회
	UFUNCTION(BlueprintPure, Category = "MD|FoamGun")
	float GetMaxCharge();

	// 장착한 폼건 정의의 최대 충전량을 기준으로 충전 사용 여부 확인
	UFUNCTION(BlueprintPure, Category = "MD|FoamGun")
	bool IsChargeRequired() const;

	// 충전을 사용하지 않는 폼건의 잔량 제한 없는 사격 허용
	UFUNCTION(BlueprintPure, Category = "MD|FoamGun")
	bool HasChargeForShot() const;

	// 충전 불필요 또는 만충 상태에서 카트리지 추가 보충 차단
	UFUNCTION(BlueprintPure, Category = "MD|FoamGun")
	bool IsChargeFull() const;

	// 사격 한 번의 사용 비용을 서버에서 차감한 결과 전달
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "MD|FoamGun")
	bool TryConsumeShotCharge();

	// BP의 보충 요청을 공통 Hotbar의 충전 상한과 서버 처리에 위임
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "MD|FoamGun")
	void AddCharge(float Amount);

	// 아이템 지급 시 Hotbar 슬롯에 설정된 한 번의 사용 비용 조회
	UFUNCTION(BlueprintPure, Category = "MD|FoamGun")
	float GetShotCost() const;

	// 발사 원점. 조준선과 탄도가 어긋나지 않도록 카메라 시점 기준으로 계산한다 (서버·클라 동일 산식)
	FVector GetLaunchOrigin() const;

	// 조준 방향. BaseAimRotation은 복제되므로 서버에서도 같은 값이 나온다
	FVector GetAimDirection() const;

	float GetLaunchSpeed() const { return LaunchSpeed; }
	float GetProjectileGravityScale() const { return ProjectileGravityScale; }
	TSubclassOf<AMD_FoamProjectile> GetProjectileClass() const { return ProjectileClass; }

	// RMB 조준 여부. 눌린 동안만 궤적 프리뷰를 그린다 (소유 클라이언트 로컬 상태)
	void SetAimHeld(bool bHeld) { bAimHeld = bHeld; }
	bool IsAimHeld() const { return bAimHeld; }

	// 충전 변경 방송. HUD 게이지가 구독한다
	UPROPERTY(BlueprintAssignable, Category = "MD|FoamGun")
	FMD_OnFoamChargeChanged OnFoamChargeChanged;

protected:
	// 스폰될 발사체 클래스. BP_FoamProjectile을 에디터에서 지정
	UPROPERTY(EditDefaultsOnly, Category = "MD|FoamGun")
	TSubclassOf<AMD_FoamProjectile> ProjectileClass;

	// 조준 궤적 나이아가라 (NS_Foam_Trajectory). 비우면 프리뷰 없이 동작
	UPROPERTY(EditDefaultsOnly, Category = "MD|FoamGun|Trajectory")
	TObjectPtr<UNiagaraSystem> TrajectorySystem;

	// 발사 속도(cm/s). 궤적 예측과 발사체가 이 값 하나를 공유한다
	UPROPERTY(EditDefaultsOnly, Category = "MD|FoamGun|Ballistics", meta = (ClampMin = "100.0"))
	float LaunchSpeed = 1200.f;

	// 발사체 중력 배율 (ProjectileMovement GravityScale과 동일 의미)
	UPROPERTY(EditDefaultsOnly, Category = "MD|FoamGun|Ballistics", meta = (ClampMin = "0.0"))
	float ProjectileGravityScale = 1.0f;

	// 예측 트레이스에 쓰는 발사체 반경(cm)
	UPROPERTY(EditDefaultsOnly, Category = "MD|FoamGun|Ballistics", meta = (ClampMin = "0.0"))
	float ProjectileRadius = 8.f;

	// 궤적 예측 최대 시뮬 시간(초). 이 시간만큼 날아간 지점까지만 예측선을 그린다
	UPROPERTY(EditDefaultsOnly, Category = "MD|FoamGun|Trajectory", meta = (ClampMin = "0.5"))
	float PredictMaxSimTime = 2.f;

	// 착탄 표식을 표면에서 띄우는 거리(cm). 벽 파묻힘 방지
	UPROPERTY(EditDefaultsOnly, Category = "MD|FoamGun|Trajectory", meta = (ClampMin = "0.0"))
	float ImpactMarkerOffset = 8.f;

	// 시점에서 발사 원점까지 전방 오프셋(cm). 1인칭 화면에서 궤적 시작부가 얼굴을 가리지 않게 띄운다
	UPROPERTY(EditDefaultsOnly, Category = "MD|FoamGun|Ballistics", meta = (ClampMin = "0.0"))
	float MuzzleForwardOffset = 40.f;

	// 공통 충전 잔량 및 비용을 보관하는 소유자 Hotbar 조회
	UMD_EquipHotbarComponent* GetHotbar() const;

	// 실제 사격 대상인 현재 장착 폼건의 정의 조회
	UMD_ToolDefinition* GetEquippedFoamTool() const;

private:
	// Hotbar 복제 및 서버 변경 알림을 기존 폼건 UI 이벤트로 전달
	UFUNCTION()
	void HandleHotbarChanged();

	// 폼건을 들고 있는 동안만 프리뷰를 유지한다
	bool ShouldShowTrajectory() const;
	void UpdateTrajectoryPreview();
	void DestroyTrajectoryPreview();

	// 소유 클라이언트 전용 궤적 프리뷰 컴포넌트
	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> TrajectoryPreview;

	// RMB를 누르고 있는 동안만 true. 복제하지 않는다 — 조준 UI는 본인 전용
	bool bAimHeld = false;
};
