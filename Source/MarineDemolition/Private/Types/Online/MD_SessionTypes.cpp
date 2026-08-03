// Fill out your copyright notice in the Description page of Project Settings.


#include "Types/Online/MD_SessionTypes.h"
#include "OnlineSessionSettings.h"
#include "Online/Session/MD_SessionSettingKey.h"

void FMD_SessionInfo::ExportSessionInfo(const FOnlineSession& Session)
{
    // Session Settings에서 Server Name 값 추출
    Session.SessionSettings.Get(MD_SessionKey::Key_ServerName, ServerName);

    // Session Settings에서 Map Name 값 추출
    Session.SessionSettings.Get(MD_SessionKey::Key_MapName, MapName);

    // Session Settings에서 Room Code 값 추출
    Session.SessionSettings.Get(MD_SessionKey::Key_RoomCode, RoomCode);

    // Session의 Owner Name 복사
    OwnerName = Session.OwningUserName;

    // 최대 공개 접속 인원 수 복사
    MaxPlayers = Session.SessionSettings.NumPublicConnections;

    // 현재 참여 중인 인원 수 계산
    CurrentPlayers = MaxPlayers - Session.NumOpenPublicConnections;
}
