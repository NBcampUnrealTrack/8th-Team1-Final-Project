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

	/**
	 * 스칼라 버전. SetScalarParameterValue도 없는 이름을 조용히 무시하는 것은 마찬가지다.
	 * 진행도처럼 값이 안 들어가도 화면이 그냥 안 변할 뿐인 경우에 특히 찾기 어렵다.
	 */
	MARINEDEMOLITION_API bool SetScalarParameterChecked(
		UMaterialInstanceDynamic* Material,
		FName ParameterName,
		float Value);
}
