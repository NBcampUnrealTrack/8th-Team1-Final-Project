#!/usr/bin/env bash
# Multi-User Editing 서버 실행 (맥미니 = 상시 호스트)
#
# 서버는 0.0.0.0 에 묶는다. 지금 구성에서는 Tailscale 만 쓰지만, 나중에 다른 경로가
# 생겨도 서버 쪽은 고칠 것이 없다.
#
# 처음 한 번:
#   chmod +x Tools/MultiUser/start-mu-server.command
#
# Epic 런처 설치본에는 서버 실행 파일이 이미 들어 있다. 빌드할 필요 없다.
# 소스 빌드에서 없다면 Program 타깃이라 기본 빌드에 빠진 것이니 먼저 빌드한다:
#   cd "$UE_ENGINE_DIR"
#   ./Engine/Build/BatchFiles/Mac/Build.sh UnrealMultiUserSlateServer Mac Development
#
# 상시 가동이므로 맥미니가 잠들지 않게 해둘 것:
#   시스템 설정 > 에너지  (또는 이 창을 caffeinate -s 로 감싸서 실행)

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/_common.sh"

require_python

ENGINE_DIR="$(find_engine)"
BIN_DIR="$ENGINE_DIR/Engine/Binaries/Mac"

# .app 번들 안의 실행 파일을 먼저 쓴다.
#
# Binaries/Mac 에는 같은 서버가 번들 밖에도 하나 놓여 있는데, 그걸 직접 부르면
# dyld 가 libUnrealMultiUserSlateServer-Concert.dylib 을 못 찾고 죽는다.
# 두 실행 파일의 LC_RPATH 목록은 같지만 기준점(@loader_path)이 다르기 때문이다.
#
#   번들 안 : .../Mac/<이름>.app/Contents/MacOS  + ../../../../../Plugins/...
#             -> Engine/Plugins/Developer/Concert/ConcertMain/Binaries/Mac   (있음)
#   번들 밖 : .../Engine/Binaries/Mac            + ../../../../../Plugins/...
#             -> /Users/Shared/Plugins/...                                   (없음)
#
# Slate 서버가 없으면 콘솔 서버로 대체한다. 둘 다 없으면 빌드 안내.
SERVER=""
for name in UnrealMultiUserSlateServer UnrealMultiUserServer; do
    for candidate in "$BIN_DIR/$name.app/Contents/MacOS/$name" "$BIN_DIR/$name"; do
        if [ -x "$candidate" ]; then
            SERVER="$candidate"
            break 2
        fi
    done
done

if [ -z "$SERVER" ]; then
    die "[오류] 멀티유저 서버 실행 파일이 없습니다." \
        "       $BIN_DIR/UnrealMultiUserSlateServer.app/Contents/MacOS/UnrealMultiUserSlateServer" \
        "" \
        "       Epic 런처 설치본에는 이미 들어 있습니다. 소스 빌드라면 Program 타깃이라" \
        "       엔진 기본 빌드에 포함되지 않으므로 먼저 빌드하세요:" \
        "         cd \"$ENGINE_DIR\"" \
        "         ./Engine/Build/BatchFiles/Mac/Build.sh UnrealMultiUserSlateServer Mac Development"
fi

SERVER_NAME="$(json_get 'cfg["serverName"]')"
SERVER_PORT="$(static_endpoints | head -n 1 | sed -E 's/.*://')"

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

  설정에 들어가야 할 값:

      host.json     -> "hostMachine": "$MY_HOSTNAME"
      DefaultEngine -> +StaticEndpoints=$MY_TAILSCALE:$SERVER_PORT

  이 값이 설정과 다르면 팀원이 접속하지 못합니다.
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
