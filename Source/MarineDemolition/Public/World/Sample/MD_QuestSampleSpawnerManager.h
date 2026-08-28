#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MD_QuestSampleSpawnerManager.generated.h"

class AMD_QuestSampleSpawner;
class UMD_QuestDefinition;

/**
 * 퀘스트와 Phase에 맞는 스포너 후보 중 하나를 무작위로 선택하고 월드 수명 동안 유지한다.
 */
UCLASS()
class MARINEDEMOLITION_API UMD_QuestSampleSpawnerManager : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Deinitialize() override;

protected:
	// 초기화가 완료되어 생성 요청을 처리할 수 있는 스포너 목록을 저장한다.
	TArray<TWeakObjectPtr<AMD_QuestSampleSpawner>> RegisteredSpawners;

	// 초기 또는 스트리밍 등록 과정 중 들어온 생성 요청을 등록 완료 뒤 다시 실행하기 위해 저장한다.
	TArray<TWeakObjectPtr<AMD_QuestSampleSpawner>> PendingSpawnRequests;

	// 같은 퀘스트 Phase가 반복 검사되어도 동일한 스포너를 사용하도록 선택 결과를 저장한다.
	TMap<FString, TWeakObjectPtr<AMD_QuestSampleSpawner>> SelectedSpawners;

	// 현재 스포너 등록 구간이 끝났는지 나타낸다.
	bool bRegistrationComplete = false;

	// 등록 완료 처리가 다음 틱에 중복 예약되지 않도록 나타낸다.
	bool bRegistrationCompletionScheduled = false;

	// 마지막 스포너 등록 이후 다음 틱으로 완료 시점을 갱신하기 위한 타이머 핸들이다.
	FTimerHandle RegistrationCompletionTimerHandle;

public:
	// 생성 또는 시작된 스포너를 무작위 선택 후보 목록에 등록한다.
	void RegisterSpawner(AMD_QuestSampleSpawner* Spawner);

	// 종료되는 스포너를 후보와 기존 선택 결과에서 제거한다.
	void UnregisterSpawner(AMD_QuestSampleSpawner* Spawner);

	// 등록 완료 상태면 즉시 생성을 허용하고 진행 중이면 비동기 대기열에 요청을 저장한다.
	bool RequestSpawnEvaluation(AMD_QuestSampleSpawner* RequestingSpawner);

	// 전달된 스포너가 해당 퀘스트 Phase를 담당하도록 무작위 선택되었는지 반환한다.
	bool IsSelectedSpawner(AMD_QuestSampleSpawner* RequestingSpawner, UMD_QuestDefinition& Definition, int32 PhaseIndex);

protected:
	// 마지막 등록 다음 틱에 스포너 등록을 완료하고 대기 중인 생성 요청을 실행한다.
	void CompleteSpawnerRegistration();

	// 퀘스트와 Phase를 고유하게 식별하는 캐시 키를 생성한다.
	FString BuildSelectionKey(UMD_QuestDefinition& Definition, int32 PhaseIndex);

	// 현재 월드에서 퀘스트 Zone과 일치하는 스포너 하나를 무작위로 선택한다.
	AMD_QuestSampleSpawner* SelectRandomSpawner(UMD_QuestDefinition& Definition, int32 PhaseIndex);
};
