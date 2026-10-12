#pragma once

#include "CoreMinimal.h"
#include "Audio/MD_AudioSubsystem.h"
#include "Engine/EngineTypes.h"
#include "Types/Facility/MD_FacilityTypes.h"
#include "World/Wall/MD_WallUtility.h"
#include "MD_PowerConduit.generated.h"

class UMaterialInterface;
class UStaticMeshComponent;
class UMD_FacilityFaultManagerComponent;

// 배선: 겹친 벽 철거 시 담당 조명 그룹 OFF + 점수. 절연테이프로 재점등
UCLASS()
class MARINEDEMOLITION_API AMD_PowerConduit : public AMD_WallUtility
{
	GENERATED_BODY()

public:
	AMD_PowerConduit();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnBroken(AActor* Cutter) override;
	virtual void OnRepaired(AActor* Repairer) override;

	/** 파열음·스파크 루프를 각 머신에서 태그로 재생·정지한다 */
	virtual void OnUtilityStateChangedLocal(EMD_UtilityState NewState) override;

	/**
	 * 절단 순간 1회. 사운드 에셋이 아니라 태그다 — 어떤 소재가 나갈지는 DA_MD_Audio_Hazard 가 정한다.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "MD|Utility|사운드")
	FGameplayTag CutSoundTag;

	/** 파열 지속 스파크. 루프 항목이라 수리 시 핸들로 끊는다 */
	UPROPERTY(EditDefaultsOnly, Category = "MD|Utility|사운드")
	FGameplayTag SparkLoopSoundTag;

	/** 스파크 정지 페이드(초) */
	UPROPERTY(EditDefaultsOnly, Category = "MD|Utility|사운드", meta = (ClampMin = "0.0"))
	float SparkStopFadeSeconds = 0.2f;

	// BP에서 조명 그룹 on/off·깜빡임 연출 처리. C++은 점수·상태만 담당
	// (조명 그룹 레퍼런스는 연출 특성상 BP/레벨에서 관리)
	UFUNCTION(BlueprintImplementableEvent, Category = "MD|Utility")
	void SetPowerGroupEnabled(bool bEnabled);

private:
	/** 재생 중인 스파크 루프. 각 머신이 자기 것을 들고 있다 (복제하지 않는다) */
	FMD_AudioHandle SparkLoopHandle;

public:
	// 맵에서 등록한 액터가 직접 소유한 모든 스태틱 메시의 공통 슬롯 제어
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "MD|Power|Targets")
	TArray<TObjectPtr<AActor>> TargetActors;

	// 액터 전체 대신 특정 스태틱 메시만 추가할 개별 참조 목록
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, AdvancedDisplay, Category = "MD|Power|Targets", meta = (UseComponentPicker, AllowAnyActor, AllowedClasses = "/Script/Engine.StaticMeshComponent"))
	TArray<FComponentReference> MeshComponents;

	// BP 기본값과 배치 액터에서 지정할 모든 대상 메시의 공통 머티리얼 슬롯
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Power|Materials", meta = (ClampMin = "0"))
	int32 MaterialIndex = 0;

	// BP 기본값을 상속하고 배치 액터에서 개별 변경 가능한 정상 머티리얼
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Power|Materials")
	TObjectPtr<UMaterialInterface> NormalMaterial = nullptr;

	// BP 기본값을 상속하고 배치 액터에서 개별 변경 가능한 정전 머티리얼
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Power|Materials")
	TObjectPtr<UMaterialInterface> BlackoutMaterial = nullptr;

protected:
	// BP 컴포넌트 참조에서 해석한 실제 머티리얼 적용 대상 보관
	UPROPERTY(Transient)
	TArray<FMD_PowerLightState> ControlledPowerLights;

	// 현재 조명 상태를 집계하는 월드의 관리자 연결
	TWeakObjectPtr<UMD_FacilityFaultManagerComponent> RegisteredPowerManager;

	// GameState 지연 도착 이후 조명 연결을 다시 적용할 단일 예약
	FTimerHandle PowerRegistrationTimer;
	// 레벨 이탈을 포함한 종료 이후 메시 재등록과 준비 대기 차단
	bool bPowerLightsShuttingDown = false;

public:
	// 현재 BP 메시 설정을 서버 고장 집계와 각 클라이언트 머티리얼 교체에 연결
	UFUNCTION(BlueprintCallable, Category = "MD|Utility")
	void ApplyPoweredLights();

protected:
	// 두 입력 목록의 유효성 및 준비 대기와 메시 슬롯 중복 제거를 공통 처리
	void AddPoweredMesh(UStaticMeshComponent* MeshComponent, bool& bPendingPowerMeshes);

	// 준비된 GameState 관리자에 현재 메시 슬롯과 전선 고장 상태 적용
	void RegisterPowerLights();
};
