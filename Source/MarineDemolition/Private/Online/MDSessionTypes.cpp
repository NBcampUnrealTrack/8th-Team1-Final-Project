// Fill out your copyright notice in the Description page of Project Settings.


#include "Online/MDSessionTypes.h"
#include "OnlineSessionSettings.h"
#include "MDSessionSettingKey.h"

void FMDSessionInfo::ExportSessionInfo(const FOnlineSession& Session)
{
    // Session Settings에서 Server Name 값 추출
    Session.SessionSettings.Get(MDSessionKey::Key_ServerName, ServerName);

    // Session Settings에서 Map Name 값 추출
    Session.SessionSettings.Get(MDSessionKey::Key_MapName, MapName);

    // Session의 Owner Name 복사
    OwnerName = Session.OwningUserName;

    // 최대 공개 접속 인원 수 복사
    MaxPlayers = Session.SessionSettings.NumPublicConnections;

    // 현재 참여 중인 인원 수 계산
    CurrentPlayers = MaxPlayers - Session.NumOpenPublicConnections;
}
