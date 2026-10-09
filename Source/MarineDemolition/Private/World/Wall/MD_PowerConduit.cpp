#include "World/Wall/MD_PowerConduit.h"

#include "Game/MD_GameStateBase.h"
#include "Component/Facility/MD_FacilityRepairComponent.h"
#include "Component/Facility/MD_FacilityFaultManagerComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameplayTags/MD_GameplayTags.h"
#include "TimerManager.h"

AMD_PowerConduit::AMD_PowerConduit()
{
	FacilityRepair->Kind = EMD_FacilityKind::WiringPanel;
	FacilityRepair->bAllowManualRepair = true;
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
	Super::BeginPlay();
	// 초기 복제와 BeginPlay 순서에 관계없이 BP 메시 슬롯 설정 등록
	OnUtilityStateChangedVisual(UtilityState);
}

void AMD_PowerConduit::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
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
	// 게임 월드에 속한 전선의 머티리얼 연결만 허용
	if (IsValid(GetWorld()) == false || GetWorld()->IsGameWorld() == false || IsActorBeingDestroyed() == true)
	{
		// 편집기와 종료 중인 전선의 상태 변경 차단
		return;
	}
	// BP에서 참조를 변경한 경우 이전 메시 슬롯까지 서버 집계에서 갱신
	ControlledPowerLights.Reset();
	// 클래스의 공통 슬롯과 머티리얼 설정을 적용할 컴포넌트 목록 순회
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
		// 해당 액터에 부착된 지정 스태틱 메시 컴포넌트 해석
		UStaticMeshComponent* MeshComponent = Cast<UStaticMeshComponent>(MeshReference.GetComponent(this));
		// 다른 월드와 종료 중인 메시 참조 제외
		if (IsValid(MeshComponent) == false || MeshComponent->GetWorld() != GetWorld() || MeshComponent->IsBeingDestroyed() == true)
		{
			// 현재 월드의 유효 메시만 등록
			continue;
		}
		// 존재하지 않는 슬롯의 머티리얼 추가 및 교체 방지
		if (MaterialIndex < 0 || MaterialIndex >= MeshComponent->GetNumMaterials())
		{
			// 현재 메시의 유효한 슬롯만 등록
			continue;
		}
		// 같은 컴포넌트와 슬롯을 반복 지정한 전선의 사건 중복 집계 방지
		if (ControlledPowerLights.ContainsByPredicate([this, MeshComponent](FMD_PowerLightState& State)
			{
				// 같은 컴포넌트와 슬롯의 기존 등록 여부 확인
				return State.MeshComponent == MeshComponent && State.MaterialIndex == MaterialIndex;
			}) == true)
		{
			// 같은 전선에서는 최초 슬롯 설정만 사용
			continue;
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
	// 현재 전선 고장과 메시 슬롯 설정을 준비된 관리자에 전달
	RegisterPowerLights();
}

void AMD_PowerConduit::RegisterPowerLights()
{
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
		// 게임 규칙의 제한 시간 없이 다음 프레임에 연결 재확인
		PowerRegistrationTimer = World->GetTimerManager().SetTimerForNextTick(this, &ThisClass::RegisterPowerLights);
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
	// 각 머신의 최초 슬롯 머티리얼 보관과 복제 집계 적용
	Manager->RegisterLocalPowerLights(Lights);
	// 서버에서만 현재 전선 고장의 실제 메시 슬롯 집계 변경
	if (HasAuthority() == true)
	{
		// 표시용 BP 인자 대신 서버 시설 Snapshot 기준으로 집계
		Manager->UpdatePowerSource(this, Lights);
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
