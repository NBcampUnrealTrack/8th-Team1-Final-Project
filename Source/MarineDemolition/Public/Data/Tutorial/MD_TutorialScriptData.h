#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Types/Tutorial/MD_TutorialTypes.h"
#include "MD_TutorialScriptData.generated.h"

/**
 * 조작 안내 문구 한 줄. 진행기의 슬롯과는 **슬롯 이름 + 단계**로 잇는다.
 *
 * 한 슬롯 안에서 같은 단계가 두 번 나오지 않으므로 이 둘로 유일하게 지목된다.
 * 진행 순서(어떤 단계를 어떤 순서로 밟는지)는 흐름이라 진행기가 계속 들고 있고,
 * 여기 있는 것은 문구뿐이다.
 */
USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_TutorialGuideText
{
	GENERATED_BODY()

	/** 진행기 슬롯의 Slot Name 과 정확히 같아야 한다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Tutorial|안내")
	FName SlotName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Tutorial|안내")
	EMD_TutorialWallPhase Phase = EMD_TutorialWallPhase::Scrape;

	/** 배열에서 어느 줄인지 알아보기 위한 식별용. 현재 UI 에는 뜨지 않는다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Tutorial|안내")
	FText Title;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Tutorial|안내",
		meta = (MultiLine = "true"))
	FText Body;

	/** 조작 안내에는 보통 화자가 없다. 채우면 이름표가 뜬다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Tutorial|안내")
	FText Speaker;

	/**
	 * Cut 단계에서만 쓴다 — 인프라가 없는 자리를 쳤을 때 띄우는 경고.
	 * 자리 제한이 걸린 슬롯(배관·전선)의 Cut 줄에 적는다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Tutorial|안내",
		meta = (MultiLine = "true"))
	FText BlockedMessage;
};

/**
 * 튜토리얼 도입부 대본. 화자 대사를 에셋으로 분리해 둔다.
 *
 * 액터에 직접 적어두면 문구가 BP·레벨 인스턴스의 델타로만 존재해서,
 * 튜토리얼 구조체를 바꾸는 핫리로드 빌드에 델타가 초기화되면 대사가 통째로 날아간다
 * (실제로 두 번 겪었다). 에셋에 두면 그 경로를 아예 타지 않는다.
 */
UCLASS(BlueprintType)
class MARINEDEMOLITION_API UMD_TutorialScriptData : public UDataAsset
{
	GENERATED_BODY()

public:
	/**
	 * 이 대본의 기본 화자. 각 장의 Speaker가 비어 있으면 이 값을 쓴다 —
	 * 다섯 장에 같은 이름을 다섯 번 적지 않기 위한 것이다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Tutorial|대본")
	FText SpeakerName;

	/** 순서대로 흐르는 서술. Phase는 Narrative로 두고 DisplaySeconds만큼 띄운다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Tutorial|대본",
		meta = (TitleProperty = "Title"))
	TArray<FMD_TutorialGuideStep> IntroPages;

	/** 넘기기 안내 문구. 비우면 진행기에 적힌 값을 쓴다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Tutorial|대본")
	FText SkipHintText;

	/**
	 * 슬롯별 조작 안내 문구. 슬롯 이름 + 단계로 찾는다.
	 * 여기서 찾지 못한 단계는 진행기에 적힌 문구를 그대로 쓴다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Tutorial|안내",
		meta = (TitleProperty = "Title"))
	TArray<FMD_TutorialGuideText> GuideTexts;

	/** 조각벽을 엉뚱한 칸에 넣었을 때 띄우는 경고. 슬롯과 무관하게 공통이다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Tutorial|안내",
		meta = (MultiLine = "true"))
	FText MisSortNotice;

	/** 이 대본이 실제로 띄울 내용을 갖고 있는지 */
	UFUNCTION(BlueprintPure, Category = "MD|Tutorial|대본")
	bool HasPages() const { return IntroPages.Num() > 0; }

	/** 슬롯 이름 + 단계로 안내 문구를 찾는다. 없으면 nullptr */
	const FMD_TutorialGuideText* FindGuideText(
		FName SlotName,
		EMD_TutorialWallPhase Phase) const
	{
		for (const FMD_TutorialGuideText& Text : GuideTexts)
		{
			if (Text.SlotName == SlotName && Text.Phase == Phase)
			{
				return &Text;
			}
		}
		return nullptr;
	}
};
