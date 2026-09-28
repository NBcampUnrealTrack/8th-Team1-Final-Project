#pragma once

#include "StateTreeConditionBase.h"
#include "STC_TrashSorter.generated.h"

class AActor;
class AMD_TrashSorter;

USTRUCT()
struct FSTC_TrashSorterInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AMD_TrashSorter> Sorter = nullptr;
};

USTRUCT()
struct FSTC_TrashSorterHeldItemInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AMD_TrashSorter> Sorter = nullptr;

	/** Bind the requesting player, never player 0 or a globally focused actor. */
	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<AActor> Interactor = nullptr;
};

USTRUCT(meta = (DisplayName = "STC_TrashSorterCanAcceptHeldItem", Category = "MD|Trash"))
struct MARINEDEMOLITION_API FSTC_TrashSorterCanAcceptHeldItem : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()
	using FInstanceDataType = FSTC_TrashSorterHeldItemInstanceData;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;

	UPROPERTY(EditAnywhere, Category = "Parameter")
	bool bInvert = false;
};

USTRUCT(meta = (DisplayName = "STC_TrashSorterHasPendingRequest", Category = "MD|Trash"))
struct MARINEDEMOLITION_API FSTC_TrashSorterHasPendingRequest : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()
	using FInstanceDataType = FSTC_TrashSorterInstanceData;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;
};

USTRUCT(meta = (DisplayName = "STC_TrashSorterIsSorting", Category = "MD|Trash"))
struct MARINEDEMOLITION_API FSTC_TrashSorterIsSorting : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()
	using FInstanceDataType = FSTC_TrashSorterInstanceData;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;

	UPROPERTY(EditAnywhere, Category = "Parameter")
	bool bInvert = false;
};

/** No result never matches, even when bInvert is true (incorrect-sort branch). */
USTRUCT(meta = (DisplayName = "STC_TrashSorterWasCorrectSort", Category = "MD|Trash"))
struct MARINEDEMOLITION_API FSTC_TrashSorterWasCorrectSort : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()
	using FInstanceDataType = FSTC_TrashSorterInstanceData;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;

	UPROPERTY(EditAnywhere, Category = "Parameter")
	bool bInvert = false;
};
