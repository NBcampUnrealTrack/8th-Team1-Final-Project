#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Gameplay/Interaction/MD_Interactable.h"
#include "MD_SupportBrace.generated.h"

class AMD_CuttableWall;

UCLASS()
class MARINEDEMOLITION_API AMD_SupportBrace : public AActor, public IMD_Interactable
{
	GENERATED_BODY()
public:
	AMD_SupportBrace();
	virtual void SetInteractionHighlighted_Implementation(bool bHighlighted) override;
	virtual FText GetInteractionDisplayName_Implementation() const override;
	virtual FText GetInteractionPrompt_Implementation(AActor* Interactor) const override;
	virtual bool CanInteract_Implementation(AActor* Interactor) const override;
	virtual void Interact_Implementation(AActor* Interactor) override;
	void SetSupportedWall(AMD_CuttableWall* Wall);
	void SetSupportedWalls(const TArray<TWeakObjectPtr<AMD_CuttableWall>>& Walls);
	AMD_CuttableWall* GetSupportedWall() const { return SupportedWall; }
	const TArray<TWeakObjectPtr<AMD_CuttableWall>>& GetSupportedWalls() const { return SupportedWalls; }
	const UStaticMeshComponent* GetBraceMesh() const { return Mesh; }
	bool IsWeakened() const { return bWeakened; }
	void RestoreWeakenedState(bool bWasWeakened);
	// 파일 복원의 교체 삭제가 철거와 점수 이벤트를 일으키지 않도록 연결만 해제
	void DetachForSaveRestore();
	void Weaken();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	UPROPERTY(VisibleAnywhere, Category = "Support")
	TObjectPtr<UStaticMeshComponent> Mesh;
	/** First wall retained for old saves and existing callers. */
	UPROPERTY(VisibleInstanceOnly, Category = "Support")
	TObjectPtr<AMD_CuttableWall> SupportedWall;
	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<AMD_CuttableWall>> SupportedWalls;
	UPROPERTY(Replicated, VisibleInstanceOnly, Category = "Support")
	bool bWeakened = false;

private:
	void ReleaseSupport(AActor* ResponsibleActor = nullptr);
	void UpdateReleasedWalls(const TArray<TWeakObjectPtr<AMD_CuttableWall>>& Walls, AActor* ResponsibleActor);
	TWeakObjectPtr<AActor> Dismantler;
	bool bDismantling = false;
};
