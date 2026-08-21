// Fill out your copyright notice in the Description page of Project Settings.


#include "Ability/Wall/MD_GA_WallScan.h"

#include "GameFramework/Pawn.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "World/Wall/MD_CuttableWall.h"

void UMD_GA_WallScan::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
                                      const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
                                      const FGameplayEventData* TriggerEventData)
{
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	if (AMD_CuttableWall* Wall = GetAimedWall(ActorInfo))
	{
		// 스캐너를 전달 → 실루엣은 스캔한 플레이어 로컬 클라에서만 노출
		Wall->ScanHighlight(ScanDuration, ActorInfo->AvatarActor.Get());

		// 스윕 연출 시작 — MPC는 월드 전역이라 스캔한 플레이어 로컬 머신에서만 세팅
		const APawn* Avatar = Cast<APawn>(ActorInfo->AvatarActor.Get());
		if (ScanMPC && Avatar && Avatar->IsLocallyControlled())
		{
			if (UMaterialParameterCollectionInstance* MPCInst = GetWorld()->GetParameterCollectionInstance(ScanMPC))
			{
				FVector Origin, Extent;
				Wall->GetActorBounds(false, Origin, Extent);
				// 머티리얼 Time 노드와 같은 게임 시간 기준
				MPCInst->SetScalarParameterValue(TEXT("ScanStartTime"), GetWorld()->GetTimeSeconds());
				MPCInst->SetVectorParameterValue(TEXT("ScanBoxCenter"), FLinearColor(Origin));
				// 격자가 벽 가장자리에서 잘리지 않게 여유 패딩
				MPCInst->SetVectorParameterValue(TEXT("ScanBoxExtent"), FLinearColor(Extent + FVector(30.f)));
			}
		}
	}

	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}
