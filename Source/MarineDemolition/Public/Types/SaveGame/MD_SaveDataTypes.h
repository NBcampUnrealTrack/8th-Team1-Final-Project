// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTags.h"
#include "StructUtils/InstancedStruct.h"
#include "Types/Merchant/MD_MerchantTypes.h"
#include "Types/Persistence/MD_DaySnapshotTypes.h"
#include "MD_SaveDataTypes.generated.h"

class UMD_SiteDefinition;

/**
 * 재접속과 Pawn 교체 이후에도 동일한 장비 구성을 적용하기 위한 플레이어 데이터
 */
USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_PlayerData
{
	GENERATED_BODY()

public:
	// 서버에서 소유권을 복원할 수 있도록 애셋 경로로 기록하는 핫바 슬롯 목록
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save GameSession")
	TArray<TSoftObjectPtr<UDataAsset>> HotbarToolList;
};

/**
 * 복제 GameState의 누계 점수와 일차 진행을 복원하기 위한 데이터 필드
 */
USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_GameStateData
{
	GENERATED_BODY()

public:
	// 인스턴스 구조체 컨테이너에서 GameState 필드를 식별하기 위한 키
	static const FGameplayTag DataFieldKey;

	// 세션 전체에 누적된 평가 점수
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save Data")
	int32 Score = 0;

	// 3일 계약과 비상 4일차의 현재 진행 위치
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save Data")
	int32 ElapsedDay = 0;

	// 같은 일차의 시각을 이어가기 위한 인게임 경과 초
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save Data")
	double ElapsedSeconds = 0.0;

	// 다음 점검 기준에 영향을 주는 비상 4일차 활성 상태
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save Data")
	bool bEmergencyDay = false;

	// 다음 점검 기준 배율에 영향을 주는 경고 상태
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save Data")
	bool bInspectionWarned = false;

	// 계약 시작 시점의 인원 배율을 플레이어 교체와 무관하게 유지하기 위한 인원수
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save Data")
	int32 ContractCrewCount = 0;

	// 출발 시 선택한 점검 난이도를 세이브 로드 이후에도 유지하기 위한 계약 값
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save Data")
	EMD_Difficulty ContractDifficulty = EMD_Difficulty::Standard;

public:
	// 손상된 숫자 범위가 서버 진행 상태에 적용되지 않도록 검사하는 함수
	bool IsStructurallyValid() const;
};

/**
 * 서버 권위 상인 원장의 경제·호감도·퀘스트 상태를 복원하기 위한 데이터 필드
 */
USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_MerchantData
{
	GENERATED_BODY()

public:
	// 인스턴스 구조체 컨테이너에서 상인 필드를 식별하기 위한 키
	static const FGameplayTag DataFieldKey;

	// 팀이 공유하는 현재 보유 크레딧
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save Data")
	int32 Credits = 0;

	// 계약과 게임 재시작을 넘어 유지되는 팀 공유 미상환 대출 잔액
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save Data")
	int32 DebtBalance = 0;

	// 상점 해금과 대화 분기에 사용하는 누적 상인 호감도
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save Data")
	float Affinity = 0.0f;

	// 퀘스트 해금과 반복 카드 상태의 기준이 되는 상인 시스템 일차
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save Data")
	int32 CurrentDay = 0;

	// 경고 다음 날의 착수금 규칙을 유지하기 위한 배율
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save Data")
	float NextDepositMultiplier = 1.0f;

	// 경고 이후 소모품 할인 규칙을 유지하기 위한 상태
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save Data")
	bool bConsumableDiscountActive = true;

	// 계약을 넘어 상점 해금 조건으로 사용하는 누적 퀘스트 성공 횟수
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save Data")
	int32 QuestCompletions = 0;

	// 구매 소유권과 소모품 누적 수량을 재개 세션에 복원하기 위한 품목별 구매 원장
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save Data")
	TMap<FPrimaryAssetId, int32> PurchasedItemAssetCounts;

	// 재개 시 같은 퀘스트 정의를 연결하기 위한 현재 현장 애셋
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save Data")
	TSoftObjectPtr<UMD_SiteDefinition> CurrentSite;

	// 잠김·진행·완료·실패와 목표 진행도를 그대로 유지하기 위한 메인 퀘스트 목록
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save Data")
	TArray<FMD_MainQuestState> MainQuests;

	// 퀘스트 진행도를 유지하기 위한 반복 퀘스트 목록
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save Data")
	TArray<FMD_RepeatQuestState> RepeatQuests;

	// 계약 사이에도 완료된 현장 기록을 유지하기 위한 클리어 현장 목록
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save Data")
	TArray<TSoftObjectPtr<UMD_SiteDefinition>> ClearedSites;

public:
	// 손상된 경제 범위와 퀘스트 상태가 서버 원장에 적용되지 않도록 검사하는 함수
	bool IsStructurallyValid() const;
};

/**
 * 저장 시점의 파괴·오염·동적 액터 상태를 정확히 복원하기 위한 월드 데이터 필드
 */
USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_WorldData
{
	GENERATED_BODY()

    /** Missing in older saves: preserve authored/per-surface seeds, including zero. */
    UPROPERTY() bool bHasContaminationSessionSeed = false;
    UPROPERTY() int32 ContaminationSessionSeed = 0;

public:
	// 인스턴스 구조체 컨테이너에서 월드 필드를 식별하기 위한 키
	static const FGameplayTag DataFieldKey;

	// 목록에 없는 레벨 배치 벽을 철거된 벽으로 판정하기 위한 생존 벽 이름
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save Data")
	TArray<FName> SurvivingWallNames;

	// 생존 벽 중 지지대 연결 상태를 복원하기 위한 벽 이름
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save Data")
	TArray<FName> SupportedWallNames;

	// 동적 지지대의 클래스·위치·연결 벽을 복원하기 위한 목록
	UPROPERTY()
	TArray<FMD_BraceRecord> Braces;

	// 미회수 샘플의 위치와 손상 상태를 복원하기 위한 목록
	UPROPERTY()
	TArray<FMD_SampleRecord> Samples;

	// 미수거 격벽 조각의 클래스·위치·오염 상태를 복원하기 위한 목록
	UPROPERTY()
	TArray<FMD_PieceRecord> Pieces;

	// 보급기가 만든 동적 통의 class·transform·stable ID와 전체 액체 원장을 복원하기 위한 목록
	UPROPERTY()
	TArray<FMD_TrashBinRecord> TrashBins;

	// 생성 기준과 달라진 표면 오염 셀을 복원하기 위한 목록
	UPROPERTY()
	TArray<FMD_WallContaminationRecord> WallContaminations;

    UPROPERTY()
    TArray<FMD_ContaminationPendingDrop> PendingContaminationDrops;

    UPROPERTY()
    TArray<FMD_SurfaceStainRecord> SurfaceStains;

	// 새 게임별 배치 시드와 제거 순번을 함께 복원하기 위한 배치기 상태 목록
	UPROPERTY()
	TArray<FMD_BarnacleRemovalRecord> BarnacleRemovals;

	// 점수를 다시 지급하지 않고 섹터 마감 상태를 유지하기 위한 마커 이름
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save Data")
	TArray<FName> ClosedMarkerNames;

public:
	// 병렬 오염 배열과 필수 이름의 정합성을 적용 전에 검사하는 함수
	bool IsStructurallyValid() const;
};

/**
 * 하나의 자동 또는 사용자 슬롯에 기록되는 확장 가능한 게임 세션 데이터
 */
USTRUCT(BlueprintType, meta = (HasNativeMake = "/Script/MarineDemolition.MD_BlueprintFunctionLibrary.MakeMD_GameData"))
struct MARINEDEMOLITION_API FMD_GameData
{
	GENERATED_BODY()

public:
	// 현재 저장 형식을 최초 배포 형식으로 식별하는 스키마 버전
	static constexpr int32 CurrentSchemaVersion = 1;

	// 저장된 필드 배치를 판별하기 위한 슬롯 데이터 버전
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save Data")
	int32 SchemaVersion = 0;

	// 자동 저장과 사용자 저장 목록에서 슬롯을 구분하기 위한 고유 이름
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save Data")
	FString UniqueName;

	// 다른 레벨의 월드 상태가 적용되지 않도록 하는 맵 패키지 경로
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save Data")
	FString MapPackagePath;

	// 슬롯 정렬과 사용자 표시를 위한 현실 저장 시각
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save Data")
	FDateTime SaveTime;

    // 슬롯 목록의 요약을 개별 저장 파일에서 읽은 전체 데이터와 구분하는 상태
    UPROPERTY()
    bool bIsSlotSummary = false;

    UPROPERTY()
    FString SlotPayloadId;

	// 기능별 구조체를 태그로 식별하여 확장하기 위한 데이터 필드 컨테이너
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save Data")
	TMap<FGameplayTag, FInstancedStruct> DataFields;

	// 안정적인 온라인 식별자로 개인 장비를 다시 연결하기 위한 플레이어 필드
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save Data")
	TMap<FString, FMD_PlayerData> PlayerFields;

public:
	// 새 게임 입력과 저장 슬롯을 구분하기 위한 최소 식별 정보 검사 함수
	bool IsEmpty() const;

	// 현재 버전과 필수 필드를 검사하고 현재 회차 이동에서만 월드 필드 생략 허용
	bool IsValidForLoad(bool bRequireWorldData = true) const;

	// 기능별 구조체를 태그 키에 복사하여 저장하기 위한 함수
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

	// 태그 키와 실제 구조체 타입이 모두 일치할 때만 데이터를 반환하는 함수
	template<typename T>
	bool GetDataField(FGameplayTag KeyTag, T& OutStructData) const
	{
		UE::StructUtils::CheckStructType<T>();

		if (KeyTag.IsValid() == false)
		{
			return false;
		}

		const FInstancedStruct* DataField = DataFields.Find(KeyTag);
		if (DataField == nullptr || DataField->IsValid() == false || DataField->GetScriptStruct() != T::StaticStruct())
		{
			return false;
		}

		OutStructData = *DataField->GetPtr<T>();

		return true;
	}
};
