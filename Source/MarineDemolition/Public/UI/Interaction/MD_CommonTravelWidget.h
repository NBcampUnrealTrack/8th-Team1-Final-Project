// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UI/Foundation/MD_CommonGameMenuWidget.h"
#include "Types/Travel/MD_TravelTypes.h"
#include "MD_CommonTravelWidget.generated.h"

class UMD_CommonButtonBase;
class UCommonButtonGroupBase;
class UScrollBox;
class AMD_TravelConsole;

/** WBP_CommonTravel의 단일 화면에서 맵과 난이도 선택 및 출항 요청을 처리한다. */
UCLASS()
class MARINEDEMOLITION_API UMD_CommonTravelWidget : public UMD_CommonGameMenuWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeOnActivated() override;

protected:
	// 동적으로 생성한 맵 버튼을 담는 WBP_CommonTravel의 ScrollBox를 선택적으로 연결한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Travel", meta = (BindWidgetOptional))
	TObjectPtr<UScrollBox> SB_MapList;

	// WBP 노드 그래프에서 DeactivateWidget 호출에 사용할 닫기 버튼을 선택적으로 연결한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Travel", meta = (BindWidgetOptional))
	TObjectPtr<UMD_CommonButtonBase> Btn_Close;

	// WBP 노드 그래프에서 맵 목록의 이전 항목 이동에 사용할 버튼을 선택적으로 연결한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Travel", meta = (BindWidgetOptional))
	TObjectPtr<UMD_CommonButtonBase> Btn_PreviousMap;

	// WBP 노드 그래프에서 맵 목록의 다음 항목 이동에 사용할 버튼을 선택적으로 연결한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Travel", meta = (BindWidgetOptional))
	TObjectPtr<UMD_CommonButtonBase> Btn_NextMap;

	// 안전 난이도를 선택하는 WBP_CommonTravel 버튼을 선택적으로 연결한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Travel", meta = (BindWidgetOptional))
	TObjectPtr<UMD_CommonButtonBase> Btn_Easy;

	// 표준 난이도를 선택하는 WBP_CommonTravel 버튼을 선택적으로 연결한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Travel", meta = (BindWidgetOptional))
	TObjectPtr<UMD_CommonButtonBase> Btn_Normal;

	// 위험 난이도를 선택하는 WBP_CommonTravel 버튼을 선택적으로 연결한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Travel", meta = (BindWidgetOptional))
	TObjectPtr<UMD_CommonButtonBase> Btn_Hard;

	// 현재 선택한 맵과 난이도로 출항을 요청하는 버튼을 선택적으로 연결한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Travel", meta = (BindWidgetOptional))
	TObjectPtr<UMD_CommonButtonBase> Btn_Travel;

	// 맵 목록의 각 항목으로 생성할 CommonButtonBase Widget Blueprint 클래스를 등록한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Travel")
	TSubclassOf<UMD_CommonButtonBase> MapCardWidgetClass;

	// 동적으로 생성한 맵 버튼 중 항상 하나만 선택되도록 관리하는 그룹을 보관한다.
	UPROPERTY(Transient)
	TObjectPtr<UCommonButtonGroupBase> MapButtonGroup;

	// Easy, Normal, Hard 버튼 중 항상 하나만 선택되도록 관리하는 그룹을 보관한다.
	UPROPERTY(Transient)
	TObjectPtr<UCommonButtonGroupBase> DifficultyButtonGroup;

	// 현재 맵 목록에 생성된 카드를 선택 상태 갱신에 사용하도록 보관한다.
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMD_CommonButtonBase>> MapCards;

	// 생성된 각 카드가 나타내는 콘솔 현장 인덱스를 같은 순서로 보관한다.
	TArray<int32> MapCardSiteIndices;

	// 현재 화면을 연 트래블 콘솔을 현장 데이터와 출항 요청에 사용하도록 보관한다.
	UPROPERTY()
	TObjectPtr<AMD_TravelConsole> OwningConsole;

	// 현재 선택한 현장 인덱스를 출항 요청에 사용하도록 보관한다.
	int32 SelectedSiteIndex = INDEX_NONE;

	// 현재 선택한 난이도를 출항 요청에 사용하도록 보관한다.
	EMD_Difficulty SelectedDifficulty = EMD_Difficulty::Standard;

public:
	// 현재 화면을 연 트래블 콘솔을 연결하고 맵 목록을 갱신한다.
	UFUNCTION(BlueprintCallable, Category = "MD|Travel")
	void SetOwningConsole(AMD_TravelConsole* InConsole);

	// 동적 MapCard 또는 WBP 노드 그래프에서 전달한 현장을 선택한다.
	UFUNCTION(BlueprintCallable, Category = "MD|Travel")
	void SelectSite(int32 SiteIndex);

	// WBP의 이전 맵 버튼에서 호출해 현재 선택값 앞의 사용 가능한 맵을 선택한다.
	UFUNCTION(BlueprintCallable, Category = "MD|Travel")
	void SelectPreviousMap();

	// WBP의 다음 맵 버튼에서 호출해 현재 선택값 뒤의 사용 가능한 맵을 선택한다.
	UFUNCTION(BlueprintCallable, Category = "MD|Travel")
	void SelectNextMap();

	// WBP 노드 그래프에서 전달한 Easy, Normal, Hard 중 하나만 선택 상태로 적용한다.
	UFUNCTION(BlueprintCallable, Category = "MD|Travel")
	void SelectIntegratedDifficulty(EMD_Difficulty Difficulty);

	// 현재 선택한 현장과 난이도로 서버 출항을 요청한다.
	UFUNCTION(BlueprintCallable, Category = "MD|Travel")
	void RequestTravel();

protected:
	// 콘솔 데이터와 현재 선택 상태를 단일 화면 레이아웃에 함께 반영한다.
	void RefreshTravelLayout();

	// 콘솔의 맵 목록에 맞춰 SB_MapList의 MapCard Widget을 다시 생성한다.
	void RebuildMapList();

	// 생성된 MapCard 중 현재 현장에 대응하는 카드만 선택 상태로 표시한다.
	void RefreshMapCardSelection();

	// Easy, Normal, Hard 중 현재 난이도에 대응하는 버튼만 선택 상태로 표시한다.
	void RefreshDifficultySelection();

	// WBP의 난이도 버튼을 선택 필수 CommonButtonGroup으로 한 번만 구성한다.
	void InitializeDifficultyButtonGroup();

	// 현재 플레이어 권한과 현장 선택값에 맞춰 출항 버튼 상태를 갱신한다.
	void RefreshTravelAvailability();

	// 생성한 모든 MapCard 클릭을 SiteIndex 기반 선택 함수로 전달한다.
	void HandleMapCardClicked(int32 SiteIndex);

	// 현재 인덱스를 기준으로 지정한 방향의 다음 사용 가능한 맵을 순환 탐색한다.
	void SelectAdjacentMap(int32 Direction);

	// 현재 선택된 MapCard가 ScrollBox 중앙에 보이도록 스크롤한다.
	void ScrollSelectedMapIntoView();

	// 현재 UI를 소유한 플레이어가 서버 권한을 가졌는지 확인한다.
	bool IsHost() const;
};
