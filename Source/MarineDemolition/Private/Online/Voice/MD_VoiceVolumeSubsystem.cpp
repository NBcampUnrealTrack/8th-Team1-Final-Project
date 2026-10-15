#include "Online/Voice/MD_VoiceVolumeSubsystem.h"

#include "Online/Voice/MD_CharacterVOIPTalker.h"

bool UMD_VoiceVolumeSubsystem::FindVolumeMultiplier(FUniqueNetIdRepl PlayerUniqueId, float& OutVolumeMultiplier)
{
	// 다른 제공자의 동일 문자열과 구분되는 저장 키 조회
	FString PlayerKey = BuildPlayerKey(PlayerUniqueId);
	// 식별자 준비 전 다른 플레이어의 설정을 읽는 동작 차단
	if (PlayerKey.IsEmpty() == true)
	{
		// 호출자가 준비한 기본 배율을 보존한 조회 실패 전달
		return false;
	}

	// 현재 GameInstance에 보존된 개별 플레이어의 설정 검색
	float* StoredVolumeMultiplier = PlayerVolumeMultipliers.Find(PlayerKey);
	// 이전 사용자 설정이 없는 플레이어의 기본 배율 유지
	if (StoredVolumeMultiplier == nullptr)
	{
		// 저장된 설정이 없는 조회 결과 전달
		return false;
	}

	// 월드 이동 이전의 로컬 음성 배율 복원
	OutVolumeMultiplier = *StoredVolumeMultiplier;
	// 보존된 설정의 조회 성공 전달
	return true;
}

bool UMD_VoiceVolumeSubsystem::StoreVolumeMultiplier(FUniqueNetIdRepl PlayerUniqueId, float VolumeMultiplier)
{
	// 플레이어 표시 이름에 의존하지 않는 온라인 식별자 키 조회
	FString PlayerKey = BuildPlayerKey(PlayerUniqueId);
	// 식별자 준비 전 여러 플레이어의 설정이 합쳐지는 저장 차단
	if (PlayerKey.IsEmpty() == true)
	{
		// 확정되지 않은 플레이어의 저장 실패 전달
		return false;
	}

	// 현재 클라이언트에서 허용된 음성 배율의 세션 수명 보존
	PlayerVolumeMultipliers.Add(PlayerKey, FMath::Clamp(VolumeMultiplier, 0.0f, UMD_CharacterVOIPTalker::MaxVolumeMultiplier));
	// 유효한 플레이어 설정의 저장 성공 전달
	return true;
}

FString UMD_VoiceVolumeSubsystem::BuildPlayerKey(FUniqueNetIdRepl PlayerUniqueId)
{
	// 복제 또는 온라인 로그인 완료 전 불안정한 식별자 사용 차단
	if (PlayerUniqueId.IsValid() == false)
	{
		// 대체 이름 없이 저장 불가능한 키 반환
		return FString();
	}

	// 온라인 제공자 종류와 고유 식별자 값의 결합
	return PlayerUniqueId.GetType().ToString() + TEXT(":") + PlayerUniqueId.ToString();
}
