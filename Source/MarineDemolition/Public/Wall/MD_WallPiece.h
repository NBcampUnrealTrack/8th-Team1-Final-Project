// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MD_WallPiece.generated.h"

// 격벽 조각 스텁. 절단 시 드랍되며 오염 여부만 보유. 픽업/운반은 샘플 회수 카드에서.
UCLASS()
class MARINEDEMOLITION_API AMD_WallPiece : public AActor
{
	GENERATED_BODY()

public:
	AMD_WallPiece();

	// 오염 여부 지정 + 색 적용. 스폰 직후 호출
	void SetContaminated(bool bInContaminated);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Piece")
	bool bContaminated = false;

protected:
	UPROPERTY(VisibleAnywhere, Category = "Piece")
	TObjectPtr<UStaticMeshComponent> Mesh;
};
