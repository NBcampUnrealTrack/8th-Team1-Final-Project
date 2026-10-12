#include "World/Wall/MD_CoolingPump.h"

#include "Components/BoxComponent.h"
#include "Component/Facility/MD_FacilityRepairComponent.h"
#include "GameplayTags/MD_GameplayTags.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "UObject/ConstructorHelpers.h"

AMD_CoolingPump::AMD_CoolingPump()
{
	bStartBroken = false;
	FacilityRepair->Kind = EMD_FacilityKind::CoolingPump;
	FacilityRepair->bRandomFaultEligible = true;
	bLinkHostWalls = false;
	LinkTrigger->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	LinkTrigger->SetGenerateOverlapEvents(false);
	LinkTrigger->SetBoxExtent(FVector::ZeroVector);
	RequiredRepairToolTag = MDGameplayTags::Ability_UtilityRepair_Foam;
	// Foam projectiles own charge consumption. E must not provide a free repair.
	bRepairRequiresToolInput = true;

	ContaminatedWaterFX = CreateDefaultSubobject<UNiagaraComponent>(TEXT("ContaminatedWaterFX"));
	ContaminatedWaterFX->SetupAttachment(Root);
	ContaminatedWaterFX->SetAutoActivate(false);
	ContaminatedWaterFX->SetRelativeLocation(FVector(60.f, 0.f, 40.f));
	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> LeakFX(
		TEXT("/Game/VFX/04_Pipe/NS_Pipe_Leak.NS_Pipe_Leak"));
	if (LeakFX.Succeeded())
	{
		ContaminatedWaterFX->SetAsset(LeakFX.Object);
	}
}

void AMD_CoolingPump::BeginPlay()
{
	// V2 incidents replace the removed Day3 mission's serialized initial fault.
	// An explicit initial incident is configured on FacilityRepair.bStartBroken instead.
	bStartBroken = false;
	Super::BeginPlay();
	// Also handles an intact start and clients whose initial replication preceded BeginPlay.
	OnUtilityStateChangedLocal(GetUtilityState());
}

void AMD_CoolingPump::OnUtilityStateChangedLocal(EMD_UtilityState NewState)
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	if (NewState == EMD_UtilityState::Broken)
	{
		if (!ContaminatedWaterFX->IsActive())
		{
			ContaminatedWaterFX->Activate(true);
		}
	}
	else
	{
		ContaminatedWaterFX->DeactivateImmediate();
	}
}
