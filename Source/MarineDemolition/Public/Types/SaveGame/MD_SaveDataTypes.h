// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "GameplayTags.h"
#include "StructUtils/InstancedStruct.h"
#include "Types/Persistence/MD_DaySnapshotTypes.h"
#include "MD_SaveDataTypes.generated.h"

/**
 * 플레이어 데이터
 */
USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_PlayerData
{
	GENERATED_BODY()

public:
	// 플레이어의 핫 바의 슬롯 목록을 복사하여 저장하는 변수
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save GameSession")
	TArray<TSoftObjectPtr<UDataAsset>> HotbarToolList;

	// Add More If Need
};

// GameState과 관련하여 저장되는 데이터
USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_GameStateData
{
	GENERATED_BODY()

public:
	const static FGameplayTag DataFieldKey;

public:
	// 점수
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save Data")
	int32 Score = 0;

	// 인게임 경과 일수
	// Ex) 1일차, 2일차
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save Data")
	int32 ElapsedDay = 0;

	// 인게임 경과 시간(초)
	// Ex) 300초
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save Data")
	double ElapsedSeconds = 0;

	// 비상 4일차 여부
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save Data")
	bool bEmergencyDay = false;

	// 점검 경고 상태. 다음 점검 기준 ×1.1
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save Data")
	bool bInspectionWarned = false;
};

// 상인 시스템과 관련하여 저장되는 데이터
USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_MerchantData
{
	GENERATED_BODY()

public:
	const static FGameplayTag DataFieldKey;

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save Data")
	int32 Credits = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save Data")
	int32 CurrentDay = 0;

	// 다음 정규일 착수금 배율 (1.0 기본)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save Data")
	float NextDepositMultiplier = 1.f;

	// 소모품 할인 활성 여부
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save Data")
	bool bConsumableDiscountActive = true;

	/** 누적 퀘스트 성공 횟수. 계약을 넘어 유지되며 상점 해금 조건에 쓴다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save Data")
	int32 QuestCompletions = 0;
};

// 저장되는 시점의 맵 상태(파괴된 벽, 남은 벽 등)가 저장되는 데이터
USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_WorldData
{
	GENERATED_BODY()

public:
	const static FGameplayTag DataFieldKey;

public:
	// 목록에 없는 레벨 배치 벽 = 어제 철거된 벽
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save Data")
	TArray<FName> SurvivingWallNames;

	// 살아남은 벽 중 지지대가 설치된 벽
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save Data")
	TArray<FName> SupportedWallNames;

	UPROPERTY()
	TArray<FMD_BraceRecord> Braces;

	UPROPERTY()
	TArray<FMD_SampleRecord> Samples;

	UPROPERTY()
	TArray<FMD_PieceRecord> Pieces;

	UPROPERTY()
	TArray<FMD_WallContaminationRecord> WallContaminations;

	// 마감 완료된 섹터 마감 마커 (레벨 배치 액터라 이름으로 식별)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save Data")
	TArray<FName> ClosedMarkerNames;
};

USTRUCT(BlueprintType, meta = (HasNativeMake = "/Script/MarineDemolition.MD_BlueprintFunctionLibrary.MakeMD_GameData"))
struct MARINEDEMOLITION_API FMD_GameData
{
	GENERATED_BODY()

public:
	// 서로 다른 게임 데이터와 구분하기 위한 고유 값(세이브 이름)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save Data")
	FString UniqueName = TEXT("");

	// 플레이 맵 패키지 경로
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save Data")
	FString MapPackagePath = TEXT("");

	// 현실 저장 시간
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save Data")
	FDateTime SaveTime = FDateTime::Now();

	// 데이터 컨테이너 (퀘스트 정보, 점수, 시간 등)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save Data")
	TMap<FGameplayTag, FInstancedStruct> DataFields;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save Data")
	TMap<FString, FMD_PlayerData> PlayerFields;

	inline bool IsEmpty() const { return UniqueName.IsEmpty() == true || MapPackagePath.IsEmpty() == true; }

	template<typename T>
	bool AddDataField(FGameplayTag KeyTag, const T& InStructData)
	{
		UE::StructUtils::CheckStructType<T>();

		if (KeyTag.IsValid() == false)
		{
			return false;
		}

		FInstancedStruct& DataField = DataFields.FindOrAdd(KeyTag);

		DataField.InitializeAs<T>(InStructData);

		return true;
	}

	template<typename T>
	bool GetDataField(FGameplayTag KeyTag, T& OutStructData) const
	{
		UE::StructUtils::CheckStructType<T>();

		if (KeyTag.IsValid() == false)
		{
			return false;
		}

		const FInstancedStruct* DataField = DataFields.Find(KeyTag);
		if (DataField == nullptr || DataField->IsValid() == false)
		{
			return false;
		}

		if (DataField->GetScriptStruct() != T::StaticStruct())
		{
			return false;
		}

		OutStructData = *DataField->GetPtr<T>();

		return true;
	}
};

UCLASS()
class MARINEDEMOLITION_API UMD_SaveGame : public USaveGame
{
	GENERATED_BODY()
	
public:
	// 자동 저장된 데이터 목록
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Marine Demolition Game Data")
	TArray<FMD_GameData> AutoSaveList;

	// 사용자가 임의로 저장한 데이터 목록
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Marine Demolition Game Data")
	TArray<FMD_GameData> UserSaveList;

public:
	void ApplyMaxSaveCountLimit(const int32& MaxAutoSaveCount, const int32& MaxUserSaveCount);
};
