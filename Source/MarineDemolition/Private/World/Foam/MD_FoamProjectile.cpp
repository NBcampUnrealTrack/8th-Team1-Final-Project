#include "World/Foam/MD_FoamProjectile.h"

#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "GameplayTags/MD_GameplayTags.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "World/Wall/MD_WallUtility.h"

AMD_FoamProjectile::AMD_FoamProjectile()
{
	PrimaryActorTick.bCanEverTick = false;

	bReplicates = true;
	SetReplicatingMovement(true);
	// 스폰 직후 히트를 놓치지 않게 넉넉히. 실제 파괴는 착탄 처리에서
	InitialLifeSpan = 5.f;

	Collision = CreateDefaultSubobject<USphereComponent>(TEXT("Collision"));
	Collision->InitSphereRadius(8.f);
	Collision->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	Collision->SetNotifyRigidBodyCollision(true);
	SetRootComponent(Collision);

	TrailFX = CreateDefaultSubobject<UNiagaraComponent>(TEXT("TrailFX"));
	TrailFX->SetupAttachment(Collision);

	ProjectileMovement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("ProjectileMovement"));
	ProjectileMovement->SetUpdatedComponent(Collision);
	ProjectileMovement->bRotationFollowsVelocity = true;
	ProjectileMovement->bShouldBounce = false;
	// 속도·중력은 InitLaunch에서 탄도 정본(FoamGunComponent) 값으로 덮어쓴다
	ProjectileMovement->InitialSpeed = 0.f;
	ProjectileMovement->ProjectileGravityScale = 1.f;
}

void AMD_FoamProjectile::InitLaunch(const FVector& InVelocity, float InGravityScale)
{
	ProjectileMovement->Velocity = InVelocity;
	ProjectileMovement->ProjectileGravityScale = InGravityScale;

	// 발사자 몸에 맞고 터지는 것 방지
	if (AActor* InstigatorActor = GetInstigator())
	{
		Collision->IgnoreActorWhenMoving(InstigatorActor, true);
	}
}

void AMD_FoamProjectile::BeginPlay()
{
	Super::BeginPlay();
	Collision->OnComponentHit.AddDynamic(this, &AMD_FoamProjectile::HandleHit);
}

void AMD_FoamProjectile::HandleHit(UPrimitiveComponent* HitComponent, AActor* OtherActor,
                                   UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	// 판정은 서버 권위. 클라이언트 히트는 연출 복제로 따라온다
	if (!HasAuthority() || bImpactHandled)
	{
		return;
	}
	bImpactHandled = true;

	TrySealNearbyUtility(Hit);

	MulticastPlayImpactFX(Hit.ImpactPoint, Hit.ImpactNormal);
	DisableAfterImpact();
}

bool AMD_FoamProjectile::TrySealNearbyUtility(const FHitResult& Hit)
{
	AMD_WallUtility* Target = nullptr;

	// 1) 인프라를 직접 맞혔으면 그대로 대상
	if (AMD_WallUtility* DirectHit = Cast<AMD_WallUtility>(Hit.GetActor()))
	{
		Target = DirectHit;
	}
	else
	{
		// 2) 벽·바닥에 맞았어도 파열 지점이 반경 안이면 밀봉 인정 (조준 관용)
		const float SealRadiusSq = FMath::Square(SealRadius);
		for (TActorIterator<AMD_WallUtility> It(GetWorld()); It; ++It)
		{
			AMD_WallUtility* Utility = *It;
			if (Utility->GetUtilityState() != EMD_UtilityState::Broken)
			{
				continue;
			}
			if (FVector::DistSquared(Utility->GetBreakLocation(), Hit.ImpactPoint) <= SealRadiusSq)
			{
				Target = Utility;
				break;
			}
		}
	}

	if (!Target || !Target->TryExternalRepair(GetInstigator(), MDGameplayTags::Ability_UtilityRepair_Foam))
	{
		return false;
	}

	// 밀봉 성공 — 파열 지점에 폼 플러그 (VFX +X = 분출 방향 규약과 동일하게 법선을 +X로)
	if (FoamPlugClass)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (AActor* Plug = GetWorld()->SpawnActor<AActor>(FoamPlugClass, Target->GetBreakLocation(),
		                                                  FRotationMatrix::MakeFromX(Target->GetBreakNormal()).Rotator(), SpawnParams))
		{
			// 플러그 BP가 Replicates를 빠뜨려도 전 클라이언트에 보이도록 보장
			Plug->SetReplicates(true);
		}
	}
	return true;
}

void AMD_FoamProjectile::MulticastPlayImpactFX_Implementation(FVector_NetQuantize ImpactPoint,
                                                              FVector_NetQuantizeNormal ImpactNormal)
{
	if (GetNetMode() == NM_DedicatedServer || !ImpactEffect)
	{
		return;
	}

	// NS_Foam_Impact는 +Z가 표면 법선을 향하도록 스폰한다 (Splat이 표면에 눕는 규약)
	UNiagaraFunctionLibrary::SpawnSystemAtLocation(
		this, ImpactEffect, ImpactPoint, FRotationMatrix::MakeFromZ(ImpactNormal).Rotator());
}

void AMD_FoamProjectile::DisableAfterImpact()
{
	// 즉시 Destroy하면 멀티캐스트가 채널과 함께 유실된다 (MD_CuttableWall::Cut 관용구)
	SetActorEnableCollision(false);
	ProjectileMovement->StopMovementImmediately();

	// 본체 메시만 숨기고 트레일은 남은 파티클이 자연 소멸하게 둔다
	TInlineComponentArray<UStaticMeshComponent*> Meshes(this);
	for (UStaticMeshComponent* MeshComp : Meshes)
	{
		MeshComp->SetVisibility(false);
	}
	if (TrailFX)
	{
		TrailFX->Deactivate();
	}

	SetLifeSpan(0.4f);
}
