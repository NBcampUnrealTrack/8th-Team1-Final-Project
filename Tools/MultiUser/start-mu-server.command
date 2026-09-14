#!/usr/bin/env bash
# Multi-User Editing 서버 실행 (맥미니 = 상시 호스트)
#
# 서버는 0.0.0.0 에 묶는다. 지금 구성에서는 Tailscale 만 쓰지만, 나중에 다른 경로가
# 생겨도 서버 쪽은 고칠 것이 없다.
#
# 처음 한 번:
#   chmod +x Tools/MultiUser/start-mu-server.command
#
# 서버 실행 파일이 없다면 엔진에서 먼저 빌드해야 한다 (Program 타깃이라 기본 빌드에 없다):
#   cd "$UE_ENGINE_DIR"
#   ./Engine/Build/BatchFiles/Mac/Build.sh UnrealMultiUserSlateServer Mac Development
#
# 상시 가동이므로 맥미니가 잠들지 않게 해둘 것:
#   시스템 설정 > 에너지  (또는 이 창을 caffeinate -s 로 감싸서 실행)

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/_common.sh"

require_python

ENGINE_DIR="$(find_engine)"
SERVER="$ENGINE_DIR/Engine/Binaries/Mac/UnrealMultiUserSlateServer"

# Slate 서버가 없으면 콘솔 서버로 대체한다. 둘 다 없으면 빌드 안내.
if [ ! -x "$SERVER" ]; then
    if [ -x "$ENGINE_DIR/Engine/Binaries/Mac/UnrealMultiUserServer" ]; then
        SERVER="$ENGINE_DIR/Engine/Binaries/Mac/UnrealMultiUserServer"
    else
        die "[오류] 멀티유저 서버 실행 파일이 없습니다." \
            "       $ENGINE_DIR/Engine/Binaries/Mac/UnrealMultiUserSlateServer" \
            "" \
            "       Program 타깃이라 엔진 기본 빌드에 포함되지 않습니다. 먼저 빌드하세요:" \
            "         cd \"$ENGINE_DIR\"" \
            "         ./Engine/Build/BatchFiles/Mac/Build.sh UnrealMultiUserSlateServer Mac Development"
    fi
fi

SERVER_NAME="$(json_get 'cfg["serverName"]')"
SERVER_PORT="$(json_get 'cfg["serverPort"]')"

MY_HOSTNAME="$(hostname -s)"
MY_TAILSCALE="$(ifconfig 2>/dev/null | awk '$1 == "inet" { print $2 }' | grep '^100\.' | head -n 1)"

if [ -z "$MY_TAILSCALE" ]; then
    die "[오류] Tailscale 주소(100.x)를 찾지 못했습니다." \
        "       Tailscale 을 실행하고 팀 tailnet 에 로그인한 뒤 다시 시도하세요:" \
        "         tailscale status"
fi

cat <<EOF

============================================================
  서버 이름 : $SERVER_NAME
  포트      : $SERVER_PORT  (0.0.0.0 / 모든 경로에서 수신)
============================================================

  host.json 에 들어가야 할 값:

      "hostMachine": "$MY_HOSTNAME",
      "hostAddress": "$MY_TAILSCALE"

  이 값이 host.json 과 다르면 팀원이 접속하지 못합니다.
============================================================

EOF

# 상시 가동: 이 프로세스가 도는 동안 맥이 잠들지 않게 한다.
# caffeinate 가 없으면 그냥 서버만 띄운다.
if command -v caffeinate >/dev/null 2>&1; then
    exec caffeinate -s "$SERVER" \
        "-CONCERTSERVER=$SERVER_NAME" \
        "-UDPMESSAGING_TRANSPORT_UNICAST=0.0.0.0:$SERVER_PORT"
else
    exec "$SERVER" \
        "-CONCERTSERVER=$SERVER_NAME" \
        "-UDPMESSAGING_TRANSPORT_UNICAST=0.0.0.0:$SERVER_PORT"
fi
