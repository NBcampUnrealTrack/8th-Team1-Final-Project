#pragma once

#include "Engine/EngineTypes.h"

namespace MDCollisionChannels
{
	/** Project Settings의 InteractTrace 채널과 동일해야 한다. */
	inline constexpr ECollisionChannel InteractTrace = ECC_GameTraceChannel1;
	inline constexpr ECollisionChannel SpillFloor = ECC_GameTraceChannel2;

	/** Project Settings의 BarnacleTrace 채널과 동일해야 한다. 기본 Block, 제외할 소품만 Ignore한다. */
	inline constexpr ECollisionChannel BarnacleTrace = ECC_GameTraceChannel3;
}
