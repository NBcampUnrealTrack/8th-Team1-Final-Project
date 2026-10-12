#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Gameplay/Interaction/MD_Interactable.h"
#include "GameFramework/Actor.h"
#include "MD_CustomizationConsole.generated.h"

class AMD_Character;
class APlayerController;
class UCameraComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class USceneCaptureComponent2D;
class USceneComponent;
class UStaticMeshComponent;
class UTextureRenderTarget2D;

/**
 * 로비의 거울 콘솔. 상호작용하면 뷰를 콘솔 카메라(캐릭터 뒷모습)로 넘기고,
 * 캐릭터가 평소 걷기 그대로 StandPoint까지 걸어가 거울을 바라본 뒤 로컬에서 캐릭터 선택 화면을 연다.
 * 거울 면은 SceneCapture로 앞모습을 비춘다. 서버 상호작용이 필요 없는 로컬 전용 대상이다.
 */
UCLASS(Blueprintable)
class MARINEDEMOLITION_API AMD_CustomizationConsole : public AActor, public IMD_Interactable
{
	GENERATED_BODY()

public:
	AMD_CustomizationConsole();

	virtual bool RequiresServerInteraction_Implementation() const override { return false; }
	virtual void Interact_Implementation(AActor* Interactor) override;
	virtual void SetInteractionHighlighted_Implementation(bool bHighlighted) override;
	virtual FText GetInteractionDisplayName_Implementation() const override;

	// 카메라를 콘솔로 넘기고 로컬 캐릭터의 3인칭 몸을 보이게 한다
	void BeginCustomization(AMD_Character* Character);

	// 카메라·메시 표시·입력을 되돌린다. 선택 화면이 닫힐 때(또는 접근 실패 시) 호출된다
	void EndCustomization(AMD_Character* Character);

	// 누르고 있는 회전 입력의 방향을 적용하고 0 입력으로 정지
	void TurnFacing(float TurnInput);

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	// 콘솔 본체·거울 프레임
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Console")
	TObjectPtr<UStaticMeshComponent> Mesh;

	// 거울 면. 렌더타깃을 표시할 평면 메시
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Console")
	TObjectPtr<UStaticMeshComponent> MirrorSurface;

	// 선택 중 뷰 타깃. 캐릭터 뒤에서 거울을 바라보게 배치한다
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Console")
	TObjectPtr<UCameraComponent> ViewCamera;

	// 거울 면에서 캐릭터 쪽을 보는 캡처. 선택 중에만 매 프레임 캡처한다
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Console")
	TObjectPtr<USceneCaptureComponent2D> MirrorCapture;

	// 선택 중 캐릭터가 서는 지점. 위치와 Yaw(거울을 바라보는 방향)를 BP·레벨에서 조정한다
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Console")
	TObjectPtr<USceneComponent> StandPoint;

	// UI 관리자에 등록된 외형 선택 화면과 레이어를 지정하는 태그
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Console", meta = (Categories = "UI"))
	FGameplayTag ConsoleWidgetTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Console")
	float CameraBlendTime = 0.5f;

	// true면 열 때 StandPoint 기준으로 카메라를 계산해 놓고, false면 ViewCamera 컴포넌트에 잡아 둔 트랜스폼을 그대로 쓴다
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Console|Camera")
	bool bPlaceCameraBehindPlayer = false;

	// 열 때 카메라를 플레이어 뒤로 옮긴다. 플레이어에서 거울 반대쪽으로 이만큼 떨어진다
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Console|Camera", meta = (EditCondition = "bPlaceCameraBehindPlayer"))
	float CameraDistanceBehindPlayer = 300.f;

	// 플레이어 발 기준 카메라 높이
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Console|Camera", meta = (EditCondition = "bPlaceCameraBehindPlayer"))
	float CameraHeight = 190.f;

	// 카메라 주시점 높이. 콘솔 원점(바닥) 기준이라 거울 중심·캐릭터 머리 높이 근처로 맞춘다
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Console|Camera", meta = (EditCondition = "bPlaceCameraBehindPlayer"))
	float CameraLookAtHeightOffset = 120.f;

	// 주시점을 거울 중심(0)과 StandPoint(1) 사이 어디에 둘지. 0.5면 캐릭터와 거울이 화면 좌우로 나뉜다
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Console|Camera", meta = (ClampMin = "0.0", ClampMax = "1.0", EditCondition = "bPlaceCameraBehindPlayer"))
	float CameraLookAtBlend = 0.5f;

	// 카메라를 플레이어 기준 오른쪽으로 비껴 놓는 거리. 정면 뒤에서 보면 거울 속 앞모습이 몸에 가린다
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Console|Camera", meta = (EditCondition = "bPlaceCameraBehindPlayer"))
	float CameraSideOffset = 400.f;

	// StandPoint 도착 판정 반경(cm)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Console|Approach")
	float ApproachAcceptRadius = 12.f;

	// 접근 중 이동 입력 세기. 1이면 평소 걷기 속도 그대로
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Console|Approach", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float ApproachInputScale = 1.f;

	// 걷는 방향·거울 정면으로 시선을 돌리는 보간 속도
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Console|Approach")
	float ApproachTurnInterpSpeed = 8.f;

	// 장애물 등으로 못 도착해도 이 시간이 지나면 그 자리에서 거울을 보고 선택 화면을 연다
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Console|Approach")
	float ApproachTimeout = 4.f;

	// 회전 입력을 누르고 있는 동안 적용할 초당 회전 각도
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Console|Facing", meta = (ClampMin = "0.0"))
	float FacingTurnSpeed = 90.f;

	// 거울 면 머티리얼. 비우면 MirrorSurface의 첫 머티리얼을 쓴다
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Console|Mirror")
	TObjectPtr<UMaterialInterface> MirrorMaterial;

	// 거울 머티리얼에서 렌더타깃을 받는 텍스처 파라미터
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Console|Mirror")
	FName MirrorTextureParameterName = TEXT("T_Target");

	// 렌더타깃 크기. 반사면 쿼드 비율(104x163)에 맞춰야 거울 속 몸이 찌그러지지 않는다
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Console|Mirror")
	FIntPoint MirrorResolution = FIntPoint(512, 800);

	// 선택 시작 시 캡처가 StandPoint 위 이 높이를 바라보게 돌린다. 캐릭터가 거울 축에서 비켜 서도 반사가 중앙에 온다
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Console|Mirror")
	float MirrorCaptureLookAtHeight = 140.f;

private:
	void InitializeMirror();
	void PlaceCameraBehind(const AMD_Character* Character);
	void SetMirrorActive(bool bActive);
	void AimMirrorCaptureAtStandPoint();

	// 입력을 막고 카메라를 넘긴 뒤 StandPoint까지 걷기 시작한다
	void BeginApproach(AMD_Character* Character, APlayerController* PC);
	void TickApproach(float DeltaSeconds);
	// 도착·정면 정렬이 끝나면 선택 화면을 연다
	void FinishApproach();
	bool PushSelectWidget(APlayerController* PC);

	// 누르고 있는 입력 방향과 경과 시간에 따른 연속 회전
	void TickFacing(float DeltaSeconds);

	bool bApproaching = false;
	bool bReachedStandPoint = false;
	float ApproachElapsed = 0.f;

	// 왼쪽 -1, 정지 0, 오른쪽 1의 현재 회전 입력
	float FacingTurnInput = 0.f;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> MirrorRenderTarget;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> MirrorMaterialInstance;

	TWeakObjectPtr<AMD_Character> ActiveCharacter;
};
