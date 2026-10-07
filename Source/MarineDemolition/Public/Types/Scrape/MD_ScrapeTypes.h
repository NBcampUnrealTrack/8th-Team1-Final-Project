#pragma once

#include "CoreMinimal.h"
#include "MD_ScrapeTypes.generated.h"

/** Stable source-side identifier retained by saved contaminant drop records. */
UENUM(BlueprintType)
enum class EMD_ScrapeSurfaceFace : uint8
{
	Front = 0 UMETA(DisplayName = "앞면"),
	Back = 1 UMETA(DisplayName = "뒷면")
};

/** Shared reward ownership for placed contaminants and liquid spills. */
USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_ScrapeRecoveryLot
{
	GENERATED_BODY()

public:
	FMD_ScrapeRecoveryLot()
		: SourceId()
		, RewardClass(NAME_None)
		, CarrierAmount(0.0)
		, RewardUnits(0.0)
	{
	}

	/** 최초 발행원을 식별한다. 보상 가치가 0인 관리용 lot은 invalid Guid를 허용한다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Scrape|Recovery")
	FGuid SourceId;

	/** 서로 합산 가능한 보상 종류. 현재 표면 오염은 Contamination을 사용한다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Scrape|Recovery")
	FName RewardClass;

	/** 현재 운반체에서 이 lot이 차지하는 양. 유한한 0 이상 값만 허용한다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Scrape|Recovery")
	double CarrierAmount;

	/** 최종 정분류로 회수 가능한 보상 단위. 유한한 0 이상 값만 허용한다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Scrape|Recovery")
	double RewardUnits;
};

/** Deterministic reward arithmetic shared by contamination drops and liquid spills. */
struct MARINEDEMOLITION_API FMD_ScrapeRecoveryLedger
{
	static bool IsLotValid(const FMD_ScrapeRecoveryLot& Lot);
	static double GetTotalCarrierAmount(
		TConstArrayView<FMD_ScrapeRecoveryLot> Lots);
	static double GetTotalRewardUnits(
		TConstArrayView<FMD_ScrapeRecoveryLot> Lots);
	static bool AppendLot(
		TArray<FMD_ScrapeRecoveryLot>& InOutLots,
		const FMD_ScrapeRecoveryLot& Lot);
	static bool AppendScaledLots(
		TArray<FMD_ScrapeRecoveryLot>& InOutLots,
		TConstArrayView<FMD_ScrapeRecoveryLot> SourceLots,
		double Scale);
	static bool RebaseCarrierAmount(
		TArray<FMD_ScrapeRecoveryLot>& InOutLots,
		double NewTotalCarrierAmount);
	static double ExtractProportional(
		TArray<FMD_ScrapeRecoveryLot>& InOutLots,
		double RequestedCarrierAmount,
		TArray<FMD_ScrapeRecoveryLot>& OutExtractedLots);
	static bool AppendZeroRewardCarrier(
		TArray<FMD_ScrapeRecoveryLot>& InOutLots,
		double CarrierAmount);
};
