// Fill out your copyright notice in the Description page of Project Settings.

#include "Types/SaveGame/MD_SaveDataTypes.h"

#include "Data/Merchant/MD_SiteDefinition.h"
#include "GameplayTags/MD_GameplayTags.h"
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
	for (const TPair<FName, int32>& PurchasedItemEntry : PurchasedItemCounts)
	{
		if (PurchasedItemEntry.Key.IsNone() == true || PurchasedItemEntry.Value <= 0)
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
		const int32 CellCount = ContaminationRecord.CellIndices.Num();
		const bool bTransferredAmountsValid =
			ContaminationRecord.TransferredAmounts.IsEmpty()
			|| ContaminationRecord.TransferredAmounts.Num() == CellCount;
		const bool bTransferredPatternDensitiesValid =
			ContaminationRecord.TransferredPatternDensities.IsEmpty()
			|| ContaminationRecord.TransferredPatternDensities.Num() == CellCount;
		if (ContaminationRecord.WallName.IsNone() == true
			|| CellCount != ContaminationRecord.InitialAmounts.Num()
			|| CellCount != ContaminationRecord.RemainingAmounts.Num()
			|| bTransferredAmountsValid == false
			|| bTransferredPatternDensitiesValid == false)
		{
			return false;
		}
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

bool FMD_GameData::MigrateToCurrentVersion()
{
	// 현재 실행 파일이 이해하지 못하는 미래 슬롯을 부분 적용하거나 덮어쓰지 않기 위한 검사
	if (SchemaVersion > CurrentSchemaVersion)
	{
		return false;
	}

	// 구형 반복 퀘스트 구조와 제거된 보상 수령 상태를 현재 의미로 오인하지 않고 안전하게 재선정하도록 비우는 마이그레이션
	if (SchemaVersion < 4)
	{
		FMD_MerchantData MerchantData;
		if (GetDataField(FMD_MerchantData::DataFieldKey, MerchantData) == true)
		{
			if (SchemaVersion <= 0)
			{
				MerchantData.Affinity = FMath::Clamp(MerchantData.Affinity, -4.0f, 5.0f);
			}
			MerchantData.RepeatQuests.Reset();
			AddDataField(FMD_MerchantData::DataFieldKey, MerchantData);
		}
	}

	// 인라인 메인 퀘스트를 저장하던 슬롯은 현장 에셋의 일차별 PrimaryAssetId에 다시 연결한다.
	if (SchemaVersion < 5)
	{
		FMD_MerchantData MerchantData;
		if (GetDataField(FMD_MerchantData::DataFieldKey, MerchantData) == true && MerchantData.MainQuests.IsEmpty() == false)
		{
			const UMD_SiteDefinition* Site = MerchantData.CurrentSite.IsNull() == true ? nullptr : MerchantData.CurrentSite.LoadSynchronous();
			bool bCanRelinkAllMainQuests = IsValid(Site) == true;
			for (FMD_MainQuestState& MainQuest : MerchantData.MainQuests)
			{
				const int32 DefinitionIndex = MainQuest.UnlockDay - 1;
				if (bCanRelinkAllMainQuests == false || Site->MainQuestLine.IsValidIndex(DefinitionIndex) == false
					|| Site->MainQuestLine[DefinitionIndex].IsValid() == false)
				{
					bCanRelinkAllMainQuests = false;
					break;
				}

				MainQuest.DefinitionId = Site->MainQuestLine[DefinitionIndex];
			}

			if (bCanRelinkAllMainQuests == false)
			{
				// 복원할 원본이 없으면 다음 월드 동기화에서 계약을 안전하게 재초기화하도록 비운다.
				MerchantData.MainQuests.Reset();
			}

			AddDataField(FMD_MerchantData::DataFieldKey, MerchantData);
		}
	}

	// 통합 Definition에서 메인 역할도 다단계를 지원하므로 기존 슬롯을 첫 페이즈 상태로 명시하는 마이그레이션
	if (SchemaVersion < 6)
	{
		FMD_MerchantData MerchantData;
		// 기존 상인 데이터가 있을 때만 메인 퀘스트 단계 인덱스를 보정하기 위한 조회 결과
		bool bHasMerchantData = GetDataField(FMD_MerchantData::DataFieldKey, MerchantData);
		if (bHasMerchantData == true)
		{
			for (FMD_MainQuestState& MainQuest : MerchantData.MainQuests)
			{
				MainQuest.CurrentPhaseIndex = 0;
			}
			AddDataField(FMD_MerchantData::DataFieldKey, MerchantData);
		}
	}

	// 버전 7은 따개비 배치기 기록에 새 게임별 런타임 시드를 추가한다.
	// 구형 레코드는 새 유효성 플래그가 false로 역직렬화되며, 복원 시 기존
	// PlacementSeed를 사용하므로 별도의 배열 변환이 필요 없다.

	SchemaVersion = CurrentSchemaVersion;
	return true;
}

bool FMD_GameData::IsValidForLoad() const
{
	// 슬롯 식별자와 프로젝트 맵 경로가 없으면 안전한 적용 대상을 결정할 수 없으므로 거부하는 검사
	if (SchemaVersion != CurrentSchemaVersion || IsEmpty() == true || MapPackagePath.StartsWith(TEXT("/Game/")) == false)
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

	// 월드 필드는 초기 슬롯 호환을 위해 선택 사항이지만 존재하면 정확한 타입과 구조를 요구하는 검사
	if (DataFields.Contains(FMD_WorldData::DataFieldKey) == true)
	{
		FMD_WorldData WorldData;
		if (GetDataField(FMD_WorldData::DataFieldKey, WorldData) == false || WorldData.IsStructurallyValid() == false)
		{
			return false;
		}
	}

	// 로어 필드는 초기 슬롯 호환을 위해 선택 사항이지만 존재하면 정확한 타입과 구조를 요구하는 검사
	if (DataFields.Contains(FMD_LoreData::DataFieldKey) == true)
	{
		FMD_LoreData LoreData;
		if (GetDataField(FMD_LoreData::DataFieldKey, LoreData) == false || LoreData.IsStructurallyValid() == false)
		{
			return false;
		}
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

void UMD_SaveGame::ApplyMaxSaveCountLimit(const int32& MaxAutoSaveCount, const int32& MaxUserSaveCount)
{
	// 가장 최근 항목이 앞에 배치되므로 목록 끝의 오래된 자동 저장을 제거하는 처리
	if (AutoSaveList.Num() > MaxAutoSaveCount)
	{
		AutoSaveList.SetNum(MaxAutoSaveCount);
	}

	// 가장 최근 항목이 앞에 배치되므로 목록 끝의 오래된 사용자 저장을 제거하는 처리
	if (UserSaveList.Num() > MaxUserSaveCount)
	{
		UserSaveList.SetNum(MaxUserSaveCount);
	}
}

bool UMD_SaveGame::MigrateAndValidate()
{
	// 현재 실행 파일보다 새로운 컨테이너를 실수로 현재 형식으로 덮어쓰지 않기 위한 검사
	if (SchemaVersion > FMD_GameData::CurrentSchemaVersion)
	{
		return false;
	}

	// 개별 슬롯을 승격하고 손상된 항목만 제거하여 나머지 슬롯을 복구하기 위한 공통 처리
	auto MigrateList = [](TArray<FMD_GameData>& DataList)
	{
		for (int32 DataIndex = DataList.Num() - 1; DataIndex >= 0; --DataIndex)
		{
			FMD_GameData& GameData = DataList[DataIndex];
			if (GameData.MigrateToCurrentVersion() == false || GameData.IsValidForLoad() == false)
			{
				DataList.RemoveAt(DataIndex);
			}
		}
	};

	MigrateList(AutoSaveList);
	MigrateList(UserSaveList);
	SchemaVersion = FMD_GameData::CurrentSchemaVersion;

	return true;
}
