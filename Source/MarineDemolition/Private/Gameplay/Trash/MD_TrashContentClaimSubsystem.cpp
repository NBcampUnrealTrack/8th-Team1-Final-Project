#include "Gameplay/Trash/MD_TrashContentClaimSubsystem.h"

#include "World/Trash/MD_TrashBin.h"

bool UMD_TrashContentClaimSubsystem::RegisterOverlap(AActor* Content, AMD_TrashBin* Bin)
{
	if (!IsValid(Content) || !IsValid(Bin) || !Bin->HasAuthority())
	{
		return false;
	}

	const TWeakObjectPtr<AActor> ContentPtr(Content);
	FContentClaim& Claim = Claims.FindOrAdd(ContentPtr);
	Claim.OverlappingBins.AddUnique(TWeakObjectPtr<AMD_TrashBin>(Bin));
	Content->OnDestroyed.AddUniqueDynamic(this, &UMD_TrashContentClaimSubsystem::HandleContentDestroyed);

	if (AMD_TrashBin* ExistingOwner = Claim.Owner.Get(); IsValid(ExistingOwner))
	{
		return ExistingOwner == Bin;
	}

	Claim.Owner = Bin;
	return true;
}

void UMD_TrashContentClaimSubsystem::UnregisterOverlap(AActor* Content, AMD_TrashBin* Bin)
{
	if (!Content || !Bin)
	{
		return;
	}

	FContentClaim* Claim = Claims.Find(TWeakObjectPtr<AActor>(Content));
	if (!Claim)
	{
		return;
	}

	Claim->OverlappingBins.Remove(TWeakObjectPtr<AMD_TrashBin>(Bin));
	if (Claim->Owner.Get() == Bin)
	{
		Claim->Owner.Reset();
	}

	if (!Claim->Owner.IsValid())
	{
		PromoteNextOwner(Content);
	}

	RemoveClaimIfUnused(Content);
}

bool UMD_TrashContentClaimSubsystem::ConsumeForSorting(AActor* Content, AMD_TrashBin* Bin)
{
	if (!IsValid(Content) || !IsValid(Bin))
	{
		return false;
	}

	const TWeakObjectPtr<AActor> ContentPtr(Content);
	const FContentClaim* Claim = Claims.Find(ContentPtr);
	if (!Claim || Claim->Owner.Get() != Bin)
	{
		return false;
	}

	Content->OnDestroyed.RemoveDynamic(this, &UMD_TrashContentClaimSubsystem::HandleContentDestroyed);
	Claims.Remove(ContentPtr);
	return true;
}

bool UMD_TrashContentClaimSubsystem::IsOwnedBy(AActor* Content, const AMD_TrashBin* Bin) const
{
	if (!Content || !Bin)
	{
		return false;
	}

	const FContentClaim* Claim = Claims.Find(TWeakObjectPtr<AActor>(Content));
	return Claim && Claim->Owner.Get() == Bin;
}

void UMD_TrashContentClaimSubsystem::Deinitialize()
{
	for (const TPair<TWeakObjectPtr<AActor>, FContentClaim>& Pair : Claims)
	{
		if (AActor* Content = Pair.Key.Get())
		{
			Content->OnDestroyed.RemoveDynamic(this, &UMD_TrashContentClaimSubsystem::HandleContentDestroyed);
		}
	}
	Claims.Reset();

	Super::Deinitialize();
}

void UMD_TrashContentClaimSubsystem::HandleContentDestroyed(AActor* DestroyedActor)
{
	if (DestroyedActor)
	{
		Claims.Remove(TWeakObjectPtr<AActor>(DestroyedActor));
	}
}

void UMD_TrashContentClaimSubsystem::PromoteNextOwner(AActor* Content)
{
	const TWeakObjectPtr<AActor> ContentPtr(Content);
	while (IsValid(Content))
	{
		FContentClaim* Claim = Claims.Find(ContentPtr);
		if (!Claim || Claim->Owner.IsValid())
		{
			return;
		}

		Claim->OverlappingBins.RemoveAll([Content](const TWeakObjectPtr<AMD_TrashBin>& BinPtr)
			{
				const AMD_TrashBin* Bin = BinPtr.Get();
				return !IsValid(Bin) || !Bin->CanAcceptContentClaim(Content);
			});
		Claim->OverlappingBins.Sort([](const TWeakObjectPtr<AMD_TrashBin>& Left, const TWeakObjectPtr<AMD_TrashBin>& Right)
			{
				return GetPathNameSafe(Left.Get()) < GetPathNameSafe(Right.Get());
			});

		if (Claim->OverlappingBins.IsEmpty())
		{
			return;
		}

		AMD_TrashBin* NewOwner = Claim->OverlappingBins[0].Get();
		Claim->Owner = NewOwner;
		if (IsValid(NewOwner) && NewOwner->TryApplyGrantedContentClaim(Content))
		{
			return;
		}

		Claim = Claims.Find(ContentPtr);
		if (!Claim)
		{
			return;
		}
		if (Claim->Owner.Get() == NewOwner)
		{
			Claim->Owner.Reset();
		}
		Claim->OverlappingBins.Remove(TWeakObjectPtr<AMD_TrashBin>(NewOwner));
	}
}

void UMD_TrashContentClaimSubsystem::RemoveClaimIfUnused(AActor* Content)
{
	const TWeakObjectPtr<AActor> ContentPtr(Content);
	FContentClaim* Claim = Claims.Find(ContentPtr);
	if (!Claim || Claim->Owner.IsValid() || !Claim->OverlappingBins.IsEmpty())
	{
		return;
	}

	if (IsValid(Content))
	{
		Content->OnDestroyed.RemoveDynamic(this, &UMD_TrashContentClaimSubsystem::HandleContentDestroyed);
	}
	Claims.Remove(ContentPtr);
}
