// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "MD_Interactable.generated.h"

class AMD_Character;

UINTERFACE()
class UMD_Interactable : public UInterface
{
	GENERATED_BODY()
};

class MARINEDEMOLITION_API IMD_Interactable
{
	GENERATED_BODY()

public:
	virtual void Interact(AMD_Character* InstigatorCharacter) = 0;
};
