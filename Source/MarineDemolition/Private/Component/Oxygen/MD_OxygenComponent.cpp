// Fill out your copyright notice in the Description page of Project Settings.


#include "Component/Oxygen/MD_OxygenComponent.h"

#include "AbilitySystemComponent.h"
#include "Components/PostProcessComponent.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

#include "Character/MD_Character.h"
#include "Component/Equipment/MD_EquipHotbarComponent.h"
#include "Gameplay/Attribute/MD_CharacterAttributeSet.h"
#include "GameplayTags/MD_GameplayTags.h"
#include "Player/MD_PlayerState.h"
#include "Util/MD_LogUtility.h"
#include "Data/Equipment/MD_ItemDefinition.h"

UMD_OxygenComponent::UMD_OxygenComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

	SetIsReplicatedByDefault(true);
}

void UMD_OxygenComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UMD_OxygenComponent, BreathState);
}


void UMD_OxygenComponent::BeginPlay()
{
	Super::BeginPlay();

	// 서버 판정과 로컬 화면 갱신에 사용할 현재 월드
	UWorld* World = GetWorld();
	// 타이머 등록이 불가능한 월드 제외
	if (IsValid(World) == false)
	{
		return;
	}

	// PlayerState 준비 전에도 기존 화면 효과를 기본 불투명도로 초기화
	UpdateOxygenPostProcess();
	// 소유권과 PlayerState의 늦은 준비 및 실행 중 PostProcess 추가를 반영할 공통 갱신 주기
	World->GetTimerManager().SetTimer(EvaluationTimerHandle, this, &UMD_OxygenComponent::EvaluateBreathState, EvaluationInterval, true);
}

void UMD_OxygenComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 캐릭터 제거 및 월드 이동 전에 이 컴포넌트가 교체한 머티리얼 복원
	UpdateOxygenPostProcess(true);

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(EvaluationTimerHandle);
	}


	//어트리뷰트 변경 델리게이트 제거
	if (UAbilitySystemComponent* ASC = BoundASC.Get())
	{
		ASC->GetGameplayAttributeValueChangeDelegate(UMD_CharacterAttributeSet::GetOxygenAttribute()).Remove(OxygenChangedHandle);
	}

	Super::EndPlay(EndPlayReason);
}

void UMD_OxygenComponent::RefillOxygen()
{
	UMD_CharacterAttributeSet* Attributes = GetAttributeSet();
	if (!IsValid(Attributes))
	{
		return;
	}

	Attributes->SetOxygen(Attributes->GetMaxOxygen());
}

void UMD_OxygenComponent::UseOxygenCan()
{
	UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
	AMD_Character* OwnerCharacter = Cast<AMD_Character>(GetOwner());
	UMD_EquipHotbarComponent* HotbarComponent = IsValid(OwnerCharacter) == true ? OwnerCharacter->GetHotbarComponent() : nullptr;

	// 효과를 적용할 수 없거나 구매한 산소 캔이 없으면 무료 사용을 막기 위한 검사
	if (IsValid(ASC) == false || OxygenCanEffectClass == nullptr || IsValid(OxygenCanToolDefinition) == false || IsValid(HotbarComponent) == false)
	{
		return;
	}

	// 산소 캔 소비 처리
	bool bConsumeSuccess = HotbarComponent->TryConsumeItem(OxygenCanToolDefinition, 1);
	if (bConsumeSuccess == false)
	{
		return;
	}

	ApplyPassiveEffect(ASC, OxygenCanEffectClass);
}

void UMD_OxygenComponent::SetOxygenTankEquipped(bool bEquipped)
{
	UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
	if (!IsValid(ASC))
	{
		return;
	}

	if (bEquipped)
	{
		// 중복 장착 방지
		if (OxygenTankEffectHandle.IsValid())
		{
			return;
		}

		OxygenTankEffectHandle = ApplyPassiveEffect(ASC, OxygenTankEffectClass);
	}

	else
	{
		if (!OxygenTankEffectHandle.IsValid())
		{
			return;
		}

		ASC->RemoveActiveGameplayEffect(OxygenTankEffectHandle);

		OxygenTankEffectHandle = FActiveGameplayEffectHandle();
	}
}

void UMD_OxygenComponent::EvaluateBreathState()
{
	// 산소 비율과 현재 등록된 PostProcess를 소유 클라이언트 화면에 반영
	UpdateOxygenPostProcess();

	// 호흡 판정과 GameplayEffect를 실행할 소유 액터
	AActor* OwnerActor = GetOwner();
	// 클라이언트의 서버 전용 산소 처리 차단
	if (IsValid(OwnerActor) == false || OwnerActor->HasAuthority() == false)
	{
		// 로컬 화면 갱신 후 서버 전용 판정 생략
		return;
	}

	UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
	if (!IsValid(ASC))
	{
		return;
	}

	EnsureInitialized(ASC);

	const EMD_BreathState NewState = CalculateBreathState();

	if (NewState == BreathState)
	{
		return;
	}

	BreathState = NewState;

	ApplyBreathStateTag();

	if (bLogBreathState)
	{
		const ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());

		const UCharacterMovementComponent* Movement = IsValid(OwnerCharacter) ? OwnerCharacter->GetCharacterMovement() : nullptr;

		UE_LOG(MarineDemolition, Log,
			TEXT("[Oxygen][Server] 호흡 상태 → %s (InWater %d, ImmersionDepth %.2f)"),
			NewState == EMD_BreathState::Submerged ? TEXT("잠수") : TEXT("수면 위"),
			IsValid(Movement) ? Movement->IsInWater() : -1,
			IsValid(Movement) ? Movement->ImmersionDepth() : -1.0f);
	}

	OnBreathStateChanged.Broadcast(NewState);
}

EMD_BreathState UMD_OxygenComponent::CalculateBreathState() const
{
	const ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());

	const UCharacterMovementComponent* Movement =
		IsValid(OwnerCharacter) ? OwnerCharacter->GetCharacterMovement() : nullptr;

	if (!IsValid(Movement) || !Movement->IsInWater())
	{
		return EMD_BreathState::Surface;
	}

	// ImmersionDepth()는 캡슐 컴포넌트가 물 볼륨에 잠긴 비율.
	// IsInWater()만 보면 머리를 수면 위로 내밀어도 계속 소모되므로 잠긴 깊이까지 본다.
	return Movement->ImmersionDepth() >= SubmergedImmersionThreshold
		? EMD_BreathState::Submerged
		: EMD_BreathState::Surface;
}

void UMD_OxygenComponent::EnsureInitialized(UAbilitySystemComponent* ASC)
{
	if (bInitialized)
	{
		return;
	}

	bInitialized = true;
	BoundASC = ASC;

	//Oxygen 0 상태 알림.
	OxygenChangedHandle = ASC->GetGameplayAttributeValueChangeDelegate(
		UMD_CharacterAttributeSet::GetOxygenAttribute())
		.AddUObject(this, &UMD_OxygenComponent::HandleOxygenChanged);

	// 지속 GE는 여기서 한 번만 붙이고, 이후에는 태그로만 켜고 끈다.
	// 잠수할 때마다 Apply/Remove 하면 주기 타이머가 리셋돼 소모를 회피할 수 있다.
	ApplyPassiveEffect(ASC, OxygenDrainEffectClass);
	ApplyPassiveEffect(ASC, OxygenRecoveryEffectClass);
	ApplyPassiveEffect(ASC, OxygenDepletedDamageEffectClass);

	// ASC 준비 전에 구매한 확장 탱크 아이템이 존재하면 해당 효과를 적용
	const AMD_Character* OwnerCharacter = Cast<AMD_Character>(GetOwner());
	const UMD_EquipHotbarComponent* HotbarComponent = IsValid(OwnerCharacter) == true ? OwnerCharacter->GetHotbarComponent() : nullptr;
	// 공통 보유 수량을 기준으로 탱크 장착 효과 복구 여부 확인
	if (IsValid(HotbarComponent) == true && IsValid(OxygenTankToolDefinition) == true && HotbarComponent->GetItemCount(OxygenTankToolDefinition) > 0)
	{
		SetOxygenTankEquipped(true);
	}
}

FActiveGameplayEffectHandle UMD_OxygenComponent::ApplyPassiveEffect(UAbilitySystemComponent* ASC, const TSubclassOf<UGameplayEffect>& EffectClass)
{
	if (!EffectClass)
	{
		UE_LOG(MarineDemolition, Warning, TEXT("[Oxygen] 지속 GE 클래스가 비어 있다. BP_MDPlayerCharacter의 OxygenComponent에서 지정할 것"));
		return FActiveGameplayEffectHandle();
	}

	FGameplayEffectContextHandle Context = ASC->MakeEffectContext();
	Context.AddSourceObject(this);

	const FGameplayEffectSpecHandle SpecHandle = ASC->MakeOutgoingSpec(EffectClass, 1.0f, Context);

	if (!SpecHandle.IsValid())
	{
		return FActiveGameplayEffectHandle();
	}

	return ASC->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());
}
void UMD_OxygenComponent::HandleOxygenChanged(const FOnAttributeChangeData& ChangeData)
{
	// 최대치에서 반복 실행되는 주기 GE처럼 실제 값이 같으면 상태 처리를 생략한다.
	bool bValueChanged = FMath::IsNearlyEqual(ChangeData.OldValue, ChangeData.NewValue) == false;
	if (bValueChanged == false)
	{
		return;
	}

	UAbilitySystemComponent* ASC = BoundASC.Get();
	if (IsValid(ASC) == false)
	{
		return;
	}

	// 산소 고갈 상태가 달라진 경우에만 관련 태그를 갱신한다.
	bool bDepleted = ChangeData.NewValue <= 0.0f;
	if (bDepleted == bOxygenDepleted)
	{
		return;
	}

	bOxygenDepleted = bDepleted;
	if (bDepleted == true)
	{
		ASC->AddLooseGameplayTag(MDGameplayTags::State_Oxygen_Depleted);
	}
	else
	{
		ASC->RemoveLooseGameplayTag(MDGameplayTags::State_Oxygen_Depleted);
	}

	UE_LOG(MarineDemolition, Log, TEXT("[Oxygen][Server] 질식 상태 %s (산소 %.1f)"),
		bDepleted == true ? TEXT("진입") : TEXT("해제"), ChangeData.NewValue);
}

void UMD_OxygenComponent::UpdateOxygenPostProcess(bool bRestore)
{
	// 액터 수나 맵 이름에 의존하지 않는 현재 등록 월드
	UWorld* World = GetWorld();
	// 렌더링 대상이 없는 월드 제외
	if (IsValid(World) == false || World->GetNetMode() == NM_DedicatedServer)
	{
		// 화면 효과 처리 종료
		return;
	}

	// 다른 플레이어의 산소량으로 화면이 변경되는 상황 방지 대상
	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	// 로컬 플레이어 소유권 상실 또는 Blueprint 필수 설정 누락 검사
	if (IsValid(OwnerPawn) == false || OwnerPawn->IsPlayerControlled() == false || OwnerPawn->IsLocallyControlled() == false
		|| OxygenPostProcessMaterial.IsNull() == true || OxygenOpacityParameterName.IsNone() == true)
	{
		// 효과 적용 조건 미충족 시 기존 상태 복원으로 전환
		bRestore = true;
	}

	// 복원할 효과가 없는 원격 캐릭터의 순회 생략
	if (bRestore == true && bOxygenPostProcessApplied == false && IsValid(LocalOxygenPostProcess) == false)
	{
		// 복원 작업 없이 갱신 종료
		return;
	}

	// 맵에서 참조하지 않아 아직 로드되지 않은 설정 머티리얼 확인
	if (bRestore == false && IsValid(LoadedOxygenPostProcessMaterial) == false)
	{
		// 로컬 효과 생성과 원본 머티리얼 비교에 필요한 에셋 로드
		LoadedOxygenPostProcessMaterial = OxygenPostProcessMaterial.LoadSynchronous();
	}

	// 산소 정보가 아직 준비되지 않은 경우의 비활성 불투명도
	float Opacity = 0.0f;
	// 현재 산소량과 확장 탱크가 반영된 최대 산소량 조회 대상
	UMD_CharacterAttributeSet* Attributes = GetAttributeSet();
	// 유효한 산소 비율 계산 조건
	if (bRestore == false && IsValid(Attributes) == true && Attributes->GetMaxOxygen() > 0.0f)
	{
		// 잘못된 설정에 의한 영 나눗셈 방지
		float EffectStartRatio = FMath::Clamp(OxygenEffectStartRatio, 0.01f, 1.0f);
		// 기본 산소 30%에서 0, 고갈 시 1인 선형 불투명도
		Opacity = FMath::Clamp(1.0f - Attributes->GetOxygen() / Attributes->GetMaxOxygen() / EffectStartRatio, 0.0f, 1.0f);
	}

	// 현재 월드에 산소 효과가 이미 등록되어 있는지 확인할 상태
	bool bFoundRegisteredMaterial = false;
	// 실행 중 등록 변경과 무효 항목 제외가 반영된 목록 순회
	for (IInterface_PostProcessVolume& RegisteredVolume : World->GetPostProcessVolumeIterator())
	{
		// 레벨에 배치된 기본 PostProcess 액터 조회
		APostProcessVolume* Volume = Cast<APostProcessVolume>(&RegisteredVolume);
		// 파괴 중인 볼륨의 설정 접근 방지
		if (IsValid(Volume) == true)
		{
			// 액터에 등록된 산소 효과 적용 여부 확인
			if (UpdateOxygenPostProcessSettings(Volume->Settings, Opacity, bRestore) == true)
			{
				// 자동 생성 효과가 필요 없는 등록 상태 기록
				bFoundRegisteredMaterial = true;
			}
			// 같은 등록 객체의 중복 처리 생략
			continue;
		}

		// Blueprint 액터 내부 PostProcess 컴포넌트 조회
		UPostProcessComponent* PostProcessComponent = Cast<UPostProcessComponent>(&RegisteredVolume);
		// 유효한 기존 컴포넌트에 산소 효과가 적용된 경우 확인
		if (IsValid(PostProcessComponent) == true && PostProcessComponent != LocalOxygenPostProcess
			&& UpdateOxygenPostProcessSettings(PostProcessComponent->Settings, Opacity, bRestore) == true)
		{
			// 기존 컴포넌트에서 효과를 제공하는 상태 기록
			bFoundRegisteredMaterial = true;
		}
	}

	// 복원 또는 기존 효과 사용 시 유효한 자동 생성 효과만 정리
	if ((bRestore == true || bFoundRegisteredMaterial == true) && IsValid(LocalOxygenPostProcess) == true)
	{
		// 목록 순회 완료 후 등록 해제 및 중복 효과 방지
		LocalOxygenPostProcess->DestroyComponent();
		// 제거한 컴포넌트 참조 해제
		LocalOxygenPostProcess = nullptr;
	}
	// 복원 중이 아니며 기존 등록 효과가 없는 경우에만 설정 머티리얼 적용
	else if (bRestore == false && bFoundRegisteredMaterial == false && IsValid(LoadedOxygenPostProcessMaterial) == true)
	{
		// 최초 한 번만 로컬 PostProcess 생성
		if (IsValid(LocalOxygenPostProcess) == false)
		{
			// 소유 플레이어의 월드에만 존재하는 런타임 컴포넌트 생성
			LocalOxygenPostProcess = NewObject<UPostProcessComponent>(OwnerPawn);
			// 생성 실패 시 후속 접근 방지
			if (IsValid(LocalOxygenPostProcess) == false)
			{
				// 다음 갱신 주기에 생성 재시도
				return;
			}

			// 맵의 볼륨 위치와 크기에 관계없는 화면 적용
			LocalOxygenPostProcess->bUnbound = true;
			// 자동 생성한 화면 효과 활성화
			LocalOxygenPostProcess->bEnabled = true;
			// 산소 효과의 강도를 Scalar 파라미터로 제어할 기본 가중치
			LocalOxygenPostProcess->BlendWeight = 1.0f;
			// 설정한 머티리얼을 실제 렌더링 목록에 등록
			LocalOxygenPostProcess->Settings.AddBlendable(LoadedOxygenPostProcessMaterial, 1.0f);
			// 현재 월드의 PostProcess 렌더링 등록
			LocalOxygenPostProcess->RegisterComponent();
		}

		// 자동 생성 효과에도 같은 산소 비율 반영
		UpdateOxygenPostProcessSettings(LocalOxygenPostProcess->Settings, Opacity, false);
	}

	// 교체 항목의 복원 완료 여부 확인
	if (bRestore == true)
	{
		// 후속 원격 갱신에서 불필요한 복원 순회 방지
		bOxygenPostProcessApplied = false;
	}
}

bool UMD_OxygenComponent::UpdateOxygenPostProcessSettings(FPostProcessSettings& Settings, float Opacity, bool bRestore)
{
	// 현재 목록에서 산소 머티리얼을 갱신했는지 기록할 상태
	bool bAppliedMaterial = false;
	// 등록 개수와 순서에 관계없는 머티리얼 항목 순회
	for (FWeightedBlendable& Blendable : Settings.WeightedBlendables.Array)
	{
		// 머티리얼 이외의 Blendable 제외 대상
		UMaterialInterface* Material = Cast<UMaterialInterface>(Blendable.Object);
		// 유효한 머티리얼 항목 확인
		if (IsValid(Material) == false)
		{
			// 머티리얼이 아닌 항목 보존
			continue;
		}

		// 기존 산소용 동적 인스턴스 재사용 대상
		UMaterialInstanceDynamic* DynamicMaterial = Cast<UMaterialInstanceDynamic>(Material);
		// 복원 요청 시 이 컴포넌트가 생성한 유효 인스턴스만 처리
		if (IsValid(DynamicMaterial) == true && DynamicMaterial->GetOuter() == this && bRestore == true)
		{
			// 원래 등록 머티리얼 복원 및 기존 Blend Weight 보존
			Blendable.Object = DynamicMaterial->Parent;
			// 복원한 원본의 파라미터 변경 방지
			continue;
		}
		// 재사용할 자체 인스턴스가 없는 경우에만 새 효과 적용 여부 검사
		else if (IsValid(DynamicMaterial) == false || DynamicMaterial->GetOuter() != this)
		{
			// 복원 대상이 아니거나 설정 에셋이 없는 경우 제외
			if (bRestore == true || IsValid(LoadedOxygenPostProcessMaterial) == false)
			{
				// 교체하지 않은 머티리얼 유지
				continue;
			}

			// 지정 머티리얼을 부모로 사용하는 인스턴스 판별 대상
			UMaterialInstance* MaterialInstance = Cast<UMaterialInstance>(Material);
			// 설정 에셋과 원본 및 자식 인스턴스에 모두 해당하지 않는 효과 제외
			if (Material != LoadedOxygenPostProcessMaterial && Material != LoadedOxygenPostProcessMaterial->GetMaterial()
				&& (IsValid(MaterialInstance) == false || MaterialInstance->IsChildOf(LoadedOxygenPostProcessMaterial) == false))
			{
				// 산소 이외의 후처리 효과 보존
				continue;
			}

			// Scalar 파라미터 조회 API가 요구하는 출력 저장소
			float ParameterValue = 0.0f;
			// 존재하지 않는 파라미터의 동적 인스턴스 생성 방지
			if (Material->GetScalarParameterValue(FMaterialParameterInfo(OxygenOpacityParameterName), ParameterValue) == false)
			{
				// 불투명도 제어가 불가능한 항목 유지
				continue;
			}

			// 원본 에셋과 다른 월드의 값을 보존할 런타임 인스턴스 생성
			DynamicMaterial = UMaterialInstanceDynamic::Create(Material, this);
			// 생성 실패 시 기존 등록 머티리얼 유지
			if (IsValid(DynamicMaterial) == false)
			{
				// 실패한 인스턴스의 등록 및 접근 방지
				continue;
			}

			// 기존 등록 위치와 Blend Weight를 유지한 인스턴스 교체
			Blendable.Object = DynamicMaterial;
		}

		// 현재 산소 비율에 따른 효과 불투명도 반영
		DynamicMaterial->SetScalarParameterValue(OxygenOpacityParameterName, Opacity);
		// 소유권 상실 및 종료 시 원본 복원 필요 상태 기록
		bOxygenPostProcessApplied = true;
		// 기존 등록 효과와 자동 생성 효과의 중복 여부 판단에 사용할 결과
		bAppliedMaterial = true;
	}

	// 실제 갱신한 산소 효과의 존재 여부 반환
	return bAppliedMaterial;
}

void UMD_OxygenComponent::ApplyBreathStateTag()
{
	UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
	if (!IsValid(ASC))
	{
		return;
	}

	if (BreathState == EMD_BreathState::Submerged)
	{
		ASC->AddLooseGameplayTag(MDGameplayTags::State_Submerged);
	}
	else
	{
		ASC->RemoveLooseGameplayTag(MDGameplayTags::State_Submerged);
	}
}

void UMD_OxygenComponent::OnRep_BreathState()
{
	ApplyBreathStateTag();

	OnBreathStateChanged.Broadcast(BreathState);
}

UAbilitySystemComponent* UMD_OxygenComponent::GetAbilitySystemComponent() const
{
	const APawn* OwnerPawn = Cast<APawn>(GetOwner());

	AMD_PlayerState* MDPlayerState =
		IsValid(OwnerPawn) ? OwnerPawn->GetPlayerState<AMD_PlayerState>() : nullptr;

	return IsValid(MDPlayerState) ? MDPlayerState->GetAbilitySystemComponent() : nullptr;
}

UMD_CharacterAttributeSet* UMD_OxygenComponent::GetAttributeSet() const
{
	const APawn* OwnerPawn = Cast<APawn>(GetOwner());

	AMD_PlayerState* MDPlayerState =
		IsValid(OwnerPawn) ? OwnerPawn->GetPlayerState<AMD_PlayerState>() : nullptr;

	return IsValid(MDPlayerState) ? MDPlayerState->GetCharacterAttributeSet() : nullptr;
}

