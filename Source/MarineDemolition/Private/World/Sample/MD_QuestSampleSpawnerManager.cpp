#include "World/Sample/MD_QuestSampleSpawnerManager.h"

#include "Data/Merchant/MD_QuestDefinition.h"
#include "Util/MD_LogUtility.h"
#include "World/Sample/MD_QuestSampleSpawner.h"

void UMD_QuestSampleSpawnerManager::Deinitialize()
{
	// 월드 종료 후 등록 완료 콜백이 실행되지 않도록 예약된 타이머를 해제한다.
	UWorld* World = GetWorld();
	bool bIsWorldValid = IsValid(World);
	if (bIsWorldValid == true)
	{
		World->GetTimerManager().ClearTimer(RegistrationCompletionTimerHandle);
		// 이전 월드의 구버전 복원 예약 정리
		World->GetTimerManager().ClearTimer(LegacyQuestRecoveryTimerHandle);
		// 종료된 월드의 준비 대기 상태 해제
		bLegacyQuestRecoveryPending = false;
	}

	// 다음 월드에서 이전 등록 객체가 재사용되지 않도록 목록을 비운다.
	RegisteredSpawners.Empty();

	// 종료된 월드의 비동기 생성 요청을 모두 제거한다.
	PendingSpawnRequests.Empty();

	// 다음 월드에서 이전 선택이 재사용되지 않도록 캐시를 비운다.
	SelectedSpawners.Empty();

	// 종료 상태에서 등록 완료 플래그가 유지되지 않도록 초기화한다.
	bRegistrationComplete = false;
	// 다음 월드의 등록 완료 예약을 막지 않도록 예약 상태를 초기화한다.
	bRegistrationCompletionScheduled = false;
	// 종료된 월드의 타이머 식별자가 남지 않도록 무효화한다.
	RegistrationCompletionTimerHandle.Invalidate();

	// 기반 월드 서브시스템의 종료 처리를 실행한다.
	Super::Deinitialize();
}

void UMD_QuestSampleSpawnerManager::RegisterSpawner(AMD_QuestSampleSpawner* Spawner)
{
	// 유효하지 않은 객체가 후보 목록에 들어가지 않도록 차단한다.
	bool bIsSpawnerValid = IsValid(Spawner);
	if (bIsSpawnerValid == false)
	{
		return;
	}

	// 기존 약한 포인터를 정리하면서 같은 객체의 중복 등록 여부를 확인한다.
	for (int32 Index = RegisteredSpawners.Num() - 1; Index >= 0; --Index)
	{
		// 파괴된 스포너 참조는 등록 목록에서 제거한다.
		bool bIsRegisteredSpawnerValid = RegisteredSpawners[Index].IsValid();
		if (bIsRegisteredSpawnerValid == false)
		{
			RegisteredSpawners.RemoveAtSwap(Index);
			continue;
		}

		// 이미 등록된 같은 스포너는 다시 추가하지 않는다.
		bool bIsAlreadyRegistered = RegisteredSpawners[Index].Get() == Spawner;
		if (bIsAlreadyRegistered == true)
		{
			return;
		}
	}

	// 초기화가 완료된 스포너를 무작위 선택 후보에 추가한다.
	RegisteredSpawners.Add(Spawner);
	// PIE 로그에서 스포너 등록 시점과 전체 후보 수를 확인할 수 있도록 기록한다.
	UE_LOG(MarineDemolition, Verbose, TEXT("%s [QuestSampleSpawnerManager] %s 등록, 전체 %d개"), *UE_CALL_LOCATIONINFO, *Spawner->GetName(), RegisteredSpawners.Num());

	// 스트리밍 추가 등록도 완료 전 상태로 전환해 생성 요청이 전체 후보 등록을 기다리게 한다.
	bRegistrationComplete = false;
	// 새 후보가 기존 퀘스트의 무작위 선택에도 참여하도록 이전 선택 결과를 비운다.
	SelectedSpawners.Empty();

	// 마지막 등록 이후 다음 틱에 완료되도록 기존 예약을 갱신한다.
	UWorld* World = GetWorld();
	bool bIsWorldValid = IsValid(World);
	if (bIsWorldValid == true)
	{
		// 같은 프레임이나 연속 프레임의 추가 등록이 있으면 완료 시점을 다시 미룬다.
		if (bRegistrationCompletionScheduled == true)
		{
			World->GetTimerManager().ClearTimer(RegistrationCompletionTimerHandle);
		}

		// 현재 등록을 마지막으로 간주하고 다음 틱 완료 콜백을 새로 예약한다.
		bRegistrationCompletionScheduled = true;
		RegistrationCompletionTimerHandle = World->GetTimerManager().SetTimerForNextTick(this, &UMD_QuestSampleSpawnerManager::CompleteSpawnerRegistration);
	}
}

void UMD_QuestSampleSpawnerManager::UnregisterSpawner(AMD_QuestSampleSpawner* Spawner)
{
	// 등록 목록에서 종료 객체와 이미 무효화된 참조를 함께 제거한다.
	for (int32 Index = RegisteredSpawners.Num() - 1; Index >= 0; --Index)
	{
		// 무효 참조는 항상 정리한다.
		bool bIsRegisteredSpawnerValid = RegisteredSpawners[Index].IsValid();
		if (bIsRegisteredSpawnerValid == false)
		{
			RegisteredSpawners.RemoveAtSwap(Index);
			continue;
		}

		// 종료되는 스포너와 일치하는 등록 항목을 제거한다.
		bool bMatchesUnregisteringSpawner = RegisteredSpawners[Index].Get() == Spawner;
		if (bMatchesUnregisteringSpawner == true)
		{
			RegisteredSpawners.RemoveAtSwap(Index);
		}
	}

	// 등록 완료 전 대기 중이던 해당 스포너의 생성 요청도 함께 제거한다.
	for (int32 Index = PendingSpawnRequests.Num() - 1; Index >= 0; --Index)
	{
		// 무효화된 대기 요청을 정리한다.
		bool bIsPendingSpawnerValid = PendingSpawnRequests[Index].IsValid();
		if (bIsPendingSpawnerValid == false)
		{
			PendingSpawnRequests.RemoveAtSwap(Index);
			continue;
		}

		// 종료되는 스포너가 보낸 요청을 완료 대기열에서 제거한다.
		bool bMatchesUnregisteringSpawner = PendingSpawnRequests[Index].Get() == Spawner;
		if (bMatchesUnregisteringSpawner == true)
		{
			PendingSpawnRequests.RemoveAtSwap(Index);
		}
	}

	// 종료되는 스포너를 가리키는 퀘스트별 선택 결과도 함께 무효화한다.
	for (TMap<FString, TWeakObjectPtr<AMD_QuestSampleSpawner>>::TIterator It = SelectedSpawners.CreateIterator(); It; ++It)
	{
		// 이미 무효화된 선택 결과를 제거한다.
		bool bIsSelectedSpawnerValid = It.Value().IsValid();
		if (bIsSelectedSpawnerValid == false)
		{
			It.RemoveCurrent();
			continue;
		}

		// 종료되는 스포너가 선택된 퀘스트는 다음 요청에서 다시 무작위 선택하도록 제거한다.
		bool bMatchesUnregisteringSpawner = It.Value().Get() == Spawner;
		if (bMatchesUnregisteringSpawner == true)
		{
			It.RemoveCurrent();
		}
	}
}

bool UMD_QuestSampleSpawnerManager::RequestSpawnEvaluation(AMD_QuestSampleSpawner* RequestingSpawner)
{
	// 유효하지 않은 스포너의 생성 요청은 처리하지 않는다.
	bool bIsRequestingSpawnerValid = IsValid(RequestingSpawner);
	if (bIsRequestingSpawnerValid == false)
	{
		return false;
	}

	// 등록이 모두 끝난 뒤 들어온 요청은 대기 없이 즉시 실행하도록 허용한다.
	if (bRegistrationComplete == true)
	{
		return true;
	}

	// 등록 중 같은 스포너가 여러 알림을 받아도 대기 요청은 하나만 유지한다.
	PendingSpawnRequests.AddUnique(RequestingSpawner);
	// 호출자가 현재 프레임의 생성을 중단하고 등록 완료 콜백을 기다리도록 알린다.
	return false;
}

void UMD_QuestSampleSpawnerManager::CompleteSpawnerRegistration()
{
	// 예약된 비동기 완료 작업이 실행되었음을 먼저 기록한다.
	bRegistrationCompletionScheduled = false;
	// 실행이 끝난 타이머 식별자를 다음 등록 예약에 사용하지 않도록 무효화한다.
	RegistrationCompletionTimerHandle.Invalidate();
	// 이후 생성 요청이 즉시 실행될 수 있도록 등록 완료 상태를 확정한다.
	bRegistrationComplete = true;

	// 콜백 중 새로운 요청이 들어와도 현재 처리 배열이 변경되지 않도록 복사한다.
	TArray<TWeakObjectPtr<AMD_QuestSampleSpawner>> RequestsToProcess = PendingSpawnRequests;
	// 같은 요청이 다시 대기열에 남지 않도록 원본 배열을 먼저 비운다.
	PendingSpawnRequests.Empty();

	// 스트리밍 등록 후 새 무작위 선택 결과의 대상이 반드시 실행되도록 전체 등록 목록을 복사한다.
	TArray<TWeakObjectPtr<AMD_QuestSampleSpawner>> RegisteredSpawnersToEvaluate = RegisteredSpawners;
	// 등록이 완료된 모든 유효한 스포너의 생성 검사를 다시 실행한다.
	for (TWeakObjectPtr<AMD_QuestSampleSpawner>& RegisteredSpawner : RegisteredSpawnersToEvaluate)
	{
		// 종료되지 않은 등록 객체만 퀘스트 생성 평가를 실행한다.
		bool bIsRegisteredSpawnerValid = RegisteredSpawner.IsValid();
		if (bIsRegisteredSpawnerValid == false)
		{
			continue;
		}

		// 전체 후보가 준비된 상태에서 퀘스트 생성 검사를 다시 시작한다.
		RegisteredSpawner->TrySpawnForQuestState();
	}

	// PIE 로그에서 비동기 등록 완료 시점과 처리된 요청 수를 확인할 수 있도록 기록한다.
	UE_LOG(MarineDemolition, Verbose, TEXT("%s [QuestSampleSpawnerManager] 등록 완료, 스포너 %d개 평가, 대기 요청 %d개 해제"), *UE_CALL_LOCATIONINFO, RegisteredSpawnersToEvaluate.Num(), RequestsToProcess.Num());
}

bool UMD_QuestSampleSpawnerManager::IsSelectedSpawner(AMD_QuestSampleSpawner* RequestingSpawner, UMD_QuestDefinition& Definition, int32 PhaseIndex)
{
	// 유효하지 않은 요청 스포너는 선택 대상에서 제외한다.
	bool bIsRequestingSpawnerValid = IsValid(RequestingSpawner);
	if (bIsRequestingSpawnerValid == false)
	{
		return false;
	}

	// 퀘스트와 현재 Phase를 식별하는 캐시 키를 생성한다.
	FString SelectionKey = BuildSelectionKey(Definition, PhaseIndex);
	// 앞서 선택한 스포너가 아직 유효하면 같은 결과를 재사용한다.
	TWeakObjectPtr<AMD_QuestSampleSpawner>* CachedSpawner = SelectedSpawners.Find(SelectionKey);
	if (CachedSpawner != nullptr)
	{
		// 스트리밍 해제나 파괴로 사라지지 않은 선택 결과를 비교한다.
		bool bIsCachedSpawnerValid = CachedSpawner->IsValid();
		if (bIsCachedSpawnerValid == true)
		{
			// 요청 스포너가 저장된 대표 스포너인지 반환한다.
			bool bIsSelectedSpawner = CachedSpawner->Get() == RequestingSpawner;
			return bIsSelectedSpawner;
		}

		// 유효하지 않은 약한 포인터는 새 선택을 위해 제거한다.
		SelectedSpawners.Remove(SelectionKey);
	}

	// 현재 월드의 같은 Zone 후보 중 하나를 새 대표 스포너로 선택한다.
	AMD_QuestSampleSpawner* SelectedSpawner = SelectRandomSpawner(Definition, PhaseIndex);
	// 선택된 스포너가 있으면 이후 이벤트에서도 재사용하도록 저장한다.
	bool bIsSelectedSpawnerValid = IsValid(SelectedSpawner);
	if (bIsSelectedSpawnerValid == true)
	{
		SelectedSpawners.Add(SelectionKey, SelectedSpawner);
		// PIE 로그에서 퀘스트 Phase별 무작위 선택 결과를 확인할 수 있도록 기록한다.
		UE_LOG(MarineDemolition, Log, TEXT("%s [QuestSampleSpawnerManager] %s에 %s 선택"), *UE_CALL_LOCATIONINFO, *SelectionKey, *SelectedSpawner->GetName());
	}

	// 이번 요청을 보낸 스포너가 무작위 선택 결과인지 반환한다.
	bool bIsRequestingSpawnerSelected = SelectedSpawner == RequestingSpawner;
	return bIsRequestingSpawnerSelected;
}

FString UMD_QuestSampleSpawnerManager::BuildSelectionKey(UMD_QuestDefinition& Definition, int32 PhaseIndex)
{
	// 동일한 Definition의 서로 다른 Phase가 독립적으로 스포너를 선택하도록 키를 구성한다.
	FString SelectionKey = FString::Printf(TEXT("%s:%d"), *Definition.GetPrimaryAssetId().ToString(), PhaseIndex);
	return SelectionKey;
}

AMD_QuestSampleSpawner* UMD_QuestSampleSpawnerManager::SelectRandomSpawner(UMD_QuestDefinition& Definition, int32 PhaseIndex)
{
	// 명시적으로 등록된 객체 중 조건을 만족하는 스포너를 수집한다.
	TArray<AMD_QuestSampleSpawner*> CandidateSpawners;
	// 등록 과정에서 저장한 약한 포인터 목록을 순회한다.
	for (int32 Index = RegisteredSpawners.Num() - 1; Index >= 0; --Index)
	{
		// 파괴된 등록 객체를 정리하고 후보에서 제외한다.
		bool bIsCandidateValid = RegisteredSpawners[Index].IsValid();
		if (bIsCandidateValid == false)
		{
			RegisteredSpawners.RemoveAtSwap(Index);
			continue;
		}

		// 유효한 약한 포인터에서 Zone 판정에 사용할 스포너를 가져온다.
		AMD_QuestSampleSpawner* CandidateSpawner = RegisteredSpawners[Index].Get();
		// 현재 퀘스트 Phase의 Zone을 담당하지 않는 스포너를 제외한다.
		bool bMatchesSpawnZone = CandidateSpawner->MatchesSpawnZone(Definition, PhaseIndex);
		if (bMatchesSpawnZone == false)
		{
			continue;
		}

		// 모든 조건을 통과한 스포너를 무작위 선택 후보에 추가한다.
		CandidateSpawners.Add(CandidateSpawner);
	}

	// 후보가 없으면 선택 결과 없이 종료한다.
	if (CandidateSpawners.Num() == 0)
	{
		return nullptr;
	}

	// 서버에서 후보 배열 범위 내의 무작위 인덱스를 선택한다.
	int32 SelectedIndex = FMath::RandRange(0, CandidateSpawners.Num() - 1);
	// 선택한 스포너를 호출자에게 반환한다.
	AMD_QuestSampleSpawner* SelectedSpawner = CandidateSpawners[SelectedIndex];
	return SelectedSpawner;
}

void UMD_QuestSampleSpawnerManager::ScheduleLegacyQuestRecovery(bool bRecoverLegacyItems)
{
	// 동일 월드에서 여러 파일을 연속 적용할 때 이전 예약 교체
	GetWorld()->GetTimerManager().ClearTimer(LegacyQuestRecoveryTimerHandle);
	// 최신 생존 목록을 적용할 때 이전 준비 대기 상태도 해제
	bLegacyQuestRecoveryPending = bRecoverLegacyItems;
	// 생존 목록이 없는 기존 파일에서만 스포너 복구 허용
	if (bRecoverLegacyItems == true)
	{
		// 파일 적용 중 생성 억제가 해제된 이후 한 번만 실행
		LegacyQuestRecoveryTimerHandle = GetWorld()->GetTimerManager().SetTimerForNextTick(this, &ThisClass::RecoverLegacyQuestItems);
	}
}

void UMD_QuestSampleSpawnerManager::RecoverLegacyQuestItems()
{
	// 스포너 등록이 끝나기 전 자동 저장으로 빈 생존 목록이 확정되는 상황 방지
	if (bRegistrationComplete == false && RegisteredSpawners.IsEmpty() == false)
	{
		// 마지막 등록 완료 콜백 이후 복구를 한 번 더 예약
		ScheduleLegacyQuestRecovery(true);
		// 실제 복구 전 준비 상태 유지
		return;
	}
	// 완료된 예약을 후속 로드에서 재사용하지 않도록 초기화
	LegacyQuestRecoveryTimerHandle.Invalidate();
	// 모든 스포너의 동기 평가가 끝나는 현재 틱의 대기 상태 해제
	bLegacyQuestRecoveryPending = false;
	// 생성 콜백 중 등록 목록이 변경되어도 순회를 유지할 후보 목록
	TArray<TWeakObjectPtr<AMD_QuestSampleSpawner>> Spawners = RegisteredSpawners;
	// 전역 액터 검색 없이 이미 등록된 스포너만 재평가
	for (TWeakObjectPtr<AMD_QuestSampleSpawner>& Spawner : Spawners)
	{
		// 구역 해제 또는 월드 종료로 사라진 후보 제외
		if (Spawner.IsValid() == false)
		{
			// 종료된 스포너 처리 생략
			continue;
		}
		// 이전 플레이 시점의 메인 퀘스트 생성 캐시 제거
		Spawner->LastSpawnedMainPhaseIndex = INDEX_NONE;
		// 동일 단계 번호의 다른 퀘스트도 현재 저장 상태로 재평가
		Spawner->LastSpawnedMainQuestDefinitionId = FPrimaryAssetId();
		// 저장된 진행도와 현재 생존 수량을 제외한 부족 물품만 생성
		Spawner->TrySpawnForQuestState();
	}
}
