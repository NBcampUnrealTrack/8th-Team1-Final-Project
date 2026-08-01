#include "Ability/MD_GA_PlaceSupport.h"

#include "Component/MD_SupplyComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameplayTags/MD_GameplayTags.h"
#include "Support/MD_SupportBrace.h"
#include "Wall/MD_CuttableWall.h"

namespace
{
	// 실패 사유를 기존 Cut()과 같은 방식으로 노출
	void ShowPlaceFailMessage(const FString& Reason)
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Yellow, Reason);
		}
	}
}

UMD_GA_PlaceSupport::UMD_GA_PlaceSupport()
{
	// 정밀 작업은 접지 상태에서만 (기획 §0.5)
	ActivationBlockedTags.AddTag(MDGameplayTags::State_Swimming);
}

void UMD_GA_PlaceSupport::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
                                          const FGameplayAbilityActorInfo* ActorInfo,
                                          const FGameplayAbilityActivationInfo ActivationInfo,
                                          const FGameplayEventData* TriggerEventData)
{
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	APawn* Pawn = Cast<APawn>(ActorInfo->AvatarActor.Get());
	AMD_CuttableWall* Wall = GetAimedWall(ActorInfo);

	// 설치 유효성 판정
	if (!Pawn || !Wall)
	{
		ShowPlaceFailMessage(TEXT("설치할 격벽을 조준하세요"));
		EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
		return;
	}

	if (Wall->GetWallType() != EMD_WallType::Strut)
	{
		ShowPlaceFailMessage(TEXT("지주격벽에만 설치할 수 있습니다"));
		EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
		return;
	}

	if (Wall->IsSupported())
	{
		ShowPlaceFailMessage(TEXT("이미 지지대가 설치된 격벽입니다"));
		EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
		return;
	}

	UMD_SupplyComponent* Supply = Pawn->FindComponentByClass<UMD_SupplyComponent>();
	if (!Supply || Supply->GetSupportCount() <= 0)
	{
		ShowPlaceFailMessage(TEXT("지지대가 없습니다"));
		EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
		return;
	}

	// 상태 변경은 서버 권위. 클라이언트는 복제로 결과를 받는다
	if (Pawn->HasAuthority())
	{
		if (Supply->TryConsumeSupport())
		{
			Wall->SetSupported(true);

			// 벽 앞, 설치자 쪽 바닥에 배치
			const FVector ToPlacer = (Pawn->GetActorLocation() - Wall->GetActorLocation()).GetSafeNormal2D();
			FVector SpawnLoc = Wall->GetActorLocation() + ToPlacer * PlacementOffsetCm;

			FHitResult FloorHit;
			if (Pawn->GetWorld()->LineTraceSingleByChannel(FloorHit, SpawnLoc, SpawnLoc - FVector(0, 0, 500.f), ECC_Visibility))
			{
				SpawnLoc.Z = FloorHit.ImpactPoint.Z;
			}

			FActorSpawnParameters SpawnParams;
			SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

			if (AMD_SupportBrace* Brace = Pawn->GetWorld()->SpawnActor<AMD_SupportBrace>(
				BraceClass, SpawnLoc, (-ToPlacer).Rotation(), SpawnParams))
			{
				Brace->SetSupportedWall(Wall);
			}
		}
	}

	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}
