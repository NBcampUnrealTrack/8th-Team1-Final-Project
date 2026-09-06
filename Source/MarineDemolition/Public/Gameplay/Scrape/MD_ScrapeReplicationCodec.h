#pragma once

#include "CoreMinimal.h"

/**
 * 오염 셀 상태를 복제용 최소 바이트로 줄이는 순수 계산 유틸리티.
 *
 * 셀 번호는 uint32, 초기 오염량과 잔여 오염량은 각각 uint8로 보낸다.
 * 셀 번호를 확장하되 런타임 셀 수는 SurfaceLayout의 메모리 상한으로 제한한다.
 *
 * 잔여 오염량 양자화에서 0은 "깨끗함" 전용으로 예약한다. 양자화 간격
 * 1/255는 기본 CleanTolerance 0.001보다 네 배 가까이 크기 때문에, 단순
 * 반올림을 쓰면 아직 깨끗하지 않은 값이 0으로 내려가 클라이언트만 그 셀을
 * 깨끗하다고 판단한다. 그러면 완료 이벤트가 서버와 다른 시점에 터진다.
 */
class MARINEDEMOLITION_API FMD_ScrapeReplicationCodec final
{
public:
	/** 유효하지 않은 셀 번호를 뜻하는 예약값. */
	static constexpr uint32 InvalidCellIndex = MAX_uint32;

	/** 잔여 오염량 양자화 최대 단계. 0은 깨끗함 전용이라 오염 단계는 1~255다. */
	static constexpr int32 QuantizationSteps = static_cast<int32>(MAX_uint8);

	/**
	 * 앞뒤 통합 셀 번호를 복제용 uint32로 바꾼다.
	 *
	 * 예약값과 SurfaceLayout의 메모리 상한을 벗어난 번호는 실패한다.
	 */
	static bool TryEncodeCellIndex(int32 CellIndex, uint32& OutEncodedCellIndex);

	/** 복제로 받은 uint32를 통합 셀 번호로 되돌린다. 범위 밖이면 INDEX_NONE. */
	static int32 DecodeCellIndex(uint32 EncodedCellIndex);

	/**
	 * 잔여 오염량을 0~255로 양자화한다.
	 *
	 * CleanTolerance 이하면 0을 돌려준다. 그보다 크면 역양자화 결과가 반드시
	 * CleanTolerance보다 크도록 최소 단계를 보장한다. 두 방향 모두 막았으므로
	 * 깨끗함 판정이 서버와 클라이언트에서 절대 갈리지 않는다.
	 */
	static uint8 QuantizeRemaining(
		float RemainingContamination,
		float CleanTolerance);

	/** 양자화된 잔여 오염량을 0~1 실수로 되돌린다. */
	static float DequantizeRemaining(uint8 QuantizedRemaining);

private:
	FMD_ScrapeReplicationCodec() = delete;

	/** 역양자화 결과가 CleanTolerance를 넘는 가장 작은 양자화 단계. */
	static uint8 CalculateMinimumContaminatedStep(float CleanTolerance);
};
