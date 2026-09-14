#include "Data/Equipment/MD_ItemDefinition.h"
#include "Util/MD_LogUtility.h"
#include "GameFramework/Actor.h"

FPrimaryAssetId UMD_ItemDefinition::GetPrimaryAssetId() const
{
	// 개별 에셋에 지정한 분류와 이름으로 공통 식별자 구성
	return FPrimaryAssetId(AssetType, ItemId);
}

bool UMD_ItemDefinition::CanGrant_Implementation(FMD_ItemGrantContext Context)
{
	// 대상과 식별자 및 수량이 유효하지 않은 지급 요청의 공통 차단
	if (IsValid(Context.Recipient) == false || GetPrimaryAssetId().IsValid() == false || Context.Quantity <= 0)
	{
		// 공통 지급 조건 실패 원인의 프로젝트 로그 기록
		UE_LOG(MarineDemolition, Warning, TEXT("%s 지급 대상, AssetType/ItemId 또는 지급 수량이 유효하지 않음 (Item=%s)"), *UE_CALL_LOCATIONINFO, *GetPathName());
		// 공통 지급 데이터 검증 실패 전달
		return false;
	}
	// 파생 정의의 추가 검증에 사용할 공통 조건 통과 결과
	return true;
}

bool UMD_ItemDefinition::Grant_Implementation(FMD_ItemGrantContext Context)
{
	// 유효한 지급 대상에 한해서 서버 권한 확인
	if (IsValid(Context.Recipient) == false || Context.Recipient->HasAuthority() == false)
	{
		// 지급 대상 또는 서버 권한 확인 실패의 프로젝트 로그 기록
		UE_LOG(MarineDemolition, Warning, TEXT("%s 지급 대상이 유효하지 않거나 실제 아이템 지급의 서버 권한이 없음 (Item=%s)"), *UE_CALL_LOCATIONINFO, *GetPathName());
		// 직접 지급 실행 조건 실패 전달
		return false;
	}
	// BP 재정의를 포함한 최신 지급 조건 확인
	if (CanGrant(Context) == false)
	{
		// 선행 검사에서 기록한 실패 결과의 전달
		return false;
	}
	// 실패 원인의 프로젝트 로그 기록
	UE_LOG(MarineDemolition, Warning, TEXT("%s 기본 지급 함수에 실제 지급 동작이 연결되지 않음 (Item=%s)"), *UE_CALL_LOCATIONINFO, *GetPathName());
	// 실제 지급이 연결되지 않은 기본 구현의 성공 차단
	return false;
}
