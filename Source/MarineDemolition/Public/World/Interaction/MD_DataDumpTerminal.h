#pragma once

#include "CoreMinimal.h"
#include "Gameplay/Interaction/MD_Interactable.h"
#include "MD_DataDumpTerminal.generated.h"

class UMD_InteractionPromptComponent;

class UMD_QuestItemDefinition;
class UStaticMeshComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMDOnDumpProgressChanged, float, Progress);

/**
 * 상호작용 키를 누르고 있으면 데이터를 덤프하고, 완료 시 핫바에 퀘스트 회수물(카트리지)을 지급하는 단말.
 * 기획 03 §3 X7-M02 4단계 「컴퓨터 Hold 덤프 → 카트리지」.
 *
 * - 서버 권한: Interact(누름)로 홀드 시작, InteractReleased(뗌)·거리 이탈·시선 이탈·사망으로 취소, HoldDuration 경과 시 완료.
 * - 완료 시 CartridgeItem을 홀더 핫바에 지급한다. 빈 슬롯이 없으면 지급하지 않고 HUD 메시지를 보낸다 (시작 시에도 미리 검사).
 * - QuestDefinitionId가 있으면 해당 메인 퀘스트가 PhaseIndex 페이즈로 활성일 때만 덤프할 수 있고,
 *   완료 시 MerchantSubsystem::NotifyInteractionProgress로 Interaction 진행도를 올린다.
 * - bLocked는 전원 게이트다. AMD_WallUtility::LinkedGateActor가 이름 기반(bLocked)으로 false로 바꾼다.
 * - 단말당 한 번만 덤프된다 (bDumped).
 */
UCLASS(Blueprintable)
class MARINEDEMOLITION_API AMD_DataDumpTerminal : public AActor, public IMD_Interactable
{
	GENERATED_BODY()

public:
	AMD_DataDumpTerminal();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// --- IMD_Interactable ---
	virtual void SetInteractionHighlighted_Implementation(bool bHighlighted) override;
	virtual FText GetInteractionDisplayName_Implementation() const override;
	virtual FText GetInteractionPrompt_Implementation(AActor* Interactor) const override;
	virtual bool CanInteract_Implementation(AActor* Interactor) const override;
	virtual bool IsHoldInteraction_Implementation() const override { return true; }
	virtual void Interact_Implementation(AActor* Interactor) override;
	virtual void InteractReleased_Implementation(AActor* Interactor) override;

    // 저장 시점의 전원 잠금 조회
    FORCEINLINE bool GetPersistentLocked() { return bLocked; }

    // 카트리지 재지급 없이 사용 완료와 전원 상태 복원
    void RestorePersistentState(bool bSavedDumped, bool bSavedLocked);

	/** 덤프가 끝났는지 (단말당 1회) */
	UFUNCTION(BlueprintPure, Category = "MD|DataDump")
	bool IsDumped() const { return bDumped; }

	/** 누군가 홀드 중인지 */
	UFUNCTION(BlueprintPure, Category = "MD|DataDump")
	bool IsHolding() const { return IsValid(HoldingPawn); }

	/** 현재 홀드 진행도 0~1 (복제) */
	UFUNCTION(BlueprintPure, Category = "MD|DataDump")
	float GetHoldProgress() const { return HoldProgress; }

	/** 홀드 진행도 변경 알림. HUD 게이지가 구독한다 (서버·클라 모두 방송) */
	UPROPERTY(BlueprintAssignable, Category = "MD|DataDump")
	FMDOnDumpProgressChanged OnDumpProgressChanged;

	// 액터별 행동 유형을 공통 프롬프트 설정에 연결하는 구성
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Interaction")
	TObjectPtr<UMD_InteractionPromptComponent> InteractionPrompt;

protected:
	/** 홀드 시작·취소 시 전 머신에서 호출. 화면 연출·사운드용 */
	UFUNCTION(BlueprintImplementableEvent, Category = "MD|DataDump")
	void OnHoldStateChanged(bool bHolding);

	/** 덤프 완료 시 전 머신에서 호출. 화면 소등·카트리지 배출 연출용 */
	UFUNCTION(BlueprintImplementableEvent, Category = "MD|DataDump")
	void OnDumpStateChanged(bool bNewDumped);

	/** 전원 잠금 변경 시 전 머신에서 호출. 화면 점등 연출용 */
	UFUNCTION(BlueprintImplementableEvent, Category = "MD|DataDump")
	void OnLockedStateChanged(bool bNewLocked);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|DataDump")
	TObjectPtr<UStaticMeshComponent> Mesh;

	/** 상호작용 HUD에 표시할 이름 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|DataDump")
	FText DisplayName;

	/** 덤프 완료까지 키를 눌러야 하는 시간 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|DataDump", meta = (ClampMin = "0.1", Units = "s"))
	float HoldDuration = 3.0f;

	/** 홀드 유지 허용 거리 (시점→단말 바운드 최근접점). 상호작용 트레이스 200cm보다 조금 넉넉하게 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|DataDump", meta = (ClampMin = "0.0", Units = "cm"))
	float MaxHoldDistance = 260.f;

	/** 홀드 유지에 필요한 시선 정렬(내적) 하한. 0.5 = 약 60° */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|DataDump", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float MinHoldAlignment = 0.5f;

	/** 완료 시 핫바에 지급할 회수물 정의 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|DataDump|Reward")
	TObjectPtr<UMD_QuestItemDefinition> CartridgeItem;

	/** 지급 수량 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|DataDump|Reward", meta = (ClampMin = "1"))
	int32 GrantQuantity = 1;

	/** 귀속 메인 퀘스트 DA 식별자. 비우면 퀘스트와 무관하게 덤프할 수 있다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|DataDump|Quest")
	FPrimaryAssetId QuestDefinitionId;

	/** 귀속 페이즈 인덱스 (DA Phases 배열 기준) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|DataDump|Quest")
	int32 PhaseIndex = 0;


	/**
	 * 전원 잠금. true면 덤프 불가. 이름을 bLocked로 유지해야 AMD_WallUtility::LinkedGateActor의
	 * 이름 기반 해제(FindPropertyByName("bLocked"))가 BP_Door와 같은 방식으로 이 단말에도 먹는다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing = OnRep_Locked, Category = "MD|DataDump|Quest")
	bool bLocked = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|DataDump|Message")
	FText LockedMessage;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|DataDump|Message")
	FText BusyMessage;


	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|DataDump|Message")
	FText InventoryFullMessage;

	/** {0} = 지급 품목 이름 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|DataDump|Message")
	FText DumpCompletedMessage;

	UPROPERTY(ReplicatedUsing = OnRep_Dumped)
	bool bDumped = false;

	UPROPERTY(ReplicatedUsing = OnRep_HoldingPawn)
	TObjectPtr<APawn> HoldingPawn;

	UPROPERTY(ReplicatedUsing = OnRep_HoldProgress)
	float HoldProgress = 0.f;

	/** 서버 전용 누적 홀드 시간 */
	float HoldElapsed = 0.f;

	UFUNCTION()
	void OnRep_Locked();

	UFUNCTION()
	void OnRep_Dumped();

	UFUNCTION()
	void OnRep_HoldingPawn();

	UFUNCTION()
	void OnRep_HoldProgress();

private:
	void BeginHold(APawn* Pawn);
	void CancelHold();
	void CompleteDump();

	/** 홀더가 여전히 살아 있고 단말을 가까이서 보고 있는지 (서버) */
	bool IsHoldStillValid() const;


	/** 지급 조건 사전 검사. 실패 사유 메시지를 돌려준다 (서버) */
	bool CanGrantCartridge(APawn* Pawn, FText& OutFailMessage) const;

	void SetHoldProgress(float NewProgress);

	static void SendHUDMessage(APawn* Pawn, const FText& Message);
};
