#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Templates/SubclassOf.h"
#include "MD_TutorialTypes.generated.h"

class AMD_CuttableWall;
class AMD_WallUtility;

/**
 * 격벽 튜토리얼 한 단계가 기다리는 플레이어 행동.
 * 완료 신호는 전부 기존 시스템에서 가져온다 — AMD_TutorialWallSpawner의 구독 표를 볼 것.
 */
UENUM(BlueprintType)
enum class EMD_TutorialWallPhase : uint8
{
	/** 지정한 박스 존 안으로 들어간다 (슬롯의 TargetZoneTag 로 존을 찾는다) */
	ReachZone UMETA(DisplayName = "지점 도달"),

	/** Reserved serialized value for the retired wall-cleaning tutorial phase. */
	Scrape UMETA(Hidden),

	/** 절단기로 벽을 철거한다 (AMD_CuttableWall::OnWallDemolished) */
	Cut UMETA(DisplayName = "철거"),

	/** 쓰레기통에 조각을 담는다 (AMD_TrashBin::GetContentCount) */
	Collect UMETA(DisplayName = "담기"),

	/** 조각을 맞는 칸에 분류한다 (AMD_TrashSorter::OnTrashSorted) */
	Sort UMETA(DisplayName = "분리"),

	/** 스캐너로 벽 속을 본다 (AMD_CuttableWall::OnWallScanned) */
	Scan UMETA(DisplayName = "스캔"),

	/** 파손된 인프라를 수리한다 (AMD_WallUtility::GetUtilityState) */
	Repair UMETA(DisplayName = "수리"),

	/**
	 * 플레이어 행동을 기다리지 않는 서술. DisplaySeconds 만큼 띄우고 저절로 넘어간다.
	 * 도입부 설명이 이걸로 되어 있다.
	 */
	Narrative UMETA(DisplayName = "서술"),
};

/** 안내 문구 한 장. Phase가 이 문구를 걷어내는 조건이다 */
USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_TutorialGuideStep
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Tutorial")
	EMD_TutorialWallPhase Phase = EMD_TutorialWallPhase::Cut;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Tutorial")
	FText Title;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Tutorial", meta = (MultiLine = "true"))
	FText Body;

	/** 말하는 사람. 비어 있으면 화자 표시를 숨긴다 (조작 안내에는 화자가 없다) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Tutorial")
	FText Speaker;

	/** Narrative 단계에서만 쓴다. 이 시간이 지나면 다음 장으로 넘어간다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Tutorial",
		meta = (ClampMin = "0.1", Units = "s",
			EditCondition = "Phase == EMD_TutorialWallPhase::Narrative"))
	float DisplaySeconds = 4.0f;

	FMD_TutorialGuideStep() = default;

	FMD_TutorialGuideStep(
		const EMD_TutorialWallPhase InPhase,
		FText InTitle,
		FText InBody,
		FText InSpeaker = FText(),
		const float InDisplaySeconds = 4.0f)
		: Phase(InPhase)
		, Title(MoveTemp(InTitle))
		, Body(MoveTemp(InBody))
		, Speaker(MoveTemp(InSpeaker))
		, DisplaySeconds(InDisplaySeconds)
	{
	}
};

/**
 * 벽 하나로 가르치는 한 묶음. 스포너가 이걸 순서대로 소화한다.
 * 트랜스폼은 전부 스포너 기준 상대값이다.
 */
USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_TutorialWallSlot
{
	GENERATED_BODY()

	/** 끄면 통째로 건너뛴다. 아직 못 만든 슬롯을 자리만 잡아둘 때 쓴다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Tutorial")
	bool bEnabled = true;

	/** 이 슬롯을 설명하는 이름. 로그와 디테일 패널 표시에만 쓴다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Tutorial")
	FName SlotName;

	/**
	 * ReachZone 단계가 찾을 박스 존의 액터 태그. 레벨의 TriggerBox 에 같은 태그를 달아둔다.
	 * 액터 태그를 쓰는 이유는 레벨 액터를 에셋(BP 기본값)에서 직접 참조할 수 없기 때문이다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Tutorial|지점 도달")
	FName TargetZoneTag;

	/** 벽이 필요한 단계가 있는 슬롯만 채운다. 이동 안내 슬롯은 비워 둔다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Tutorial|스폰")
	TSubclassOf<AMD_CuttableWall> WallClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Tutorial|스폰")
	FTransform WallRelativeTransform = FTransform::Identity;

	/** 벽 속에 심을 인프라. 비우면 안 심는다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Tutorial|스폰")
	TSubclassOf<AMD_WallUtility> UtilityClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Tutorial|스폰")
	FTransform UtilityRelativeTransform = FTransform::Identity;

	/**
	 * 켜면 인프라가 있는 자리 말고는 절단기가 먹지 않는다.
	 * 파열을 반드시 겪게 만들어 수리를 가르치는 슬롯용이다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Tutorial|절단 제한",
		meta = (EditCondition = "UtilityClass != nullptr"))
	bool bCutOnlyAtUtility = false;

	/** 인프라 바운즈에 더할 여유. 너무 좁으면 조준이 고문이 된다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Tutorial|절단 제한",
		meta = (EditCondition = "bCutOnlyAtUtility", ClampMin = "0.0", Units = "cm"))
	float UtilityCutTolerance = 40.0f;

	/** 허용되지 않은 자리를 칠 때 띄울 문구. 1초에 한 번만 나간다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Tutorial|절단 제한",
		meta = (EditCondition = "bCutOnlyAtUtility"))
	FText BlockedCutMessage;

	/** 수리에 성공하면 이 벽에서 나온 조각을 치워준다 (분리까지 시키지 않는 슬롯용) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Tutorial|정리")
	bool bDestroyPiecesOnRepair = false;

	/**
	 * Sort 단계를 끝내는 칸. 이 칸의 분류기에 조각벽을 정분류로 넣어야 넘어간다.
	 * 조각은 항상 깨끗하며 Score.Sorting.NonBurnable로 분류한다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Tutorial|정리",
		meta = (Categories = "Score.Sorting"))
	FGameplayTag ExpectedSortCategory;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Tutorial")
	TArray<FMD_TutorialGuideStep> Steps;
};
