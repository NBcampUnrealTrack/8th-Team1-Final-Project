#include "Component/Lore/MD_LoreComponent.h"

#include "Engine/GameInstance.h"
#include "Gameplay/Lore/MD_LoreSubsystem.h"
#include "Net/UnrealNetwork.h"

UMD_LoreComponent::UMD_LoreComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UMD_LoreComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// 영구 누적 로어는 팀 공유 정보이므로 모든 접속자에게 전달하는 복제 등록
	DOREPLIFETIME(UMD_LoreComponent, SiteProgressList);
}

void UMD_LoreComponent::BeginPlay()
{
	Super::BeginPlay();

	AActor* Owner = GetOwner();
	if (IsValid(Owner) == false || Owner->HasAuthority() == false)
	{
		return;
	}

	UMD_LoreSubsystem* LoreSubsystem = GetLoreSubsystem();
	if (IsValid(LoreSubsystem) == false)
	{
		return;
	}

	SubsystemChangedHandle = LoreSubsystem->OnLoreProgressChanged.AddUObject(this, &UMD_LoreComponent::RefreshFromSubsystem);
	RefreshFromSubsystem();
}

void UMD_LoreComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UMD_LoreSubsystem* LoreSubsystem = GetLoreSubsystem();
	if (IsValid(LoreSubsystem) == true)
	{
		LoreSubsystem->OnLoreProgressChanged.Remove(SubsystemChangedHandle);
	}

	SubsystemChangedHandle.Reset();
	Super::EndPlay(EndPlayReason);
}

bool UMD_LoreComponent::GetSiteProgress(FName SiteId, FMD_LoreSiteProgress& OutSiteProgress) const
{
	for (const FMD_LoreSiteProgress& SiteProgress : SiteProgressList)
	{
		if (SiteProgress.SiteId == SiteId)
		{
			OutSiteProgress = SiteProgress;
			return true;
		}
	}

	OutSiteProgress = FMD_LoreSiteProgress();
	return false;
}

EMD_LoreEntryState UMD_LoreComponent::GetLoreState(const UMD_LoreCatalog* LoreCatalog, FName LoreId) const
{
	if (IsValid(LoreCatalog) == false || LoreId.IsNone() == true)
	{
		return EMD_LoreEntryState::Undiscovered;
	}

	FMD_LoreSiteProgress SiteProgress;
	if (GetSiteProgress(LoreCatalog->SiteId, SiteProgress) == false)
	{
		return EMD_LoreEntryState::Undiscovered;
	}

	return LoreCatalog->ResolveLoreState(LoreId, SiteProgress.DiscoveredLoreIds);
}

bool UMD_LoreComponent::HasDiscoveredLore(FName SiteId, FName LoreId) const
{
	if (SiteId.IsNone() == true || LoreId.IsNone() == true)
	{
		return false;
	}

	FMD_LoreSiteProgress SiteProgress;
	if (GetSiteProgress(SiteId, SiteProgress) == false)
	{
		return false;
	}

	bool bIsDiscovered = SiteProgress.DiscoveredLoreIds.Contains(LoreId);

	return bIsDiscovered;
}

void UMD_LoreComponent::RefreshFromSubsystem()
{
	UMD_LoreSubsystem* LoreSubsystem = GetLoreSubsystem();
	AActor* Owner = GetOwner();
	if (IsValid(LoreSubsystem) == false || IsValid(Owner) == false || Owner->HasAuthority() == false)
	{
		return;
	}

	SiteProgressList = LoreSubsystem->GetSiteProgressList();
	Owner->ForceNetUpdate();

	// Listen Server 환경을 고려하여 직접 알림
	OnLoreProgressUpdated.Broadcast();
}

UMD_LoreSubsystem* UMD_LoreComponent::GetLoreSubsystem() const
{
	UWorld* World = GetWorld();
	UGameInstance* GameInstance = IsValid(World) == true ? World->GetGameInstance() : nullptr;
	UMD_LoreSubsystem* LoreManager = IsValid(GameInstance) == true ? GameInstance->GetSubsystem<UMD_LoreSubsystem>() : nullptr;
	return LoreManager;
}

void UMD_LoreComponent::OnRep_LoreProgress()
{
	OnLoreProgressUpdated.Broadcast();
}
