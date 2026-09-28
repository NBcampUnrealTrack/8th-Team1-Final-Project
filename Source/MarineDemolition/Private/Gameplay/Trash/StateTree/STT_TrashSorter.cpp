#include "Gameplay/Trash/StateTree/STT_TrashSorter.h"

#include "StateTreeExecutionContext.h"
#include "World/Trash/MD_TrashSorter.h"

FSTT_TrashSorterReady::FSTT_TrashSorterReady()
{
	bShouldCallTick = false;
	bShouldCallTickOnlyOnEvents = false;
	bShouldCopyBoundPropertiesOnTick = false;
	bShouldCopyBoundPropertiesOnExitState = false;
	bShouldStateChangeOnReselect = false;
}

EStateTreeRunStatus FSTT_TrashSorterReady::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	AMD_TrashSorter* Sorter = Context.GetInstanceData(*this).Sorter;
	return IsValid(Sorter) && Sorter->EnterReadyState() ? EStateTreeRunStatus::Running : EStateTreeRunStatus::Failed;
}

void FSTT_TrashSorterReady::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	if (AMD_TrashSorter* Sorter = Context.GetInstanceData(*this).Sorter; IsValid(Sorter))
	{
		Sorter->ExitReadyState();
	}
}

FSTT_TrashSorterSorting::FSTT_TrashSorterSorting()
{
	bShouldCallTick = false;
	bShouldCallTickOnlyOnEvents = false;
	bShouldCopyBoundPropertiesOnTick = false;
	bShouldCopyBoundPropertiesOnExitState = false;
	bShouldStateChangeOnReselect = false;
}

EStateTreeRunStatus FSTT_TrashSorterSorting::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	AMD_TrashSorter* Sorter = Context.GetInstanceData(*this).Sorter;
	if (!IsValid(Sorter) || !Sorter->BeginPendingSort()) return EStateTreeRunStatus::Failed;
	// Keep this lifecycle task alive. With Tasks Completion = Any, Delay succeeds the state,
	// while a failed start above still takes the state's On Failed transition.
	return EStateTreeRunStatus::Running;
}

void FSTT_TrashSorterSorting::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	if (AMD_TrashSorter* Sorter = Context.GetInstanceData(*this).Sorter; IsValid(Sorter))
	{
		Sorter->EndSortingState();
	}
}

FSTT_TrashSorterError::FSTT_TrashSorterError()
{
	bShouldCallTick = false;
	bShouldCallTickOnlyOnEvents = false;
	bShouldCopyBoundPropertiesOnTick = false;
	bShouldCopyBoundPropertiesOnExitState = false;
	bShouldStateChangeOnReselect = false;
}

EStateTreeRunStatus FSTT_TrashSorterError::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	AMD_TrashSorter* Sorter = Context.GetInstanceData(*this).Sorter;
	// Delay is the successful completion source; this task only invokes the optional hook.
	return IsValid(Sorter) && Sorter->BeginErrorState() ? EStateTreeRunStatus::Running : EStateTreeRunStatus::Failed;
}
