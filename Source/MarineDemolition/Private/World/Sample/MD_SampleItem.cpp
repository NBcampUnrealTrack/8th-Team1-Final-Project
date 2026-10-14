#include "World/Sample/MD_SampleItem.h"
#include "Gameplay/Interaction/MD_InteractionPresentationLibrary.h"
#include "World/Sample/MD_SampleProtectionZone.h"

#include "AbilitySystemComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "Component/Interaction/MD_InteractionComponent.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "GameplayTags/MD_GameplayTags.h"
#include "Util/MD_LogUtility.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Gameplay/Merchant/MD_MerchantSubsystem.h"
#include "Net/UnrealNetwork.h"
#include "Util/MD_MaterialTintUtility.h"

AMD_SampleItem::AMD_SampleItem()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	// 공통 부모의 파괴 불가 기본값을 샘플 전용 피해 허용 정책으로 전환하는 설정
	SetCanBeDamaged(true);

	// 샘플의 충격 강도에 따라 손상과 파괴를 구분하기 위한 임계값
	BreakImpactSpeed = 800.0f;
	DamageImpactSpeed = 400.0f;

	// 캐릭터와의 물리 충돌은 유지하되 작은 샘플을 발판이나 계단으로 판정하지 않는다
	Mesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	Mesh->CanCharacterStepUpOn = ECB_No;
	Mesh->SetWalkableSlopeOverride(FWalkableSlopeOverride(WalkableSlope_Unwalkable, 0.f));

	// 가벼운 샘플이 캐릭터와 접촉한 뒤 오래 튀거나 회전하지 않도록 잔여 운동을 줄인다
	Mesh->SetLinearDamping(0.5f);
	Mesh->SetAngularDamping(1.f);

	// 샘플에서만 물리 충격 이벤트를 받아 파괴 기능을 추가하기 위한 설정
	Mesh->SetNotifyRigidBodyCollision(true);
}

void AMD_SampleItem::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority() || !WaterFloat.bEnabled || !IsValid(Mesh)
		|| !Mesh->IsSimulatingPhysics() || !Mesh->IsGravityEnabled()
		|| IsHidden() || GetAttachParentActor()) return;

	// Check actual grips, not the historical bHasBeenCarried flag: dropping re-enables buoyancy.
	for (TActorIterator<APawn> It(GetWorld()); It; ++It)
	{
		const UMD_InteractionComponent* Interaction = It->FindComponentByClass<UMD_InteractionComponent>();
		if (Interaction && Interaction->GetHeldActor() == this) return;
	}
	const FVector Acceleration = WaterFloat.GetAcceleration(Mesh->GetCenterOfMass().Z,
		Mesh->GetPhysicsLinearVelocity(), GetWorld()->GetTimeSeconds(), GetWorld()->GetGravityZ());
	if (!Acceleration.IsNearlyZero()) Mesh->AddForce(Acceleration, NAME_None, true);
}

FText AMD_SampleItem::GetInteractionPrompt_Implementation(AActor* Interactor) const
{
	// 선택한 행동 유형의 설정 문구와 현재 입력 키 표시
	return UMD_InteractionPresentationLibrary::FindActorActionPrompt(this, Interactor);
}

void AMD_SampleItem::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	
	DOREPLIFETIME(AMD_SampleItem, bDamaged);
}

FGameplayTag AMD_SampleItem::GetQuestGradeTag() const
{
	FGameplayTag OutGradeTag = UMD_MerchantSubsystem::GradeTagFromSampleGrade(Grade);
	return OutGradeTag;
}

bool AMD_SampleItem::IsQuestItemIntact() const
{
	bool bOutIntact = bDamaged == false;
	return bOutIntact;
}

void AMD_SampleItem::OnRep_Damaged()
{
	if (bDamaged)
	{
		ApplyDamagedVisual();
	}
}

void AMD_SampleItem::ApplyDamagedVisual()
{
	if (UMaterialInstanceDynamic* MID = Mesh->CreateAndSetMaterialInstanceDynamic(0))
	{
		MDMaterialTint::SetVectorParameterChecked(MID, TEXT("Color"), FLinearColor::Red);
	}
}

void AMD_SampleItem::SetDamaged()
{
	// 하위 클래스가 지정한 게임플레이 파괴 정책을 손상 상태에도 동일하게 적용하기 위한 결과
	bool bCanBeDestroyed = CanBeDestroyedByGameplay();
	if (bCanBeDestroyed == false || bDamaged == true)
	{
		return;
	}
	bDamaged = true;

	ApplyDamagedVisual();
}

bool AMD_SampleItem::CanBeDestroyedByGameplay() const
{
	// 기존 일반 샘플의 손상·파괴 동작을 기본값으로 보존하기 위한 결과
	bool bOutCanBeDestroyed = true;
	return bOutCanBeDestroyed;
}

void AMD_SampleItem::MulticastSpawnBreakDebris_Implementation()
{
	if (!BreakDebrisClass)
	{
		return;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	GetWorld()->SpawnActor<AActor>(BreakDebrisClass, GetActorTransform(), SpawnParams);
}

void AMD_SampleItem::BeginPlay()
{
	Super::BeginPlay();

	OnTakeAnyDamage.AddDynamic(this, &AMD_SampleItem::OnAnyDamage);
	if (HasAuthority())
	{
		Mesh->OnComponentHit.AddDynamic(this, &AMD_SampleItem::HandleMeshHit);

		// 스포너가 SpawnActor 직후 SpawnProtectionDuration을 세팅하므로, 그 값을 반영하도록 다음 틱으로 미룬다.
		GetWorldTimerManager().SetTimerForNextTick(this, &AMD_SampleItem::BeginSpawnProtection);
	}
}

void AMD_SampleItem::BeginSpawnProtection()
{
	if (SpawnProtectionDuration <= 0.f)
	{
		return;
	}

	BeginImpactProtection(SpawnProtectionDuration);
}

void AMD_SampleItem::NotifyGrabbed()
{
	Super::NotifyGrabbed();
	// 충격 보호 설정과 무관하게 서버의 실제 그랩 성공 이력 보존
	if (HasAuthority() == true)
	{
		// 퇴근 시 복귀 구역 안의 운반한 샘플만 자동 제출 허용
		bHasBeenCarried = true;
	}

	if (GrabProtectionDuration <= 0.f)
	{
		return;
	}

	BeginImpactProtection(GrabProtectionDuration);
}

void AMD_SampleItem::SuppressImpactDamage(float Duration)
{
	if (Duration <= 0.f)
	{
		return;
	}

	BeginImpactProtection(Duration);
}

void AMD_SampleItem::BeginImpactProtection(float Duration)
{
	// 이미 더 길게 걸려 있는 보호를 짧은 요청이 덮어쓰지 않게 한다.
	// (스폰 보호 3초 중에 선반 보호 0.5초가 들어오면 스폰 보호가 잘려버린다)
	float Remaining = GetWorldTimerManager().GetTimerRemaining(ImpactProtectionTimerHandle);
	if (bImpactProtected && Remaining > Duration)
	{
		return;
	}

	bImpactProtected = true;
	GetWorldTimerManager().SetTimer(
		ImpactProtectionTimerHandle, this, &AMD_SampleItem::EndImpactProtection, Duration, false);
}

void AMD_SampleItem::EndImpactProtection()
{
	bImpactProtected = false;
}

void AMD_SampleItem::OnAnyDamage(AActor* DamagedActor, float Damage, const UDamageType* DamageType, AController* InstigatedBy, AActor* DamageCauser)
{
	// 파괴 불가 하위 클래스가 일반 피해로 손상되는 경로를 차단하기 위한 결과
	bool bCanBeDestroyed = CanBeDestroyedByGameplay();
	if (bCanBeDestroyed == false || bImpactProtected == true || bDamaged == true
		|| AMD_SampleProtectionZone::ProtectsActor(this))
	{
		return;
	}
	SetDamaged();

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Orange,
				FString::Printf(TEXT("샘플 손상: %s"), *GetName()));
	}
}

void AMD_SampleItem::HandleMeshHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComponent, FVector NormalImpulse, const FHitResult& Hit)
{
	// 파괴 불가 하위 클래스가 물리 충격으로 손상되거나 파괴되는 경로를 차단하기 위한 결과
	bool bCanBeDestroyed = CanBeDestroyedByGameplay();
	if (HasAuthority() == false || IsActorBeingDestroyed() == true || bCanBeDestroyed == false)
	{
		return;
	}

	if (bImpactProtected || AMD_SampleProtectionZone::ProtectsActor(this))
	{
		return;
	}

	// 폴리지(풀·산호·켈프 등)는 폴리지 툴로 배치된 HISM 컴포넌트를 쓴다. 손상·파괴 판정에서 제외한다.
	if (IsValid(OtherComponent) && OtherComponent->IsA(UHierarchicalInstancedStaticMeshComponent::StaticClass()))
	{
		return;
	}

	float SampleMass = Mesh->GetMass();
	if (!FMath::IsFinite(SampleMass) || SampleMass <= UE_SMALL_NUMBER)
	{
		return;
	}

	float ImpactSpeed = NormalImpulse.Size() / SampleMass;
	if (!FMath::IsFinite(ImpactSpeed))
	{
		return;
	}

	// 실제 손상 또는 파괴를 일으키는 충격에만 완충 보호 소비
	if (ImpactSpeed >= BreakImpactSpeed || (bDamaged == false && ImpactSpeed >= DamageImpactSpeed))
	{
		// 서버의 실제 운반 상태를 가진 플레이어 목록 조회
		AGameStateBase* GameState = GetWorld()->GetGameState();
		// 월드 초기화나 종료 중 운반자 조회 생략
		if (IsValid(GameState) == true)
		{
			// 다른 플레이어의 보호 효과를 소비하지 않도록 운반자 확인
			for (APlayerState* PlayerState : GameState->PlayerArray)
			{
				// 연결이 종료된 플레이어의 접근 차단
				APawn* Carrier = IsValid(PlayerState) == true ? PlayerState->GetPawn() : nullptr;
				// 물리 운반을 담당하는 실제 컴포넌트 조회
				UMD_InteractionComponent* Interaction = IsValid(Carrier) == true ? Carrier->FindComponentByClass<UMD_InteractionComponent>() : nullptr;
				// 손에 들고 있는 이 샘플에만 보호 허용
				if (IsValid(Interaction) == false || Interaction->GetHeldActor() != this)
				{
					// 놓여 있거나 다른 플레이어가 운반 중인 샘플 제외
					continue;
				}
				// GA가 운반자에게 부여한 효과의 실제 저장소 조회
				UAbilitySystemComponent* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Carrier);
				// 활성 완충 보호가 있는 운반자에 한정한 효과 조회
				if (IsValid(ASC) == true && ASC->HasMatchingGameplayTag(MDGameplayTags::State_SampleProtection) == true)
				{
					// 보호 상태를 부여한 GE만 선택하는 태그 필터
					FGameplayTagContainer ProtectionTags(MDGameplayTags::State_SampleProtection);
					// 기본 도구 GA와 다른 효과를 보존할 활성 효과 목록
					TArray<FActiveGameplayEffectHandle> Effects = ASC->GetActiveEffects(FGameplayEffectQuery::MakeQuery_MatchAllOwningTags(ProtectionTags));
					// 한 충돌에 보호 효과 한 개만 회수
					for (FActiveGameplayEffectHandle Effect : Effects)
					{
						// 실제 회수에 성공한 경우에만 손상 무효 처리
						if (ASC->RemoveActiveGameplayEffect(Effect) == true)
						{
							// 동일 충돌의 연속 물리 알림으로 즉시 재손상되는 현상 방지
							BeginImpactProtection(GrabProtectionDuration);
							// 소비한 보호와 대상의 프로젝트 로그 기록
							UE_LOG(MarineDemolition, Log, TEXT("[SampleProtection] 충돌 흡수: Sample=%s, Carrier=%s"), *GetName(), *Carrier->GetName());
							// 손상과 파괴 판정 없이 충돌 처리 종료
							return;
						}
					}
				}
				// 실제 운반자 외 다른 플레이어의 검사 종료
				break;
			}
		}
	}

	// 파괴 임계 이상이면 즉시 파괴, 손상 임계 이상이면 손상만 (기획 02 §4 손 운반 리스크)
	if (ImpactSpeed >= BreakImpactSpeed)
	{
		MulticastSpawnBreakDebris();
		Destroy();
		return;
	}

	if (ImpactSpeed >= DamageImpactSpeed)
	{
		SetDamaged();
	}
}

void AMD_SampleItem::RestorePersistentSampleState(EMD_SampleGrade SavedGrade, bool bSavedDamaged, bool bSavedHasBeenCarried)
{
	// 물품 상태 복원은 서버에서만 적용
	if (HasAuthority() == false)
	{
		// 비권위 요청 종료
		return;
	}
	// 파일 및 다음 일자 복원에서 자동 제출 자격 유지
	bHasBeenCarried = bSavedHasBeenCarried;
	// 저장 시점의 납품 등급 적용
	Grade = SavedGrade;
	// 손상 효과와 퀘스트 실패를 재발행하지 않고 상태만 복구
	bDamaged = bSavedDamaged;
	// 호스트 손상 외형 갱신
	OnRep_Damaged();
	// 로드 직후 물리 안정화 중 발생하는 충격 보호
	SuppressImpactDamage(SpawnProtectionDuration);
	// 클라이언트 손상 상태 동기화
	ForceNetUpdate();
}
