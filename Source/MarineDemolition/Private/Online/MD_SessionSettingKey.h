// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

namespace MD_SessionKey
{
    // Session Settings의 프로젝트 식별용 Key
    const FName Key_ProjectID = TEXT("PROJECT_ID");

    // Session Settings의 프로젝트 식별용 Value
    const FString Value_ProjectID = TEXT("MarineDemolition");

    // Session Settings의 Session 이름을 구분하는 Key
    const FName Key_ServerName = FName("SERVER_NAME");

    // Session Settings의 Map 이름을 구분하는 Key
    const FName Key_MapName = FName("MAP_NAME");
}