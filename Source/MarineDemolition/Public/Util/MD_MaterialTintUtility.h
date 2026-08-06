#pragma once

#include "CoreMinimal.h"

class UMaterialInstanceDynamic;

/**
 * SetVectorParameterValue는 없는 파라미터 이름을 조용히 무시한다.
 * 이 프로젝트에서 두 번 겪었다 — M_Master_Opaque는 Tint가 아니라 Color고, M_MD_Wall은 0개다.
 */
namespace MDMaterialTint
{
	/** 파라미터가 있으면 넣고 true. 없으면 (머티리얼, 파라미터)당 한 번만 경고하고 false */
	MARINEDEMOLITION_API bool SetVectorParameterChecked(
		UMaterialInstanceDynamic* Material,
		FName ParameterName,
		const FLinearColor& Value);
}
