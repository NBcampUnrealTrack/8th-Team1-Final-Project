#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "MD_InteractionTypes.generated.h"

UENUM(BlueprintType)
// 공용 행동 문구를 선택하는 상호작용 유형
enum class EMD_InteractionPromptType : uint8
{
	// 행동 안내를 표시하지 않는 유형
	None UMETA(DisplayName = "표시 안 함"),
	// 별도 행동 구분이 없는 기본 상호작용
	Interact UMETA(DisplayName = "상호작용"),
	// 소지품에 넣는 아이템 습득
	Pickup UMETA(DisplayName = "줍기"),
	// 물리 오브젝트 운반
	Carry UMETA(DisplayName = "들기"),
	// 인물과 대화 시작
	Talk UMETA(DisplayName = "대화"),
	// 카트 운반 시작
	PushCart UMETA(DisplayName = "카트 밀기"),
	// 보급기에서 쓰레기통 생성
	SpawnTrashBin UMETA(DisplayName = "쓰레기통 소환"),
	// 처리장에서 쓰레기 분류
	SortTrash UMETA(DisplayName = "쓰레기 분류"),
	// 문이나 보관함 개방
	Open UMETA(DisplayName = "열기"),
	// 퀘스트 물품 회수
	Collect UMETA(DisplayName = "회수"),
	// 기록물 열람
	Read UMETA(DisplayName = "읽기"),
	// 플레이어 외형 설정
	Customize UMETA(DisplayName = "외형 변경"),
	// 하루 작업 마감
	EndDay UMETA(DisplayName = "하루 마감"),
	// 목적지 이동 화면 진입
	Travel UMETA(DisplayName = "이동"),
	// 섹터 작업 완료 제출
	CloseSector UMETA(DisplayName = "구역 마감"),
	// 시설 제어 장치 조작
	Operate UMETA(DisplayName = "조작"),
	// 고장 시설 복구 시작
	Repair UMETA(DisplayName = "수리"),
	// 지문 패드 인증 시작
	Authenticate UMETA(DisplayName = "지문 인증"),
	// 단말 데이터 덤프 시작
	StartDump UMETA(DisplayName = "데이터 덤프"),
	// 수리 지침 사용과 다인 작업 시작
	UseInstruction UMETA(DisplayName = "수리 지침 사용"),
	// 수리 정답 변경과 출입문 개방
	RestartInstruction UMETA(DisplayName = "수리 지침 재시작"),
	// 차단기 노브를 다음 행으로 이동
	AdvanceBreakerRow UMETA(DisplayName = "차단기 다음 칸"),
	// 엘리베이터 하강 레버 조작
	ElevatorDown UMETA(DisplayName = "엘리베이터 내려가기"),
	// 엘리베이터 상승 레버 조작
	ElevatorUp UMETA(DisplayName = "엘리베이터 올라가기")
};

/** 월드 상호작용 대상의 화면 표시 정보를 보관하는 데이터 테이블 행. */
USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_InteractionPresentationRow : public FTableRowBase
{
	GENERATED_BODY()

	/** 조준했을 때 HUD에 표시할 이름. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Interaction")
	FText DisplayName;

	/** 입력 키를 제외한 행동 문구. 조회할 때 실제 적용된 상호작용 키를 붙인다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Interaction")
	FText ActionPrompt;
};
