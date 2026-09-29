#include "Gameplay/Trash/StateTree/STC_TrashSorter.h"

#include "StateTreeExecutionContext.h"
#include "World/Trash/MD_TrashSorter.h"

bool FSTC_TrashSorterCanAcceptHeldItem::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);
	return IsValid(Data.Sorter) && IsValid(Data.Interactor)
		&& (Data.Sorter->CanAcceptHeldItem(Data.Interactor) != bInvert);
}

bool FSTC_TrashSorterHasPendingRequest::TestCondition(FStateTreeExecutionContext& Context) const
{
	const AMD_TrashSorter* Sorter = Context.GetInstanceData(*this).Sorter;
	return IsValid(Sorter) && Sorter->HasPendingSortRequest();
}

bool FSTC_TrashSorterIsSorting::TestCondition(FStateTreeExecutionContext& Context) const
{
	const AMD_TrashSorter* Sorter = Context.GetInstanceData(*this).Sorter;
	return IsValid(Sorter) && (Sorter->IsSorting() != bInvert);
}

bool FSTC_TrashSorterWasCorrectSort::TestCondition(FStateTreeExecutionContext& Context) const
{
	const AMD_TrashSorter* Sorter = Context.GetInstanceData(*this).Sorter;
	return IsValid(Sorter) && Sorter->HasSortResult() && (Sorter->WasCorrectSort() != bInvert);
}
