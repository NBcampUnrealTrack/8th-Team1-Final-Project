// Fill out your copyright notice in the Description page of Project Settings.

#include "Types/SaveGame/MD_SaveDataTypes.h"
#include "Gameplay/Contamination/MD_SurfaceStainPersistence.h"

#include "GameplayTags/MD_GameplayTags.h"
#include "Gameplay/Scrape/MD_ScrapeSurfaceLayout.h"
#include "Types/Lore/MD_LoreTypes.h"
#include "World/Trash/MD_TrashBin.h"

const FGameplayTag FMD_GameStateData::DataFieldKey = MDGameplayTags::Save_Data_GameState;
const FGameplayTag FMD_MerchantData::DataFieldKey = MDGameplayTags::Save_Data_Merchant;
const FGameplayTag FMD_WorldData::DataFieldKey = MDGameplayTags::Save_Data_World;

bool FMD_TrashBinRecord::IsStructurallyValid() const
{
	const UClass* const SavedBinClass = TrashBinClass.Get();
	const FVector SavedLocation = Transform.GetLocation();
	const FVector SavedScale = Transform.GetScale3D();
	const FQuat SavedRotation = Transform.GetRotation();
	if (!StableBinId.IsValid()
		|| SourceDispenserName.IsNone()
		|| !IsValid(SavedBinClass)
		|| !SavedBinClass->IsChildOf(AMD_TrashBin::StaticClass())
		|| SavedLocation.ContainsNaN()
		|| SavedScale.ContainsNaN()
		|| SavedRotation.ContainsNaN()
		|| !SavedRotation.IsNormalized()
		|| LiquidRevision < 0)
	{
		return false;
	}

	FMD_TrashBinLiquidState SavedLiquidState;
	SavedLiquidState.LiquidUnits = LiquidUnits;
	SavedLiquidState.RewardUnits = RewardUnits;
	SavedLiquidState.Revision = LiquidRevision;
	if (!SavedLiquidState.IsValid(5.0))
	{
		return false;
	}

	const double SavedCarrierAmount =
		FMD_ScrapeRecoveryLedger::GetTotalCarrierAmount(LiquidRecoveryLots);
	const double SavedRewardUnits =
		FMD_ScrapeRecoveryLedger::GetTotalRewardUnits(LiquidRecoveryLots);
	return SavedCarrierAmount >= 0.0
		&& SavedRewardUnits >= 0.0
		&& FMath::IsNearlyEqual(
			SavedCarrierAmount,
			LiquidUnits,
			1.0e-9)
		&& FMath::IsNearlyEqual(
			SavedRewardUnits,
			RewardUnits,
			1.0e-12);
}

bool FMD_GameStateData::IsStructurallyValid() const
{
	// 계약 범위와 하루 길이를 벗어난 값이 시간·점검 로직을 오염시키지 않도록 하는 검사
	return ElapsedDay >= 1 && ElapsedDay <= 4 && ElapsedSeconds >= 0.0 && FMath::IsFinite(ElapsedSeconds) == true && ContractCrewCount >= 0 && ContractDifficulty >= EMD_Difficulty::Safe && ContractDifficulty <= EMD_Difficulty::Extreme;
}

bool FMD_MerchantData::IsStructurallyValid() const
{
	// 경제와 호감도 범위가 런타임 규칙에서 허용하는 범위인지 확인하는 검사
	if (Credits < 0 || DebtBalance < 0 || Affinity < -4.0f || Affinity > 5.0f || FMath::IsFinite(Affinity) == false || CurrentDay < 0 || CurrentDay > 4 || NextDepositMultiplier < 0.0f || FMath::IsFinite(NextDepositMultiplier) == false || QuestCompletions < 0)
	{
		return false;
	}

	// 활성 계약 없이 퀘스트 상태만 남은 손상 데이터의 부분 적용을 막기 위한 검사
	if (CurrentSite.IsNull() == true && (CurrentDay > 0 || MainQuests.Num() > 0 || RepeatQuests.Num() > 0))
	{
		return false;
	}

	// 식별자가 없거나 수량이 유효하지 않은 구매 원장이 해금과 중복 구매 판정을 오염시키지 않도록 하는 검사
	for (const TPair<FPrimaryAssetId, int32>& PurchasedItemEntry : PurchasedItemAssetCounts)
	{
		// 전체 AssetId와 양수 구매 수량을 갖춘 저장 항목만 허용
		if (PurchasedItemEntry.Key.IsValid() == false || PurchasedItemEntry.Value <= 0)
		{
			return false;
		}
	}

	// 메인 퀘스트 상태가 원본 Primary Data Asset을 다시 찾을 식별자와 유효한 진행도를 갖는지 확인하는 검사
	for (const FMD_MainQuestState& MainQuest : MainQuests)
	{
		bool bHasValidDefinitionId = MainQuest.HasValidDefinitionId();
		if (bHasValidDefinitionId == false || MainQuest.UnlockDay <= 0)
		{
			return false;
		}

		if (MainQuest.CurrentPhaseIndex < 0 || MainQuest.CurrentProgress < 0)
		{
			return false;
		}
	}

	// 반복 퀘스트 상태가 Primary Data Asset과 페이즈 진행을 다시 찾을 최소 식별 정보를 갖는지 확인하는 검사
	for (const FMD_RepeatQuestState& RepeatQuest : RepeatQuests)
	{
		bool bHasValidDefinitionId = RepeatQuest.HasValidDefinitionId();
		if (bHasValidDefinitionId == false || RepeatQuest.CurrentPhaseIndex < 0 || RepeatQuest.CurrentProgress < 0)
		{
			return false;
		}
	}

	return true;
}

bool FMD_WorldData::IsStructurallyValid() const
{
    if (!FMD_SurfaceStainPersistence::Validate(SurfaceStains)) return false;
    TSet<FGuid> PendingBatchIds;
    for (const FMD_ContaminationPendingDrop& Drop : PendingContaminationDrops)
    {
        if (!Drop.IsStructurallyValid() || PendingBatchIds.Contains(Drop.BatchId)) return false;
        PendingBatchIds.Add(Drop.BatchId);
    }

	// 같은 stable ID가 두 record를 가리키거나 lot 합이 요약과 다르면 일부만 복원하지 않는다.
	TSet<FGuid> SavedTrashBinIds;
	for (const FMD_TrashBinRecord& TrashBinRecord : TrashBins)
	{
		if (!TrashBinRecord.IsStructurallyValid()
			|| SavedTrashBinIds.Contains(TrashBinRecord.StableBinId))
		{
			return false;
		}
		SavedTrashBinIds.Add(TrashBinRecord.StableBinId);
	}

	// 오염 셀의 병렬 배열 길이가 다르면 잘못된 인덱스 복원을 수행하지 않기 위한 검사
	for (const FMD_WallContaminationRecord& ContaminationRecord : WallContaminations)
	{
        int32 MaximumCellCount = FMD_ScrapeSurfaceLayout::LegacyMaximumCombinedCellCount;
        // 현재 월드 기록에는 유효한 생성 정보와 액터 및 컴포넌트 식별자 필수
        if (ContaminationRecord.Generation.IsValid() == false || ContaminationRecord.SurfaceComponentName.IsNone() == true
            || ContaminationRecord.WallName.IsNone() == true
            || FMD_ScrapeSurfaceLayout::IsLayoutMetadataValid(
                ContaminationRecord.LayoutVersion, ContaminationRecord.LayoutId,
                ContaminationRecord.GridDimensions) == false
            || (ContaminationRecord.LayoutVersion == 1
                && FMD_ScrapeSurfaceLayout::IsCarrierScaleValid(ContaminationRecord.CarrierScale) == false))
        {
            return false;
        }
        if (ContaminationRecord.GridDimensions != FIntPoint::ZeroValue)
        {
            int32 CellsPerFace = 0;
            if (!FMD_ScrapeSurfaceLayout::TryCalculateCellCounts(
                ContaminationRecord.GridDimensions, CellsPerFace, MaximumCellCount)) return false;
            if (ContaminationRecord.LayoutVersion == 1) MaximumCellCount = CellsPerFace;
        }
        if (!ContaminationRecord.ValidateCellIndices(MaximumCellCount)) return false;
	}

	// 배치기 식별자가 없는 따개비 기록을 다른 액터에 적용하지 않기 위한 검사
	for (const FMD_BarnacleRemovalRecord& RemovalRecord : BarnacleRemovals)
	{
		if (RemovalRecord.SpawnerName.IsNone() == true)
		{
			return false;
		}
	}

	return true;
}

bool FMD_GameData::IsEmpty() const
{
	return UniqueName.IsEmpty() == true || MapPackagePath.IsEmpty() == true;
}

bool FMD_GameData::IsValidForLoad(bool bRequireWorldData) const
{
	// 슬롯 식별자와 프로젝트 맵 경로가 없으면 안전한 적용 대상을 결정할 수 없으므로 거부하는 검사
	if (bIsSlotSummary || SchemaVersion != CurrentSchemaVersion || IsEmpty() == true || MapPackagePath.StartsWith(TEXT("/Game/")) == false)
	{
		return false;
	}

	// 복원에 필수인 GameState와 상인 필드의 존재·타입·범위를 함께 확인하는 검사
	FMD_GameStateData GameStateData;
	FMD_MerchantData MerchantData;
	if (GetDataField(FMD_GameStateData::DataFieldKey, GameStateData) == false || GetDataField(FMD_MerchantData::DataFieldKey, MerchantData) == false || GameStateData.IsStructurallyValid() == false || MerchantData.IsStructurallyValid() == false)
	{
		return false;
	}

	// 진행 중인 계약에서 GameState와 상인 퀘스트 기준 일차가 어긋난 슬롯을 거부하는 검사
	if (MerchantData.CurrentDay != GameStateData.ElapsedDay)
	{
		return false;
	}

    // 대용량 월드 필드를 복사하지 않고 현재 형식의 타입과 구조 검사
    const FInstancedStruct* WorldField = DataFields.Find(FMD_WorldData::DataFieldKey);
    // 파일 저장 데이터에는 월드 필드를 필수로 요구
    if (WorldField == nullptr && bRequireWorldData == true)
    {
        // 파일에서 월드 필드가 누락된 경우 거부
        return false;
    }
    // 현재 회차 이동의 의도적인 생략을 제외한 월드 필드 전체 검증
    if (WorldField != nullptr && (WorldField->IsValid() == false || WorldField->GetScriptStruct() != FMD_WorldData::StaticStruct()
        || WorldField->GetPtr<FMD_WorldData>()->IsStructurallyValid() == false))
    {
        // 잘못된 월드 타입과 손상된 기록의 적용 거부
        return false;
    }

    // 최초 저장 형식부터 포함되는 로어 발견 원장 조회
    FMD_LoreData LoreData;
    // 로어 필드 누락을 빈 진행으로 대체하지 않고 거부
    if (GetDataField(FMD_LoreData::DataFieldKey, LoreData) == false || LoreData.IsStructurallyValid() == false)
    {
        // 누락되거나 손상된 로어 원장의 적용 거부
        return false;
    }

	// 빈 플레이어 식별자가 다른 사용자의 개인 데이터를 가리키지 않도록 하는 검사
	for (const TPair<FString, FMD_PlayerData>& PlayerField : PlayerFields)
	{
		if (PlayerField.Key.IsEmpty() == true)
		{
			return false;
		}
	}

	return true;
}
