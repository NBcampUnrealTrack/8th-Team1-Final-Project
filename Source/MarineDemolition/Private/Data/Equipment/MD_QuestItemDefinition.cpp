#include "Data/Equipment/MD_QuestItemDefinition.h"

#include "Component/Equipment/MD_EquipHotbarComponent.h"
#include "GameFramework/Actor.h"
#include "Util/MD_LogUtility.h"

bool UMD_QuestItemDefinition::CanGrant_Implementation(FMD_ItemGrantContext Context)
{
	if (Super::CanGrant_Implementation(Context) == false)
	{
		return false;
	}

	// 사용 GA가 없으므로 핫바의 빈 슬롯(또는 같은 품목 합산) 여부만 본다
	UMD_EquipHotbarComponent* Hotbar = Context.Recipient->FindComponentByClass<UMD_EquipHotbarComponent>();
	if (IsValid(Hotbar) == false || Hotbar->CanAddItem(this, Context.Quantity) == false)
	{
		UE_LOG(MarineDemolition, Verbose, TEXT("%s Hotbar에 퀘스트 아이템을 지급할 수 없음 (Item=%s)"), *UE_CALL_LOCATIONINFO, *GetPathName());
		return false;
	}

	return true;
}

bool UMD_QuestItemDefinition::Grant_Implementation(FMD_ItemGrantContext Context)
{
	if (IsValid(Context.Recipient) == false || Context.Recipient->HasAuthority() == false)
	{
		UE_LOG(MarineDemolition, Warning, TEXT("%s 퀘스트 아이템 지급 대상 또는 서버 권한이 유효하지 않음 (Item=%s)"), *UE_CALL_LOCATIONINFO, *GetPathName());
		return false;
	}

	// BP 재정의를 포함한 최신 지급 조건 검증
	if (CanGrant(Context) == false)
	{
		return false;
	}

	UMD_EquipHotbarComponent* Hotbar = Context.Recipient->FindComponentByClass<UMD_EquipHotbarComponent>();
	if (IsValid(Hotbar) == false)
	{
		return false;
	}

	return Hotbar->AddItem(this, Context.Quantity);
}
