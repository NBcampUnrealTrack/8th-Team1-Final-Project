#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
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

	/**
	 * 발사 원점과 방향. 원점은 도구 메시의 총구 소켓(MuzzleSocketName), 방향은 총구에서
	 * 카메라 조준점(시점 트레이스 착점)을 향한다 — 궤적 프리뷰와 서버 스폰이 같은 산식을 쓴다.
	 * 소켓이 없으면 옛 방식(시점 + MuzzleForwardOffset)으로 폴백한다.
	 */
	void ComputeLaunch(FVector& OutOrigin, FVector& OutDirection) const;

	/**
	 * 발사 요청. 로컬 플레이어가 보는 총구 원점·방향을 서버로 보내고, 서버가 충전 소모·스폰·발사음 중계를 한다.
	 * 서버는 1인칭 도구 메시를 모르므로 클라이언트 값이 필요하다 — 호스트는 즉시 로컬 실행.
	 * 값이 서버 시점 기준 허용 범위를 벗어나면 서버 계산값으로 대체한다.
	 */
	UFUNCTION(Server, Reliable)
	void ServerFire(FVector_NetQuantize Origin, FVector_NetQuantizeNormal Direction, FGameplayTag FireSoundTag);

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

	// 폼건 스태틱 메시의 총구 소켓. 궤적 프리뷰·발사체가 여기서 시작한다
	UPROPERTY(EditDefaultsOnly, Category = "MD|FoamGun|Ballistics")
	FName MuzzleSocketName = TEXT("VFX_Muzzle");

	// 카메라 조준점 트레이스 거리(cm). 아무것도 안 맞으면 이 거리의 점을 조준점으로 쓴다
	UPROPERTY(EditDefaultsOnly, Category = "MD|FoamGun|Ballistics", meta = (ClampMin = "100.0"))
	float AimTraceRange = 10000.f;

	// 클라이언트가 보낸 발사 원점이 서버 시점에서 이 거리(cm) 안이어야 신뢰한다
	UPROPERTY(EditDefaultsOnly, Category = "MD|FoamGun|Ballistics", meta = (ClampMin = "0.0"))
	float MaxClientOriginDistance = 250.f;

	// 클라이언트가 보낸 발사 방향이 서버 조준 방향과 이 각도(도) 안이어야 신뢰한다
	UPROPERTY(EditDefaultsOnly, Category = "MD|FoamGun|Ballistics", meta = (ClampMin = "0.0", ClampMax = "90.0"))
	float MaxClientAimAngle = 30.f;

	// 총구 소켓이 없을 때의 폴백: 시점에서 발사 원점까지 전방 오프셋(cm)
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

	// 시점 위치·방향. 로컬 플레이어는 실제 카메라, 서버·타인은 복제된 BaseAimRotation 기준
	void GetViewPoint(FVector& OutLocation, FVector& OutDirection) const;

	// 시점 트레이스 착점. 총구에서 이 점을 향해 쏘면 조준선(화면 중앙)과 탄도가 맞는다
	FVector ComputeAimTarget(const FVector& ViewLocation, const FVector& ViewDirection) const;

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
