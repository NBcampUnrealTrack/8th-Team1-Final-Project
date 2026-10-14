#include "World/Wall/MD_PowerConduit.h"

#include "Game/MD_GameStateBase.h"
#include "Component/Facility/MD_FacilityRepairComponent.h"
#include "Component/Facility/MD_FacilityFaultManagerComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameplayTags/MD_GameplayTags.h"
#include "TimerManager.h"
#include "UObject/UnrealType.h"

AMD_PowerConduit::AMD_PowerConduit()
{
	FacilityRepair->Kind = EMD_FacilityKind::WiringPanel;
	FacilityRepair->bAllowManualRepair = false;
	FacilityRepair->bAllowInsulationTape = true;
	RequiredRepairToolTag = MDGameplayTags::Ability_UtilityRepair_Insulation;
	// 절연테이프는 1회용: 수리 성공 시 핫바에서 1개 차감. 상호작용 키가 아니라 도구 입력(LMB)으로만 사용
	bConsumesRepairTool = true;
	bRepairRequiresToolInput = true;

	CutSoundTag = FGameplayTag::RequestGameplayTag(
		TEXT("SFX.Hazard.Cable.Cut"), /*ErrorIfNotFound*/ false);
	SparkLoopSoundTag = FGameplayTag::RequestGameplayTag(
		TEXT("SFX.Hazard.Cable.SparkLoop"), /*ErrorIfNotFound*/ false);
}

void AMD_PowerConduit::BeginPlay()
{
	// 레벨 재진입 시 현재 월드의 메시 연결 허용
	bPowerLightsShuttingDown = false;
	// Cut wires are repaired with one insulation tape, independently of the breaker puzzle.
	FacilityRepair->bAllowManualRepair = false;
	FacilityRepair->bAllowInsulationTape = true;
	Super::BeginPlay();
	// 초기 복제와 BeginPlay 순서에 관계없이 BP 메시 슬롯 설정 등록
	OnUtilityStateChangedVisual(UtilityState);
}

void AMD_PowerConduit::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 정리 도중의 머티리얼 콜백에서 전선 재등록 차단
	bPowerLightsShuttingDown = true;
	// 월드 종료 이후의 조명 재연결 콜백 차단
	if (IsValid(GetWorld()) == true)
	{
		// 진행 중인 다음 틱 재시도 예약 정리
		GetWorld()->GetTimerManager().ClearTimer(PowerRegistrationTimer);
	}
	// 제거되는 전선의 고장이 남은 메시 슬롯 집계에 유지되는 상황 방지
	if (RegisteredPowerManager.IsValid() == true)
	{
		// 서버 집계에서 현재 전선의 메시 슬롯 연결 해제
		RegisteredPowerManager->RemovePowerSource(this);
	}
	// 종료된 월드의 관리자 약한 연결 해제
	RegisteredPowerManager.Reset();
	// 종료된 전선이 보관한 메시 슬롯과 머티리얼 연결 정리
	ControlledPowerLights.Reset();
	Super::EndPlay(EndPlayReason);
}

void AMD_PowerConduit::ApplyPoweredLights()
{
	// 삭제되지 않고 레벨에서 이탈한 전선의 재시도 차단
	if (bPowerLightsShuttingDown == true)
	{
		// 종료 정리가 끝난 메시 연결 유지
		return;
	}
	// 게임 월드에 속한 전선의 머티리얼 연결만 허용
	if (IsValid(GetWorld()) == false || GetWorld()->IsGameWorld() == false || IsActorBeingDestroyed() == true)
	{
		// 편집기와 종료 중인 전선의 상태 변경 차단
		return;
	}
	// 이전 준비 대기를 현재 BP 설정 기준으로 다시 판단
	GetWorld()->GetTimerManager().ClearTimer(PowerRegistrationTimer);
	// 이름으로 지정한 컴포넌트와 메시 에셋의 지연 준비 여부 보관
	bool bPendingPowerMeshes = false;
	// BP에서 참조를 변경한 경우 이전 메시 슬롯까지 서버 집계에서 갱신
	ControlledPowerLights.Reset();
	// 드롭한 액터의 직접 소유 메시를 공통 머티리얼 적용 대상으로 확장
	for (AActor* TargetActor : TargetActors)
	{
		// 다른 월드와 종료 중인 배치 액터의 메시 등록 제외
		if (IsValid(TargetActor) == false || TargetActor->GetWorld() != GetWorld() || TargetActor->IsActorBeingDestroyed() == true)
		{
			// 현재 월드에 남아 있는 액터만 대상 해석
			continue;
		}
		// 자식 액터까지 확장하지 않는 직접 소유 스태틱 메시 목록
		TInlineComponentArray<UStaticMeshComponent*> ActorMeshes;
		// 액터의 메시 루트와 추가 메시 컴포넌트를 함께 수집
		TargetActor->GetComponents<UStaticMeshComponent>(ActorMeshes, false);
		// 초기화 순서상 컴포넌트가 아직 준비되지 않은 액터만 재확인
		if (ActorMeshes.IsEmpty() == true && TargetActor->HasActorBegunPlay() == false && MaterialIndex >= 0)
		{
			// 액터 초기화가 끝날 때까지 기존 단일 재시도 예약 사용
			bPendingPowerMeshes = true;
		}
		// 여러 메시가 있는 액터의 각 컴포넌트를 동일 슬롯으로 등록
		for (UStaticMeshComponent* MeshComponent : ActorMeshes)
		{
			// 개별 참조와 공유하는 슬롯 검증 및 중복 제거 적용
			AddPoweredMesh(MeshComponent, bPendingPowerMeshes);
		}
	}
	// 특정 컴포넌트만 지정한 기존 참조도 같은 적용 목록에 병합
	for (FComponentReference& MeshReference : MeshComponents)
	{
		// 제거된 외부 액터와 컴포넌트 참조의 자기 루트 대체 해석 차단
		if (MeshReference.OtherActor.IsStale() == true || MeshReference.OverrideComponent.IsStale() == true)
		{
			// 제거된 배치 참조의 머티리얼 적용 생략
			continue;
		}
		// 명시적인 액터 또는 컴포넌트 참조 존재 여부 확인
		if (MeshReference.OtherActor.IsValid() == false && MeshReference.OverrideComponent.IsValid() == false)
		{
			// 미설정 항목을 전선 자신의 루트로 해석하는 상황 방지
			if (MeshReference.ComponentProperty.IsNone() == true && MeshReference.PathToComponent.IsEmpty() == true)
			{
				// BP에서 실제 메시를 선택한 항목만 적용
				continue;
			}
		}
		// 지연 생성과 잘못된 컴포넌트 타입을 구분할 원래 참조 해석
		UActorComponent* ReferencedComponent = MeshReference.GetComponent(this);
		// 명시한 이름의 컴포넌트가 아직 생성되지 않은 경우 준비 대기
		if (IsValid(ReferencedComponent) == false)
		{
			// 조회할 대상 액터가 현재 월드에서 살아 있는 경우에만 재시도
			AActor* ReferenceOwner = MeshReference.OtherActor.IsValid() == true ? MeshReference.OtherActor.Get() : this;
			// 삭제 중이거나 다른 월드의 대상에 대한 대기 차단
			if (ReferenceOwner->GetWorld() == GetWorld() && ReferenceOwner->IsActorBeingDestroyed() == false && MaterialIndex >= 0)
			{
				// 클래스에 실제 존재하는 컴포넌트 프로퍼티만 준비 대기
				if (MeshReference.ComponentProperty.IsNone() == false)
				{
					// 존재하지 않는 이름을 영구 미생성 참조로 오인하는 상황 방지
					FObjectPropertyBase* ReferenceProperty = FindFProperty<FObjectPropertyBase>(ReferenceOwner->GetClass(), MeshReference.ComponentProperty);
					// 스태틱 메시를 보관할 수 없는 프로퍼티의 재시도 제외
					if (ReferenceProperty != nullptr && IsValid(ReferenceProperty->PropertyClass) == true)
					{
						// 상위 객체 타입 또는 스태틱 메시 파생 타입의 지연 값 허용
						if (UStaticMeshComponent::StaticClass()->IsChildOf(ReferenceProperty->PropertyClass) == true
							|| ReferenceProperty->PropertyClass->IsChildOf(UStaticMeshComponent::StaticClass()) == true)
						{
							// 실제 프로퍼티 값이 채워진 이후 재해석 예약
							bPendingPowerMeshes = true;
						}
					}
				}
				else if (MeshReference.PathToComponent.IsEmpty() == false)
				{
					// 런타임 생성이 가능한 컴포넌트 경로의 준비 대기
					bPendingPowerMeshes = true;
				}
			}
			// 유효 컴포넌트가 준비된 이후 등록
			continue;
		}
		// 해당 액터에 부착된 지정 스태틱 메시 컴포넌트 해석
		UStaticMeshComponent* MeshComponent = Cast<UStaticMeshComponent>(ReferencedComponent);
		// 액터 목록과 동일한 검증으로 실제 메시 슬롯 등록
		AddPoweredMesh(MeshComponent, bPendingPowerMeshes);
	}
	for (const FMD_PowerLightTarget& Slot : PowerLightTargets)
	{
		UStaticMeshComponent* TargetMesh = Slot.MeshComponent;
		if (!IsValid(TargetMesh) || TargetMesh->GetWorld()!=GetWorld() || TargetMesh->IsBeingDestroyed()
			|| !IsValid(TargetMesh->GetOwner()) || TargetMesh->GetOwner()->IsActorBeingDestroyed()) continue;
		if (Slot.MaterialIndex<0 || Slot.MaterialIndex>=TargetMesh->GetNumMaterials())
		{
			bPendingPowerMeshes |= !TargetMesh->GetStaticMesh() && Slot.MaterialIndex>=0;
			continue;
		}
		if (ControlledPowerLights.ContainsByPredicate([&Slot](const FMD_PowerLightState& Entry)
			{ return Entry.MeshComponent==Slot.MeshComponent && Entry.MaterialIndex==Slot.MaterialIndex; })) continue;
		FMD_PowerLightState& State=ControlledPowerLights.AddDefaulted_GetRef();
		State.MeshComponent=TargetMesh;State.MaterialIndex=Slot.MaterialIndex;
		State.NormalMaterial=Slot.NormalMaterial;State.BlackoutMaterial=Slot.BlackoutMaterial;
	}
	// 현재 전선 고장과 메시 슬롯 설정을 준비된 관리자에 전달
	RegisterPowerLights();
	// 유효한 설정의 준비 지연만 현재 상태를 유지하며 재확인
	if (bPendingPowerMeshes == true && bPowerLightsShuttingDown == false && IsActorBeingDestroyed() == false)
	{
		// 관리자 준비 대기와 메시 준비 대기의 중복 예약 방지
		if (GetWorld()->GetTimerManager().IsTimerActive(PowerRegistrationTimer) == false)
		{
			// 게임 규칙과 분리된 준비 확인만 0.1초 간격으로 제한
			GetWorld()->GetTimerManager().SetTimer(PowerRegistrationTimer, this, &ThisClass::ApplyPoweredLights, 0.1f, false);
		}
	}
}

void AMD_PowerConduit::AddPoweredMesh(UStaticMeshComponent* MeshComponent, bool& bPendingPowerMeshes)
{
	// 다른 월드와 종료 중인 메시 참조 제외
	if (IsValid(MeshComponent) == false || MeshComponent->GetWorld() != GetWorld() || MeshComponent->IsBeingDestroyed() == true)
	{
		// 현재 월드의 유효 메시만 등록
		return;
	}
	// 컴포넌트 제거보다 먼저 종료된 소유 액터의 준비 대기 차단
	if (IsValid(MeshComponent->GetOwner()) == false || MeshComponent->GetOwner()->IsActorBeingDestroyed() == true)
	{
		// 종료된 대상 메시의 재등록 생략
		return;
	}
	// 존재하지 않는 슬롯의 머티리얼 추가 및 교체 방지
	if (MaterialIndex < 0 || MaterialIndex >= MeshComponent->GetNumMaterials())
	{
		// 메시 에셋이 아직 없는 경우에만 슬롯 생성 대기
		if (MaterialIndex >= 0 && IsValid(MeshComponent->GetStaticMesh()) == false)
		{
			// 에셋 준비 이후 BP에서 지정한 슬롯 재확인
			bPendingPowerMeshes = true;
		}
		// 현재 메시의 유효한 슬롯만 등록
		return;
	}
	// 같은 컴포넌트와 슬롯을 반복 지정한 전선의 사건 중복 집계 방지
	if (ControlledPowerLights.ContainsByPredicate([this, MeshComponent](FMD_PowerLightState& State)
		{
			// 같은 컴포넌트와 슬롯의 기존 등록 여부 확인
			return State.MeshComponent == MeshComponent && State.MaterialIndex == MaterialIndex;
		}) == true)
	{
		// 같은 전선에서는 최초 슬롯 설정만 사용
		return;
	}
	// 실제 슬롯과 BP 머티리얼을 전달할 적용 항목 구성
	FMD_PowerLightState& Target = ControlledPowerLights.AddDefaulted_GetRef();
	// 참조가 지정한 실제 컴포넌트 보관
	Target.MeshComponent = MeshComponent;
	// 교체 대상 슬롯 보관
	Target.MaterialIndex = MaterialIndex;
	// 정상 상태의 BP 지정 머티리얼 전달
	Target.NormalMaterial = NormalMaterial;
	// 정전 상태의 BP 지정 머티리얼 전달
	Target.BlackoutMaterial = BlackoutMaterial;
}

void AMD_PowerConduit::RegisterPowerLights()
{
	// 레벨 이탈 이후 지연 도착한 연결 콜백 차단
	if (bPowerLightsShuttingDown == true)
	{
		// 종료된 전선의 등록 해제 상태 유지
		return;
	}
	// 준비 여부와 다음 틱 예약에 사용할 실제 월드 조회
	UWorld* World = GetWorld();
	// 종료 중인 월드와 전선의 재연결 차단
	if (IsValid(World) == false || World->IsGameWorld() == false || IsActorBeingDestroyed() == true)
	{
		// 유효한 게임 월드에서만 연결 유지
		return;
	}
	// 적용 대상이 없는 기본 전선은 관리자 대기 예약 생략
	if (ControlledPowerLights.IsEmpty() == true && RegisteredPowerManager.IsValid() == false)
	{
		// 머티리얼을 제어하지 않는 기존 배치 유지
		return;
	}
	// 현재 월드에 복제된 GameState 조회
	AGameStateBase* GameState = World->GetGameState();
	// 지연 도착 가능한 GameState의 공통 시설 관리자 조회
	UMD_FacilityFaultManagerComponent* Manager = IsValid(GameState) == true ? GameState->FindComponentByClass<UMD_FacilityFaultManagerComponent>() : nullptr;
	// 메시 참조보다 관리자가 늦게 도착한 경우 재연결 예약
	if (IsValid(Manager) == false)
	{
		// 중복 재시도 예약 제거
		World->GetTimerManager().ClearTimer(PowerRegistrationTimer);
		// 게임 규칙과 분리된 관리자 준비 확인만 0.1초 간격으로 제한
		World->GetTimerManager().SetTimer(PowerRegistrationTimer, this, &ThisClass::ApplyPoweredLights, 0.1f, false);
		// 관리자가 준비된 이후 현재 상태 적용
		return;
	}
	// 관리자 준비 이후 남은 재시도 예약 정리
	World->GetTimerManager().ClearTimer(PowerRegistrationTimer);
	// 같은 월드에서 관리자가 교체된 경우 이전 서버 집계 해제
	if (RegisteredPowerManager.IsValid() == true && RegisteredPowerManager.Get() != Manager)
	{
		// 이전 관리자에 남은 전선 연결 정리
		RegisteredPowerManager->RemovePowerSource(this);
	}
	// 종료 시 해제할 현재 관리자 보관
	RegisteredPowerManager = Manager;
	// 현재 유효한 슬롯만 전달할 임시 목록 준비
	TArray<FMD_PowerLightState> Lights;
	// 초기 등록 이후 제거될 수 있는 컴포넌트 참조 확인
	for (FMD_PowerLightState& Light : ControlledPowerLights)
	{
		// 같은 월드에 남은 컴포넌트만 현재 관리자에 전달
		if (IsValid(Light.MeshComponent) == false || Light.MeshComponent->GetWorld() != World || Light.MeshComponent->IsBeingDestroyed() == true)
		{
			// 제거된 메시의 슬롯 등록 생략
			continue;
		}
		// 등록 이후 변경될 수 있는 실제 슬롯 범위 확인
		if (Light.MaterialIndex < 0 || Light.MaterialIndex >= Light.MeshComponent->GetNumMaterials())
		{
			// 더 이상 존재하지 않는 슬롯 등록 생략
			continue;
		}
		// 현재 메시 슬롯과 BP 머티리얼 설정 추가
		Lights.Add(Light);
	}
	// 서버는 최신 집계 확정 이후 한 번만 원본 보관과 재질 적용
	if (HasAuthority() == true)
	{
		// 로컬 적용 콜백 이후 오래된 설정을 다시 등록하는 순서 역전 방지
		Manager->UpdatePowerSource(this, MoveTemp(Lights));
	}
	else
	{
		// 클라이언트는 수신한 현재 집계의 로컬 재질만 적용
		Manager->RegisterLocalPowerLights(MoveTemp(Lights));
	}
}

void AMD_PowerConduit::OnUtilityStateChangedLocal(const EMD_UtilityState NewState)
{
	UWorld* const World = GetWorld();
	UMD_AudioSubsystem* const Audio = World ? World->GetSubsystem<UMD_AudioSubsystem>() : nullptr;
	if (!Audio)
	{
		return;
	}

	if (NewState != EMD_UtilityState::Broken)
	{
		// 수리·초기 상태. 스파크가 돌고 있으면 끊는다. 무효 핸들이면 StopHandle 이 무시한다.
		Audio->StopHandle(SparkLoopHandle, SparkStopFadeSeconds);
		SparkLoopHandle = FMD_AudioHandle();
		return;
	}

	// 파열 지점에서 재생한다. BreakLocation 은 복제 속성이고 RepNotify 보다 먼저 적용된다.
	const FVector BreakWorldLocation = BreakLocation;

	// 처음부터 고장인 전선은 "끊기는 순간" 원샷이 없다. 스파크 루프만 켠다
	if (!bStartBroken && CutSoundTag.IsValid())
	{
		Audio->PlayEventAtLocation(CutSoundTag, BreakWorldLocation);
	}

	if (SparkLoopSoundTag.IsValid())
	{
		// 이미 돌고 있으면 겹치지 않게 먼저 끊는다.
		Audio->StopHandle(SparkLoopHandle, 0.0f);
		SparkLoopHandle = Audio->PlayEventAtLocation(SparkLoopSoundTag, BreakWorldLocation);
	}
}

void AMD_PowerConduit::OnBroken(AActor* Cutter)
{
	// 절단 즉시 감점. 처음부터 고장(Cutter == nullptr)이면 감점 없음
	if (Cutter != nullptr)
	{
		if (AMD_GameStateBase* GS = GetWorld()->GetGameState<AMD_GameStateBase>())
		{
			GS->AddScoreEvent(MDGameplayTags::Score_Penalty_CableCut);
		}
	}

	// 담당 조명 그룹 소등 (연출은 BP)
	SetPowerGroupEnabled(false);
}

void AMD_PowerConduit::OnRepaired(AActor* Repairer)
{
	// 절연테이프 복구: 재점등
	SetPowerGroupEnabled(true);
}
