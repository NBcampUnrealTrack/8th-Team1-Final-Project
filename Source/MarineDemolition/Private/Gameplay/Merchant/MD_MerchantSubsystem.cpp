// Fill out your copyright notice in the Description page of Project Settings.


#include "Gameplay/Merchant/MD_MerchantSubsystem.h"

void UMD_MerchantSubsystem::AddCredits(int32 Amount)
{
	//크레딧은 최소 0
	if (Amount == 0)
	{
		return;
	}

	Credits = FMath::Max(0, Credits + Amount);
}
