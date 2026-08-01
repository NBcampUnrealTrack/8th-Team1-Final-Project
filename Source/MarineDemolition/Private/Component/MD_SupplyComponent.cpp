#include "Component/MD_SupplyComponent.h"

#include "Net/UnrealNetwork.h"

UMD_SupplyComponent::UMD_SupplyComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UMD_SupplyComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UMD_SupplyComponent, SupportCount);
}

bool UMD_SupplyComponent::TryConsumeSupport()
{
	if (SupportCount <= 0)
	{
		return false;
	}

	--SupportCount;
	// 리슨 서버는 OnRep이 안 오므로 직접 방송
	OnRep_SupportCount();
	return true;
}

void UMD_SupplyComponent::AddSupport(int32 Amount)
{
	if (Amount <= 0)
	{
		return;
	}

	SupportCount += Amount;
	OnRep_SupportCount();
}

void UMD_SupplyComponent::OnRep_SupportCount()
{
	OnSupplyChanged.Broadcast(SupportCount);
}
