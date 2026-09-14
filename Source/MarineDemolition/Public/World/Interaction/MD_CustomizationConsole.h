#pragma once

#include "CoreMinimal.h"
#include "Gameplay/Interaction/MD_Interactable.h"
#include "GameFramework/Actor.h"
#include "MD_CustomizationConsole.generated.h"

class AMD_Character;
class UCameraComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UMD_CharacterSelectWidget;
class USceneCaptureComponent2D;
class UStaticMeshComponent;
class UTextureRenderTarget2D;

/**
 * 로비의 거울 콘솔. 상호작용하면 로컬에서 캐릭터 선택 화면을 열고
 * 뷰를 콘솔 카메라(캐릭터 뒷모습)로 넘긴다. 거울 면은 SceneCapture로 앞모습을 비춘다.
 * 서버 상호작용이 필요 없는 로컬 전용 대상이다.
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

	// 카메라와 메시 표시를 되돌린다. 선택 화면이 닫힐 때 호출된다
	void EndCustomization(AMD_Character* Character);

protected:
	virtual void BeginPlay() override;

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

	// 열릴 선택 화면 클래스. BP에서 할당한다
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Console")
	TSubclassOf<UMD_CharacterSelectWidget> SelectWidgetClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Console")
	float CameraBlendTime = 0.5f;

	// 열 때 카메라를 플레이어 뒤로 옮긴다. 플레이어에서 거울 반대쪽으로 이만큼 떨어진다
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Console|Camera")
	float CameraDistanceBehindPlayer = 320.f;

	// 플레이어 발 기준 카메라 높이
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Console|Camera")
	float CameraHeight = 130.f;

	// 카메라가 바라볼 거울 중심 높이 보정
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Console|Camera")
	float CameraLookAtHeightOffset = -40.f;

	// 카메라를 플레이어 기준 오른쪽으로 비껴 놓는 거리. 정면 뒤에서 보면 거울 속 앞모습이 몸에 가린다
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Console|Camera")
	float CameraSideOffset = 110.f;

	// 거울 면 머티리얼. 비우면 MirrorSurface의 첫 머티리얼을 쓴다
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Console|Mirror")
	TObjectPtr<UMaterialInterface> MirrorMaterial;

	// 거울 머티리얼에서 렌더타깃을 받는 텍스처 파라미터
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Console|Mirror")
	FName MirrorTextureParameterName = TEXT("T_Target");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Console|Mirror")
	FIntPoint MirrorResolution = FIntPoint(512, 512);

private:
	void InitializeMirror();
	void PlaceCameraBehind(const AMD_Character* Character);
	void SetMirrorActive(bool bActive);

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> MirrorRenderTarget;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> MirrorMaterialInstance;

	TWeakObjectPtr<AMD_Character> ActiveCharacter;
};
