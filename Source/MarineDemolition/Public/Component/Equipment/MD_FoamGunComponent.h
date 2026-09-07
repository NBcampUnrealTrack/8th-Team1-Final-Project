#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MD_FoamGunComponent.generated.h"

class AMD_FoamProjectile;
class UNiagaraComponent;
class UNiagaraSystem;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMD_OnFoamChargeChanged, int32, NewCharge);

/**
 * 폼건 상태·탄도 정본 (기획 08 §4.2).
 *
 * 세 가지를 한곳에서 관리한다:
 *  1) 충전 게이지 — 만충 100, 사격 1회 −25, 카트리지 +25. 서버가 바꾸고 복제로 퍼진다.
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

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
	                           FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION(BlueprintPure, Category = "MD|FoamGun")
	int32 GetFoamCharge() const { return FoamCharge; }

	UFUNCTION(BlueprintPure, Category = "MD|FoamGun")
	bool HasChargeForShot() const { return FoamCharge >= ShotCost; }

	// 만충이면 상인이 카트리지 판매를 거절한다 (기획 §4.3)
	UFUNCTION(BlueprintPure, Category = "MD|FoamGun")
	bool IsChargeFull() const { return FoamCharge >= MaxCharge; }

	// 사격 1회분 차감. 성공 시 true. 서버에서만 호출
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "MD|FoamGun")
	bool TryConsumeShotCharge();

	// 카트리지 충전(+25)용. 상점/보관함이 호출한다. 서버 전용
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "MD|FoamGun")
	void AddCharge(int32 Amount);

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

	// 만충 게이지. 기획 정본 100
	UPROPERTY(EditDefaultsOnly, Category = "MD|FoamGun", meta = (ClampMin = "1"))
	int32 MaxCharge = 100;

	// 사격 1회 소모량. 기획 정본 25 (4회분)
	UPROPERTY(EditDefaultsOnly, Category = "MD|FoamGun", meta = (ClampMin = "1"))
	int32 ShotCost = 25;

private:
	UFUNCTION()
	void OnRep_FoamCharge();

	// 폼건을 들고 있는 동안만 프리뷰를 유지한다
	bool ShouldShowTrajectory() const;
	void UpdateTrajectoryPreview();
	void DestroyTrajectoryPreview();

	// 현재 충전량. 서버가 바꾸고 복제로 퍼진다.
	// 신품은 충전 0 — 만충 비용 = 카트리지 4개 220₡가 기획 전제 (08 §4.3).
	// 테스트·스타트킷 지급분은 에디터에서 초기값을 올려 쓴다
	UPROPERTY(EditAnywhere, ReplicatedUsing = OnRep_FoamCharge, Category = "MD|FoamGun",
		meta = (ClampMin = "0", AllowPrivateAccess = "true"))
	int32 FoamCharge = 0;

	// 소유 클라이언트 전용 궤적 프리뷰 컴포넌트
	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> TrajectoryPreview;

	// RMB를 누르고 있는 동안만 true. 복제하지 않는다 — 조준 UI는 본인 전용
	bool bAimHeld = false;
};
