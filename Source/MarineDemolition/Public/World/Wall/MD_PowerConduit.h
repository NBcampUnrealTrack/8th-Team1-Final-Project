#pragma once

#include "CoreMinimal.h"
#include "Audio/MD_AudioSubsystem.h"
#include "Engine/EngineTypes.h"
#include "Types/Facility/MD_FacilityTypes.h"
#include "World/Wall/MD_WallUtility.h"
#include "MD_PowerConduit.generated.h"

class UMaterialInterface;
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
	// 동일한 슬롯과 머티리얼 설정을 적용할 스태틱 메시 컴포넌트 목록
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Utility|Power", meta = (UseComponentPicker, AllowAnyActor, AllowedClasses = "/Script/Engine.StaticMeshComponent"))
	TArray<FComponentReference> MeshComponents;

	// 모든 대상 메시에서 교체할 0부터 시작하는 머티리얼 슬롯
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Utility|Power", meta = (ClampMin = "0"))
	int32 MaterialIndex = 0;

	// 모든 대상 메시의 정상 상태에 사용할 BP 지정 머티리얼
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Utility|Power")
	TObjectPtr<UMaterialInterface> NormalMaterial = nullptr;

	// 모든 대상 메시의 정전 상태에 사용할 BP 지정 머티리얼
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Utility|Power")
	TObjectPtr<UMaterialInterface> BlackoutMaterial = nullptr;

protected:
	// BP 컴포넌트 참조에서 해석한 실제 머티리얼 적용 대상 보관
	UPROPERTY(Transient)
	TArray<FMD_PowerLightState> ControlledPowerLights;

	// 현재 조명 상태를 집계하는 월드의 관리자 연결
	TWeakObjectPtr<UMD_FacilityFaultManagerComponent> RegisteredPowerManager;

	// GameState 지연 도착 이후 조명 연결을 다시 적용할 단일 예약
	FTimerHandle PowerRegistrationTimer;

public:
	// 현재 BP 메시 설정을 서버 고장 집계와 각 클라이언트 머티리얼 교체에 연결
	UFUNCTION(BlueprintCallable, Category = "MD|Utility")
	void ApplyPoweredLights();

protected:
	// 준비된 GameState 관리자에 현재 메시 슬롯과 전선 고장 상태 적용
	void RegisterPowerLights();
};
