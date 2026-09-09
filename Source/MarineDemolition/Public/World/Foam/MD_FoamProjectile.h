#pragma once

#include "CoreMinimal.h"
#include "Engine/NetSerialization.h"
#include "GameFramework/Actor.h"
#include "MD_FoamProjectile.generated.h"

class UNiagaraComponent;
class UNiagaraSystem;
class UProjectileMovementComponent;
class USphereComponent;

/**
 * 폼건 발사체 (기획 08 §4.2).
 *
 * 서버가 스폰·시뮬레이션하고 이동은 복제로 퍼진다. 서버에서 무언가에 맞으면
 * 착탄 반경 안의 파열된 수도관을 밀봉(TryExternalRepair)하고, 밀봉 지점에
 * 폼 플러그를 스폰한 뒤 착탄 이펙트를 멀티캐스트한다.
 *
 * 탄도 수치(속도·중력)는 UMD_FoamGunComponent가 정본이라 여기 두지 않는다 —
 * 발사 시 InitLaunch로 주입받는다.
 */
UCLASS()
class MARINEDEMOLITION_API AMD_FoamProjectile : public AActor
{
	GENERATED_BODY()

public:
	AMD_FoamProjectile();

	// 발사 직후 서버가 호출. 탄도 정본(FoamGunComponent) 값을 주입한다
	void InitLaunch(const FVector& InVelocity, float InGravityScale);

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void HandleHit(UPrimitiveComponent* HitComponent, AActor* OtherActor,
	               UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);

	// 착탄 지점 주변의 파열 수도관을 밀봉 시도. 성공 시 폼 플러그 스폰. 서버 전용
	bool TrySealNearbyUtility(const FHitResult& Hit);

	// 모든 머신에서 착탄 이펙트 재생 (MD_CuttableWall::MulticastPlayDemolishEffect 패턴)
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastPlayImpactFX(FVector_NetQuantize ImpactPoint, FVector_NetQuantizeNormal ImpactNormal);

	// 멀티캐스트 유실 방지를 위해 즉시 파괴 대신 숨김 + 수명 지연 파괴
	void DisableAfterImpact();

	UPROPERTY(VisibleAnywhere, Category = "MD|Foam")
	TObjectPtr<USphereComponent> Collision;

	// 트레일 이펙트(NS_Foam_Projectile). BP에서 에셋 할당
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Foam")
	TObjectPtr<UNiagaraComponent> TrailFX;

	UPROPERTY(VisibleAnywhere, Category = "MD|Foam")
	TObjectPtr<UProjectileMovementComponent> ProjectileMovement;

	// 착탄 원샷 이펙트(NS_Foam_Impact). BP에서 할당
	UPROPERTY(EditDefaultsOnly, Category = "MD|Foam")
	TObjectPtr<UNiagaraSystem> ImpactEffect;

	// 밀봉 성공 시 파열 지점에 스폰할 폼 플러그(BP_FoamPlug, 부풀기 연출은 BP 타임라인)
	UPROPERTY(EditDefaultsOnly, Category = "MD|Foam")
	TSubclassOf<AActor> FoamPlugClass;

	// 착탄 지점에서 이 반경 안의 파열 지점(BreakLocation)이면 밀봉으로 인정 (조준 관용)
	UPROPERTY(EditDefaultsOnly, Category = "MD|Foam", meta = (ClampMin = "0.0"))
	float SealRadius = 120.f;

private:
	// 중복 히트 처리 방지 (히트 프레임에 여러 콜백이 올 수 있다)
	bool bImpactHandled = false;
};
