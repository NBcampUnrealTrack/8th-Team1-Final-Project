#!/usr/bin/env bash
# Multi-User Editing 스크립트 공용 함수 (macOS).
# ifconfig 출력 형식과 ping 의 -t(타임아웃) 의미가 BSD 기준이라 리눅스에서는 안 맞는다.
# Windows 쪽 _Common.ps1 과 같은 일을 한다. 두 파일은 같은 host.json 을 읽는다.
#
# macOS 에는 Radmin VPN 이 없다(Windows 전용 제품). 따라서 맥은 항상 Tailscale 경로로
# 떨어진다 — host.json 의 radmin 항목은 어댑터가 없어 자동으로 건너뛴다.

set -uo pipefail

TOOLS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$TOOLS_DIR/../.." && pwd)"
PROJECT_FILE="$PROJECT_DIR/MarineDemolition.uproject"
HOST_JSON="$TOOLS_DIR/host.json"

# 엔진 위치는 맥마다 다르다. 아래 순서로 찾고, 못 찾으면 UE_ENGINE_DIR 을 요구한다.
ENGINE_CANDIDATES=(
    "${UE_ENGINE_DIR:-}"
    "$HOME/UnrealEngine"
    "$HOME/dev/UnrealEngine"
    "/Users/Shared/Epic Games/UE_5.8"
    "/Users/Shared/UnrealEngine/UE_5.8"
)

die() {
    echo "" >&2
    for line in "$@"; do echo "$line" >&2; done
    echo "" >&2
    exit 1
}

require_python() {
    if command -v python3 >/dev/null 2>&1; then
        PYTHON=python3
    elif command -v /usr/bin/python3 >/dev/null 2>&1; then
        PYTHON=/usr/bin/python3
    else
        die "[오류] python3 이 없습니다. host.json 을 읽을 수 없습니다." \
            "       맥에서 언리얼을 빌드하려면 Xcode Command Line Tools 가 필요하고," \
            "       거기에 python3 가 포함됩니다:  xcode-select --install"
    fi
}

find_engine() {
    for c in "${ENGINE_CANDIDATES[@]}"; do
        [ -n "$c" ] || continue
        if [ -x "$c/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor" ]; then
            echo "$c"
            return 0
        fi
    done
    die "[오류] 언리얼 엔진을 찾지 못했습니다." \
        "       다음 위치를 확인했습니다:" \
        "$(printf '         %s\n' "${ENGINE_CANDIDATES[@]}")" \
        "" \
        "       엔진 경로를 직접 지정해서 다시 실행하세요:" \
        "         UE_ENGINE_DIR=/path/to/UnrealEngine $0"
}

# host.json 에서 값을 꺼낸다.  json_get <파이썬표현식>
json_get() {
    "$PYTHON" - "$HOST_JSON" "$1" <<'PY'
import json, sys
cfg = json.load(open(sys.argv[1], encoding="utf-8"))
print(eval(sys.argv[2], {"cfg": cfg}))
PY
}

# 이 맥에서 주어진 서브넷 접두사를 가진 IPv4 주소를 찾는다.
local_address_for() {
    local prefix="$1"
    # BSD ifconfig 는 주소 줄을 탭으로 들여쓴다: "\tinet 100.74.171.37 --> ..."
    ifconfig 2>/dev/null \
        | awk '$1 == "inet" { print $2 }' \
        | grep "^${prefix}" \
        | head -n 1
}

# host.json 의 transports 를 순서대로 시도해 쓸 수 있는 첫 경로를 고른다.
# 성공하면 SELECTED_NAME / SELECTED_LOCAL / SELECTED_HOST 를 채운다.
select_transport() {
    local count
    count="$(json_get 'len(cfg["transports"])')"

    local failures=()
    local i
    for (( i=0; i<count; i++ )); do
        local name prefix host_addr local_ip
        name="$(json_get "cfg['transports'][$i]['name']")"
        prefix="$(json_get "cfg['transports'][$i]['subnetPrefix']")"
        host_addr="$(json_get "cfg['transports'][$i]['hostAddress']")"

        printf '  %-10s 확인 중...' "$name"

        local_ip="$(local_address_for "$prefix")"
        if [ -z "$local_ip" ]; then
            echo " 인터페이스 없음"
            if [ "$name" = "radmin" ]; then
                failures+=("  - $name: macOS 에는 Radmin VPN 이 없습니다 (Windows 전용). 정상입니다.")
            else
                failures+=("  - $name: ${prefix}* 주소를 가진 인터페이스가 없음")
            fi
            continue
        fi

        if ! ping -c 1 -t 2 "$host_addr" >/dev/null 2>&1; then
            echo " 호스트 $host_addr 응답 없음"
            failures+=("  - $name: 내 주소는 $local_ip 인데 호스트 $host_addr 에 닿지 않음")
            continue
        fi

        echo " OK ($local_ip)"
        SELECTED_NAME="$name"
        SELECTED_LOCAL="$local_ip"
        SELECTED_HOST="$host_addr"
        return 0
    done

    die "[오류] 사용할 수 있는 VPN 경로가 없습니다." \
        "" \
        "${failures[@]}" \
        "" \
        "       Tailscale 을 실행하고 팀 tailnet 에 로그인한 뒤 다시 시도하세요:" \
        "         tailscale status" \
        "       호스트 주소가 바뀌었다면 Tools/MultiUser/host.json 을 고쳐야 합니다."
}

# 선택된 경로를 머신별 설정에 기록한다. Saved/ 는 .gitignore 대상이다.
# 맥은 WindowsEditor 가 아니라 MacEditor 를 쓴다.
write_multiuser_config() {
    local display_name="$1"
    local port
    port="$(json_get 'cfg["serverPort"]')"

    local dir="$PROJECT_DIR/Saved/Config/MacEditor"
    mkdir -p "$dir"
    local ini="$dir/Engine.ini"

    # 이 스크립트가 관리하는 블록만 걷어낸다.
    if [ -f "$ini" ]; then
        awk '
            /^[[:space:]]*;[[:space:]]*<<< MultiUser launcher/ { skip=1; next }
            /^[[:space:]]*;[[:space:]]*>>> MultiUser launcher/ { skip=0; next }
            !skip
        ' "$ini" > "$ini.tmp" && mv "$ini.tmp" "$ini"
    else
        : > "$ini"
    fi

    cat >> "$ini" <<EOF
; <<< MultiUser launcher — 이 블록은 start-mu-editor.command 가 매번 덮어쓴다. 직접 고치지 말 것.
; 선택된 경로: $SELECTED_NAME  (내 주소 $SELECTED_LOCAL → 호스트 $SELECTED_HOST:$port)
[/Script/UdpMessaging.UdpMessagingSettings]
UnicastEndpoint=$SELECTED_LOCAL:0
!StaticEndpoints=ClearArray
+StaticEndpoints=$SELECTED_HOST:$port

[/Script/ConcertClient.ConcertClientConfig]
ClientSettings=(DisplayName="$display_name",DiscoveryTimeoutSeconds=30)
; >>> MultiUser launcher
EOF

    echo "$ini"
}
