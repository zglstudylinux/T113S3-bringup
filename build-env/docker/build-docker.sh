#!/bin/bash
# T113 Tina 5.0 SDK Docker 编译包装脚本
#
# 用法(在任意目录均可,建议放 SDK 树里跑):
#   ./build-docker.sh build-image              构建镜像 t113-build:22.04
#   ./build-docker.sh shell [SDK_DIR]          进交互编译 shell(SDK 挂在 /sdk)
#   ./build-docker.sh run "命令" [SDK_DIR]     容器内非交互执行一条命令
#   ./build-docker.sh zgl-build [SDK_DIR]      一键全流程: kernel + openwrt_rootfs + pack
#                                              (无需任何预置脚本;SDK 自带 python3.8/make4.1
#                                               由构建系统自动接管,系统 python 由
#                                               python-is-python3 提供)
#
# SDK_DIR 缺省自动推断:本脚本位于 <sdk>/docs/build-env/docker/,向上三层即 SDK 根。
set -e

IMAGE_NAME="t113-build"
IMAGE_TAG="22.04"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

infer_sdk_dir() {
    # <sdk>/docs/build-env/docker/ -> <sdk>
    local d="$(cd "$SCRIPT_DIR/../../.." && pwd)"
    if [ -f "$d/build.sh" ]; then echo "$d"; return 0; fi
    echo ""
}

resolve_sdk_dir() {
    if [ -n "$2" ]; then
        (cd "$2" && pwd)
    else
        local d; d="$(infer_sdk_dir)"
        [ -n "$d" ] || { echo "ERROR: 无法推断 SDK 目录,请显式传入 SDK_DIR" >&2; exit 1; }
        echo "$d"
    fi
}

docker_run() {
    local sdk_dir="$1"; shift
    mkdir -p "$sdk_dir/.disclaimer_accpet" 2>/dev/null || touch "$sdk_dir/.disclaimer_accpet"
    docker run --rm -it \
        --network host \
        -e http_proxy -e https_proxy -e HTTP_PROXY -e HTTPS_PROXY \
        -e NO_PROXY -e no_PROXY \
        -v "$sdk_dir:/sdk" \
        -w /sdk \
        "${IMAGE_NAME}:${IMAGE_TAG}" "$@"
}

case "$1" in
    build-image)
        docker build \
            --build-arg HOST_UID="$(id -u)" \
            --build-arg HOST_GID="$(id -g)" \
            -t "${IMAGE_NAME}:${IMAGE_TAG}" \
            "$SCRIPT_DIR"
        echo "== 镜像 ${IMAGE_NAME}:${IMAGE_TAG} 构建完成 =="
        ;;
    shell)
        SDK_DIR="$(resolve_sdk_dir "$@")"
        echo "== SDK: $SDK_DIR -> /sdk =="
        docker_run "$SDK_DIR" /bin/bash
        ;;
    run)
        # run "命令" [SDK_DIR]
        CMD="$2"
        [ -n "$CMD" ] || { echo "ERROR: run 需要一条命令" >&2; exit 1; }
        SDK_DIR="$(resolve_sdk_dir "" "$3")"
        echo "== SDK: $SDK_DIR -> /sdk =="
        # pty 包装:全志 build 脚本依赖交互终端,非交互执行必须走 script -qec
        docker_run "$SDK_DIR" bash -lc "script -qec \"$CMD\" /tmp/build-docker.log"
        ;;
    zgl-build)
        SDK_DIR="$(resolve_sdk_dir "" "$2")"
        echo "== SDK: $SDK_DIR -> /sdk =="
        INNER="source build/envsetup.sh && lunch t113-zgl_board && ./build.sh kernel -j8 && ./build.sh openwrt_rootfs -j1 && ./build.sh pack"
        docker_run "$SDK_DIR" bash -lc "script -qec \"$INNER\" /tmp/zgl-build.log"
        ;;
    *)
        grep '^#' "$0" | sed -n '2,12p' | sed 's/^# \{0,1\}//'
        exit 1
        ;;
esac
