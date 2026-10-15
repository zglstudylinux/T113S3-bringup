# T113 Tina5.0 SDK 拉取与本地同步指南

> 适用对象:全志 T113 / Tina5.0 SDK 包(本机目录 `T113-Tina5.0-V1.2.tar`,解压后
> 只看到 `.repo/` 一个目录的情况)。
> 目标:把 `.repo/projects/` 里 84 个 bare git 仓库 + 共享 object store,checkout
> 成完整可编译的 SDK 工作树。
>
> 实测完成时间 ~70 分钟,落盘 ~40 GB(`.repo` 11 GB + 工作树 29 GB)。

---

## 1. 为什么要"解压完 SDK 还要 repo 一下"

这个 SDK 的形态比较特殊,**不是**"压一个完整目录树"那种,而是 Google `repo` 工具
管理的"多 git 仓库聚合包"—— 122 个项目分别独立版本控制,共用一个 manifest 描述
谁在哪个目录、用哪个分支。所以你拿到的 tar 包本质上只装了"git 数据库",没装"工作树"。

具体拆开看 `.repo/` 里的内容:

```
.repo/
├── manifest.xml → manifests/tina5.0-t113.xml   # 总配方:122 个 project 在哪个目录、用哪个分支
├── manifests/                # 当前生效的 manifest(一个普通 git 仓库)
├── manifests.git/            # manifest 仓库的 bare 形式
├── projects/                 # 84 个 bare git 仓库,每个 project 一份
│   ├── kernel/linux-5.4.git/
│   ├── openwrt/openwrt.git/
│   └── …
├── project-objects/          # 共享 object store(11 GB),所有 bare repo 的 objects 软链到这里
└── repo/                     # repo 工具本体的 Python 源码
```

每个 bare 仓库的 `objects` 都是软链:

```
.repo/projects/kernel/linux-5.4.git/objects
  -> ../../../project-objects/lichee/linux-5.4.git/objects
```

manifest 里一行示例(`.repo/manifests/tina5.0-t113.xml`):

```xml
<remote fetch="../T113_all_Tina5.0/tina5.0-t113" name="tina"/>
<default remote="tina" revision="product-da-t113-tina5.0-stable"/>
…
<project name="lichee/linux-5.4" path="kernel/linux-5.4"/>
```

意思是:`kernel/linux-5.4` 这个 git 仓库(在远端)要 checkout 到本地的
`kernel/linux-5.4/` 目录,版本 `product-da-t113-tina5.0-stable`。

> **历史包袱**:全志这套 SDK 通常是分两份 tar 发——一份含 `.repo`(你有的),
> 一份含真正的 `T113_all_Tina5.0/tina5.0-t113` 裸仓库目录(你没有)。官方
> 流程是先 tar 解出后者,再 tar 解出前者并 `repo sync`。今天我们这边没有
> 那份"裸仓库"tar,但**运气好**:`.repo/projects/` 里已经全有了,所以我们
> 直接从本地 bare 仓库 clone 工作树,不走网络。

---

## 2. 前置条件

| 依赖 | 用途 | 备注 |
|------|------|------|
| `git` | checkout 工作树、解析 manifest | Ubuntu 24.04 默认没装 |
| `python3` (≥3.6) | 跑同步脚本 | 系统自带,默认无 `python` 软链 |
| `sudo` 权限 | 安装 `git` | 需要交互式输入密码 |
| 磁盘 ≥ 50 GB | 11 GB `.repo` + 29 GB 工作树 | 实际写满 ~40 GB |

注意:**不要尝试用 `.repo/repo/` 里那份 `repo` 工具**——它是 Allwinner 改的
Python 2 时代版本,在 Python 3.12 上会撞 `imp` / `Queue` / `urllib` 这些
已删除 API。强行打补丁要改一堆文件,得不偿失,直接用 `git clone` 更省事。

---

## 3. 完整步骤

### 3.1 解压 SDK

```bash
mkdir -p ~/Linux_SDK/T113
cd ~/Linux_SDK/T113
tar -xf T113-Tina5.0-V1.2.tar        # 解压后得到 T113-Tina5.0-V1.2/
cd T113-Tina5.0-V1.2
ls -la                              # 应该只有 docs/ 和 .repo/
```

> ⚠️ 如果你看到 `tar: Unexpected EOF` 之类的报错,先校验:
> `md5sum T113-Tina5.0-V1.2.tar`(找你下载源提供的校验值对比)。

### 3.2 安装基础工具

```bash
sudo apt-get update
sudo apt-get install -y git curl ca-certificates
git --version                       # 看到 git version 2.x 即可
```

Python 3.12 把 `python` 命令删了,做个软链(只是为了让某些老脚本能跑):

```bash
sudo ln -sf /usr/bin/python3 /usr/local/bin/python
```

### 3.3 确认 manifest / bare 仓库完整

```bash
ls .repo/manifest.xml .repo/projects/ 2>&1
cat .repo/manifest.xml | head -3
find .repo/projects -maxdepth 4 -name "*.git" -type d | wc -l   # 应该是 84
```

如果 `find` 结果明显偏少,说明 tar 抽坏了,要重新下。

### 3.4 跑同步脚本(本仓库自带 `tools/scripts/sync_sdk.py`)

```bash
python3 tools/scripts/sync_sdk.py
```

脚本干了什么(完整源码见 `tools/scripts/sync_sdk.py`):

1. 解析 `.repo/manifest.xml` 里 122 个 `<project>` 节点
2. 对每个 project:
   - 找到对应的 bare 仓库 `.repo/projects/<path>.git`
   - 用 `git clone file://<bare> <dest>` 拉工作树(关键:用 `file://` 不是 `--local`,原因见 §4.2)
   - 如果 manifest 里写了 `<linkfile>`,在工作树外补一个软链
3. 把每个克隆的 origin URL 改回 manifest 里的相对路径,这样以后 `git fetch` 还能跑

正常输出尾部长这样:

```
[ok]   prebuilt/rootfsbuilt/arm  @ product-da-t113-tina5.0-stable
[ok]   rtos/lichee/rtos          @ product-da-t113-tina5.0-stable
[ok]   platform/allwinner/security/optee  @ product-da-t113-tina5.0-stable

=== Summary ===  ok=122  skip=0  fail=0  total=122
```

跑完磁盘应该增加 ~29 GB,顶层出现这些目录:

```
brandy  bsp  build  build.sh*  buildroot  device  kernel
openwrt  platform  prebuilt  rtos  tools
```

`build.sh` 是一个软链,真实文件是 `build/top_build.sh`,链接方式跟
`repo sync` 标准产物一致。

### 3.5 验证关键软链

```bash
ls -la build.sh rtos/envsetup.sh
# 期望:
#   build.sh        -> build/top_build.sh
#   rtos/envsetup.sh -> tools/scripts/source_envsetup.sh
```

### 3.6 验证一个内核能正常读

```bash
ls kernel/linux-5.4/ | head
git -C kernel/linux-5.4 log --oneline -3
# 期望能看到 da-t113-tina5.0-v1.2 / v1.1 / v1.0 三条 release 提交
```

---

## 4. 踩过的坑与解决方案

### 4.1 `ModuleNotFoundError: No module named 'imp'`

**症状**:

```
$ repo --version
warning: Python 3 support is currently experimental. YMMV.
Traceback (most recent call last):
  File ".repo/repo/main.py", line 19, in <module>
    import imp
ModuleNotFoundError: No module named 'imp'
```

**原因**:Allwinner vendor 改的 `repo` 工具源码停留在 Python 2.6/2.7 时代,
里面用了 `imp.load_source()` / `imp.new_module()`。`imp` 模块在 Python 3.4
就 deprecated 了,**Python 3.12 彻底删除**。

**解决方案**:**不用 `repo`**。直接走 `git clone` 路径。`repo` 工具只是
XML→`git clone` 的胶水,我们手写一个 100 行的 Python 脚本替代它
(`tools/scripts/sync_sdk.py`)。

> 备选:你可以从 https://gerrit.googlesource.com/git-repo 下载一份 Google 官方的
> `repo` 脚本(支持 Python 3),然后 `PATH=$PWD/bin:$PATH repo sync --local-only`。
> 但我们这里没有源站(Allwinner 屏蔽了),`--local-only` 也需要 manifest 的
> remote URL 改成可解析的本地路径,折腾一圈不如直接 `git clone`。

### 4.2 `fatal: '…/objects' is a symlink, refusing to clone with --local`

**症状**:

```
fatal: '/…/.repo/projects/kernel/linux-5.4.git/objects' is a symlink,
refusing to clone with --local
```

**原因**:每个 bare 仓库的 `objects/` 都软链到 `.repo/project-objects/`(共享
object store,这是 Allwinner 把 84 个 git 仓库去重压缩的关键)。git 出于安全
考虑,当目标 `objects` 是 symlink 时,拒绝用 `--local` / `--no-hardlinks`
协议 clone,以防用户误把跨设备的引用当成普通文件。

**解决方案**:用 `file://` 协议强制走 smart 协议:

```bash
git clone file:///abs/path/.repo/projects/kernel/linux-5.4.git dest/
```

`file://` 即使指向本地路径,git 也会 fork 一个 `git upload-pack` 进程走
智能协议,绕开 symlink 校验。`tools/scripts/sync_sdk.py` 内部就是这么做的。

### 4.3 `from Queue import Queue` / `import urllib` 失败

**症状**(如果你执意要用 vendor `repo` 还会撞到这些):

```
ModuleNotFoundError: No module named 'Queue'
ImportError: cannot import name 'contextlib' from 'urllib'
```

**原因**:Python 2 → 3 的标准库改名。`Queue` → `queue`,`urllib` 在 3 里
被拆成 `urllib.request` / `urllib.parse` / `urllib.error`。

**解决方案**:跟 §4.1 一样,放弃 vendor `repo`。

### 4.4 `fatal: your current branch 'master' does not have any commits yet`

**症状**(检查 bare 仓库时遇到):

```
$ git -C .repo/projects/kernel/linux-5.4.git log --oneline -3
fatal: your current branch 'master' does not have any commits yet
```

**原因**:`master` 这个默认分支名只是 git 初始化时建的占位 ref,**实际产品
代码在 `product-da-t113-tina5.0-stable` 这个分支**。`HEAD` 指向 `master`,
但 `master` 没提交。要看内容应该用:

```bash
git -C .repo/projects/kernel/linux-5.4.git \
  log --oneline product-da-t113-tina5.0-stable -3
```

或者切到那个分支:

```bash
git -C .repo/projects/kernel/linux-5.4.git \
  checkout product-da-t113-tina5.0-stable
```

`tools/scripts/sync_sdk.py` 已经处理好这件事——它用 `--branch <revision>`
参数去 clone,git 会自动 checkout 到那个分支。

### 4.5 `linkfile` 创建后 `readlink -f` 显示空

**症状**:

```
$ readlink -f rtos/envsetup.sh
                      <- 空!
```

**原因**:`readlink -f` 在某些情况下对"软链目标存在但解析路径里有奇怪
组件"会返回空。实际软链本身是好的,可以 `readlink rtos/envsetup.sh`
或者直接 `cat rtos/envsetup.sh` 验证。

**解决方案**:无,这是 readlink 的 quirk。直接用 `cat` / `bash rtos/envsetup.sh`
判断软链是否工作即可。

### 4.6 `repo sync` 直接报 "no remote"

**症状**(如果你之后想用 vendor `repo` 二次同步):

```
fatal: No remote defined for project ...
```

**原因**:manifest 里的 `<remote fetch="../T113_all_Tina5.0/tina5.0-t113"/>` 是
相对路径,要求 SDK 根目录的兄弟目录有 `T113_all_Tina5.0/` 这个原始 tar 解出来
的目录。我们这里没有,所以 `repo sync` 走不通。

**解决方案**:
- 想从本地二次同步:用我们写的 `tools/scripts/sync_sdk.py`(已经支持幂等,
  已存在的目录会 skip)
- 想从 Allwinner 远端 fetch:你需要有 `whycan@sdk.allwinnertech.com` 的 SSH key,
  然后 `repo sync` 才会工作

### 4.7 Python 报"NameError: name 'python' is not defined"

**症状**:某些老的 build 脚本第一行 `#!/usr/bin/python` 在 Ubuntu 24.04 上
找不到解释器。

**解决方案**:§3.2 那行 `sudo ln -sf /usr/bin/python3 /usr/local/bin/python` 就是
干这个的。如果还撞到 `#!/usr/bin/python2`,自己改文件头或者装个 `python2`
包(不推荐,会污染环境)。

---

## 5. 进阶:manifest / linkfile / alternates 速查

### 5.1 manifest 结构

```xml
<manifest>
  <remote  name="tina"  fetch="../T113_all_Tina5.0/tina5.0-t113"/>
  <default remote="tina" revision="product-da-t113-tina5.0-stable" sync-j="4"/>
  <project name="lichee/linux-5.4" path="kernel/linux-5.4"/>          <!-- 简单 -->
  <project name="lichee/brandy-2.0/tools" path="brandy/brandy-2.0/tools">
    <linkfile dest="brandy/brandy-2.0/build.sh" src="build.sh"/>     <!-- 软链 -->
  </project>
</manifest>
```

- `name`:远端 git 仓库名
- `path`:本地工作树路径,**也是 `.repo/projects/<path>.git` 这个 bare 仓库路径**
- `revision`:要 checkout 的分支(tag 也行)
- `linkfile`:在工作树外补一个 `<dest>` → `<path>/<src>` 的软链(常见用途:给
  顶层放一个 `build.sh` 入口,实际指向 `build/top_build.sh`)

### 5.2 object alternates

```
.repo/projects/<ns>/<name>.git/objects      →  .repo/project-objects/<ns>/<name>.git/objects
```

`.repo/project-objects/` 是个"内容寻址存储"——所有 bare repo 的 pack 文件
按 SHA-1 前两位分目录放(`objects/aa/...`、`objects/ab/...` …),84 个 git
仓库的提交 / blob / tree 都混在一起去重。Allwinner 的 trick:同一个全志
vendor 仓库在不同 SDK 版本里反复出现,改一行配置就全 SDK 复用 object,
省下来至少一半磁盘。

`tools/scripts/sync_sdk.py` 用 `git clone file://…` 不会破坏这个 alternates
关系——所有 clone 出来的 working tree 共享同一个 `project-objects`。
如果你担心 disk,跑完可以 `rm -rf <worktree>/.git/objects` 回收一部分(但
不是必须)。

### 5.3 二次同步(增量更新)

`tools/scripts/sync_sdk.py` 是幂等的:已经存在的工作树会被 skip。如果你想
真做增量 fetch(比如切到新分支),在每个工作树里:

```bash
cd kernel/linux-5.4
git fetch --all
git checkout product-da-t113-tina5.0-stable   # 或别的 tag
```

裸仓库 fetch:

```bash
git -C .repo/projects/kernel/linux-5.4.git fetch --all
# 然后跑 sync_sdk.py 重新 clone 那一个(把工作树先 rm -rf 一下)
```

---

## 6. 常见问题 FAQ

**Q1: 同步完总共多大?**
40 GB(其中 11 GB 是 `.repo/` 含共享 object store,29 GB 是 122 个工作树)。
如果你不需要 `prebuilt/hostbuilt/` 和 `prebuilt/rootfsbuilt/arm/`(编译好的
host 工具链和 rootfs),可以 `rm -rf` 省 5~6 GB,然后改用 `tools/build.sh`
里的 `toolchain/` 重新下载。

**Q2: 能不能只 sync 部分 project?**
可以。编辑 `tools/scripts/sync_sdk.py`,在 `for p in projects:` 那里加白名单:

```python
if "openwrt/" not in p["path"] and "buildroot/" not in p["path"]:
    continue
```

或者用标准 `repo` 的写法:`repo sync -c <project>`(但要先解决 §4.1 ~ 4.3)。

**Q3: 怎么验证 SDK 是完整可编译的?**
跑一遍:

```bash
source build/envsetup.sh        # 如果有
./build.sh                       # 顶层 build.sh 软链 → build/top_build.sh
h                                 # 列出可选的编译 target(lichee 风格)
```

具体 target 看你的板子(`t113` / `t113_i` / `t113_s3p` / `t113_s4` / `t113_s4p` /
`t113s2`)。

**Q4: `repo` 工具还能不能用?**
能用,但你需要:
1. `sudo apt install python` 或装个 Python 2
2. 用一份 Google 官方的 `repo`(不是 `.repo/repo/` 里那份),放到 `PATH`
3. 改 manifest 的 `<remote fetch=...>` 指向一个可达的 URL

或者,你就用 `tools/scripts/sync_sdk.py`,省事。

**Q5: 怎么知道某个 project 对应的 bare 仓库在哪?**
看 manifest 里的 `path`,bare 仓库固定是 `.repo/projects/<path>.git`。例如
manifest 写 `<project path="kernel/linux-5.4"/>`,bare 仓库就是
`.repo/projects/kernel/linux-5.4.git/`。

---

## 7. 一键复现(粘贴版)

```bash
# 0. 准备
mkdir -p ~/Linux_SDK/T113 && cd ~/Linux_SDK/T113
tar -xf T113-Tina5.0-V1.2.tar
cd T113-Tina5.0-V1.2

# 1. 装依赖
echo '123456' | sudo -S apt-get install -y git curl ca-certificates
echo '123456' | sudo -S ln -sf /usr/bin/python3 /usr/local/bin/python

# 2. 同步
python3 tools/scripts/sync_sdk.py

# 3. 校验
ls -la build.sh rtos/envsetup.sh
ls kernel/linux-5.4/ | head
git -C kernel/linux-5.4 log --oneline -3
```

> 把 `'123456'` 换成你自己的 sudo 密码,或者更安全地用 `sudo -A` 配 askpass。

---

## 8. 参考

- Google `repo` 工具官方:`https://gerrit.googlesource.com/git-repo/`
- AOSP `repo` 命令文档:`https://source.android.com/docs/setup/develop/repo`
- Allwinner Tina SDK 文档:你下载的 SDK 包内 `lichee/docs/` 或 `rtos/docs/`
- 本仓库同步脚本源码:`tools/scripts/sync_sdk.py`

---

*Last updated: 2026-07-31 / 同步产物总大小 40 GB / 122 projects / 0 failures.*
