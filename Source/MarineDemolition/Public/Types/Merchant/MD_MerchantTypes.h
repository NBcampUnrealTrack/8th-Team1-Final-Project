
#pragma once

#include "CoreMinimal.h"
#include "Engine/AssetManagerTypes.h"
#include "MD_MerchantTypes.generated.h"

UENUM(BlueprintType)
enum class EMD_QuestObjectiveType : uint8
{
	RecoverIntact UMETA(DisplayName = "회수"),
	RecoverDamaged UMETA(DisplayName = "손상 허용 회수"),
	Cleanup        UMETA(DisplayName = "청소 계약"),
	Sealing        UMETA(DisplayName = "마감 계약"),
	Composite      UMETA(DisplayName = "복합"),
	Interaction    UMETA(DisplayName = "NPC 상호작용")
};
