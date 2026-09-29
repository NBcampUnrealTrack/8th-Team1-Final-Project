#pragma once

#include "StateTreeTaskBase.h"
#include "STT_TrashSorter.generated.h"

class AMD_TrashSorter;

USTRUCT()
struct FSTT_TrashSorterInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AMD_TrashSorter> Sorter = nullptr;
};

/** Opens intake. The asset routes the sorter's Request/Rejected events. */
USTRUCT(meta = (DisplayName = "STT_TrashSorterReady", Category = "MD|Trash"))
struct MARINEDEMOLITION_API FSTT_TrashSorterReady : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()
	using FInstanceDataType = FSTT_TrashSorterInstanceData;

	FSTT_TrashSorterReady();
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
};

/** Starts/stops the sorter's work. Pair with Delay and Tasks Completion = Any; no gameplay logic here. */
USTRUCT(meta = (DisplayName = "STT_TrashSorterSorting", Category = "MD|Trash"))
struct MARINEDEMOLITION_API FSTT_TrashSorterSorting : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()
	using FInstanceDataType = FSTT_TrashSorterInstanceData;

	FSTT_TrashSorterSorting();
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
};

/** Starts optional feedback. Pair with Delay and Tasks Completion = Any to return to Ready. */
USTRUCT(meta = (DisplayName = "STT_TrashSorterError", Category = "MD|Trash"))
struct MARINEDEMOLITION_API FSTT_TrashSorterError : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()
	using FInstanceDataType = FSTT_TrashSorterInstanceData;

	FSTT_TrashSorterError();
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
};
