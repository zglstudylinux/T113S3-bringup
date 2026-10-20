# T113 Tina 5.0 SDK 可复现编译环境(Ubuntu 22.04 基线)
#
# 为什么是 22.04:
#   - SDK 对宿主 python 的窗口是 3.5~3.10(openwrt prereq-build.mk),22.04 的
#     3.10 正好在窗口内;24.04 的 3.12 缺 distutils 会挂(虽然 SDK 自带
#     python3.8,但 openwrt 首轮 prereq 探测仍扫系统 python3)。
#   - pack 的 32 位工具(fsbuild/dragon/update_23 等 i386 ELF)需要
#     libc6-i386 + lib32stdc++6,22.04 完整提供。
#
# UID/GID 直通:容器内 builder 用户与宿主用户同 uid/gid,
# bind-mount 的 SDK 目录里生成的文件属主保持宿主用户。
FROM ubuntu:22.04

ARG HOST_UID=1000
ARG HOST_GID=1000
ARG APT_MIRROR=""

ENV DEBIAN_FRONTEND=noninteractive \
    LC_ALL=C.UTF-8 \
    LANG=C.UTF-8

# 可选国内源(--build-arg APT_MIRROR=mirrors.aliyun.com)
RUN if [ -n "$APT_MIRROR" ]; then \
      sed -i "s|http://archive.ubuntu.com|http://${APT_MIRROR}|g; s|http://security.ubuntu.com|http://${APT_MIRROR}|g" /etc/apt/sources.list; \
    fi

# 编译依赖清单(apt-packages.txt,教程 apt 列表超集)
COPY apt-packages.txt /tmp/apt-packages.txt
RUN apt-get update && \
    xargs -a /tmp/apt-packages.txt apt-get install -y --no-install-recommends && \
    rm -rf /var/lib/apt/lists/* /tmp/apt-packages.txt

# builder 用户(与宿主 uid/gid 对齐)
RUN groupadd -g ${HOST_GID} builder && \
    useradd -m -u ${HOST_UID} -g ${HOST_GID} -s /bin/bash builder && \
    mkdir -p /etc/sudoers.d && \
    echo "builder ALL=(ALL) NOPASSWD:ALL" > /etc/sudoers.d/builder

# git safe.directory:挂载进来的 SDK 仓库属主有时会被判为非当前用户
# (以 builder 身份跑,免 sudo;sudoers 文件 build 阶段用 chmod 440 会在挂载后失效,不必依赖)
USER builder
RUN git config --global --add safe.directory '*' && \
    git config --global user.name "builder" && \
    git config --global user.email "builder@docker.local"

WORKDIR /sdk
CMD ["/bin/bash"]
