#include "Gameplay/Lore/MD_LoreSubsystem.h"

#include "Engine/GameInstance.h"
#include "Gameplay/Merchant/MD_MerchantSubsystem.h"

bool UMD_LoreSubsystem::TryDiscoverLore(const UMD_LoreCatalog* LoreCatalog, FName LoreId)
{
	if (HasServerAuthority() == false || IsValid(LoreCatalog) == false || LoreId.IsNone() == true)
	{
		return false;
	}

	// 잘못 입력된 카탈로그로 발견 상태와 보상 원장이 오염되는 상황을 막기 위한 검사
	TArray<FText> ValidationErrors;
	if (LoreCatalog->ValidateCatalog(ValidationErrors) == false)
	{
		return false;
	}

	const FMD_LoreDefinition* LoreDefinition = LoreCatalog->FindLoreDefinition(LoreId);
	if (LoreDefinition == nullptr || LoreDefinition->IsValidDefinition() == false)
	{
		return false;
	}

	UGameInstance* GameInstance = GetGameInstance();
	UMD_MerchantSubsystem* MerchantSubsystem = IsValid(GameInstance) == true ? GameInstance->GetSubsystem<UMD_MerchantSubsystem>() : nullptr;
	if (IsValid(MerchantSubsystem) == false)
	{
		return false;
	}

	// 검증 실패 시 빈 현장 상태가 원장에 남지 않도록 값 복사본에서 수행하는 변경 준비
	FMD_LoreSiteProgress NewSiteProgress;
	const FMD_LoreSiteProgress* ExistingSiteProgress = FindSiteProgress(LoreCatalog->SiteId);
	if (ExistingSiteProgress != nullptr)
	{
		NewSiteProgress = *ExistingSiteProgress;
	}
	else
	{
		NewSiteProgress.SiteId = LoreCatalog->SiteId;
	}

	if (NewSiteProgress.DiscoveredLoreIds.Contains(LoreId) == true)
	{
		return false;
	}

	// 마지막 발견과 완성 보상을 하나의 서버 연산으로 확정하기 위한 완료 예정 상태
	bool bWillCompleteCatalog = WillCompleteCatalog(LoreCatalog, NewSiteProgress, LoreId);
	bool bShouldGrantCompletionReward = bWillCompleteCatalog == true && NewSiteProgress.bCompletionRewardGranted == false;
	int32 TotalRewardCredits = LoreDefinition->RewardCredits + (bShouldGrantCompletionReward == true ? LoreCatalog->CompletionRewardCredits : 0);

	// 크레딧 정수 범위를 넘는 데이터가 발견 상태만 먼저 확정하는 부분 처리를 막기 위한 검사
	if (TotalRewardCredits < 0 || TotalRewardCredits > MAX_int32 || MerchantSubsystem->GetCredits() + TotalRewardCredits > MAX_int32)
	{
		return false;
	}

	// 발견 사실 기록
	NewSiteProgress.DiscoveredLoreIds.Add(LoreId);
	if (bShouldGrantCompletionReward == true)
	{
		NewSiteProgress.bCompletionRewardGranted = true;
	}

	FMD_LoreSiteProgress* SiteProgress = FindOrAddSiteProgress(LoreCatalog->SiteId);
	if (SiteProgress == nullptr)
	{
		return false;
	}

	*SiteProgress = MoveTemp(NewSiteProgress);

	// 최초 발견 기록과 같은 서버 호출 안에서만 실행되는 중복 방지 보상 지급
	MerchantSubsystem->AddCredits(static_cast<int32>(TotalRewardCredits));
	OnLoreProgressChanged.Broadcast();

	return true;
}

EMD_LoreEntryState UMD_LoreSubsystem::GetLoreState(const UMD_LoreCatalog* LoreCatalog, FName LoreId) const
{
	if (IsValid(LoreCatalog) == false || LoreId.IsNone() == true)
	{
		return EMD_LoreEntryState::Undiscovered;
	}

	const FMD_LoreSiteProgress* SiteProgress = FindSiteProgress(LoreCatalog->SiteId);
	if (SiteProgress == nullptr)
	{
		return EMD_LoreEntryState::Undiscovered;
	}

	return LoreCatalog->ResolveLoreState(LoreId, SiteProgress->DiscoveredLoreIds);
}

bool UMD_LoreSubsystem::GetSiteProgress(FName SiteId, FMD_LoreSiteProgress& OutSiteProgress) const
{
	const FMD_LoreSiteProgress* SiteProgress = FindSiteProgress(SiteId);
	if (SiteProgress == nullptr)
	{
		OutSiteProgress = FMD_LoreSiteProgress();
		return false;
	}

	OutSiteProgress = *SiteProgress;
	return true;
}

int32 UMD_LoreSubsystem::GetDiscoveredLoreCount(FName SiteId) const
{
	const FMD_LoreSiteProgress* SiteProgress = FindSiteProgress(SiteId);
	return SiteProgress != nullptr ? SiteProgress->DiscoveredLoreIds.Num() : 0;
}

bool UMD_LoreSubsystem::IsCatalogComplete(const UMD_LoreCatalog* LoreCatalog) const
{
	if (IsValid(LoreCatalog) == false || LoreCatalog->LoreEntries.Num() <= 0)
	{
		return false;
	}

	const FMD_LoreSiteProgress* SiteProgress = FindSiteProgress(LoreCatalog->SiteId);
	if (SiteProgress == nullptr)
	{
		return false;
	}

	// 정적 카탈로그의 모든 식별자가 발견 원장에 존재하는지 확인하는 완성 판정
	for (const FMD_LoreDefinition& LoreDefinition : LoreCatalog->LoreEntries)
	{
		if (SiteProgress->DiscoveredLoreIds.Contains(LoreDefinition.LoreId) == false)
		{
			return false;
		}
	}

	return true;
}

FMD_LoreData UMD_LoreSubsystem::GetLoreSaveData() const
{
	FMD_LoreData SaveData;
	SaveData.SiteProgressList = SiteProgressList;
	return SaveData;
}

bool UMD_LoreSubsystem::CanApplyLoreSaveData(const FMD_LoreData& SaveData) const
{
	return HasServerAuthority() == true && SaveData.IsStructurallyValid() == true;
}

bool UMD_LoreSubsystem::ApplyLoreSaveData(const FMD_LoreData& SaveData)
{
	if (CanApplyLoreSaveData(SaveData) == false)
	{
		return false;
	}

	SiteProgressList = SaveData.SiteProgressList;
	OnLoreProgressChanged.Broadcast();
	return true;
}

const TArray<FMD_LoreSiteProgress>& UMD_LoreSubsystem::GetSiteProgressList() const
{
	return SiteProgressList;
}

bool UMD_LoreSubsystem::HasServerAuthority() const
{
	const UWorld* World = GetWorld();
	return IsValid(World) == true && World->GetNetMode() != ENetMode::NM_Client;
}

FMD_LoreSiteProgress* UMD_LoreSubsystem::FindOrAddSiteProgress(FName SiteId)
{
	if (SiteId.IsNone() == true)
	{
		return nullptr;
	}

	for (FMD_LoreSiteProgress& SiteProgress : SiteProgressList)
	{
		if (SiteProgress.SiteId == SiteId)
		{
			return &SiteProgress;
		}
	}

	FMD_LoreSiteProgress& NewSiteProgress = SiteProgressList.AddDefaulted_GetRef();
	NewSiteProgress.SiteId = SiteId;
	return &NewSiteProgress;
}

const FMD_LoreSiteProgress* UMD_LoreSubsystem::FindSiteProgress(FName SiteId) const
{
	if (SiteId.IsNone() == true)
	{
		return nullptr;
	}

	for (const FMD_LoreSiteProgress& SiteProgress : SiteProgressList)
	{
		if (SiteProgress.SiteId == SiteId)
		{
			return &SiteProgress;
		}
	}

	return nullptr;
}

bool UMD_LoreSubsystem::WillCompleteCatalog(const UMD_LoreCatalog* LoreCatalog, const FMD_LoreSiteProgress& SiteProgress, FName NewLoreId) const
{
	if (IsValid(LoreCatalog) == false || LoreCatalog->LoreEntries.Num() <= 0)
	{
		return false;
	}

	for (const FMD_LoreDefinition& LoreDefinition : LoreCatalog->LoreEntries)
	{
		if (LoreDefinition.LoreId != NewLoreId && SiteProgress.DiscoveredLoreIds.Contains(LoreDefinition.LoreId) == false)
		{
			return false;
		}
	}

	return true;
}
