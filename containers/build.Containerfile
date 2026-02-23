# ---------------------------------------------------------------------------
# Stage 1: Bootstrap -- use system Python 3.12 to install uv and Python 3.14
# ---------------------------------------------------------------------------
FROM docker.io/library/ubuntu:24.04 AS bootstrap

ENV DEBIAN_FRONTEND=noninteractive
ENV DEBCONF_NOWARNINGS=yes

RUN apt-get update && apt-get install -y --no-install-recommends \
    python3 python3-venv \
    && rm -rf /var/lib/apt/lists/*

RUN python3 -m venv /tmp/bootstrap
RUN /tmp/bootstrap/bin/pip install --upgrade pip uv
RUN /tmp/bootstrap/bin/uv python install 3.14

# ---------------------------------------------------------------------------
# Stage 2: Final -- clang-20, ceedling, cross-compilation sysroots
# ---------------------------------------------------------------------------
FROM docker.io/library/ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive
ENV DEBCONF_NOWARNINGS=yes

# ---- LLVM 20 repository ----
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates gnupg software-properties-common wget \
    && rm -rf /var/lib/apt/lists/*

RUN wget -qO /etc/apt/trusted.gpg.d/llvm.asc \
    https://apt.llvm.org/llvm-snapshot.gpg.key

RUN add-apt-repository -y \
    "deb http://apt.llvm.org/noble/ llvm-toolchain-noble-20 main"

# ---- All apt packages ----
# Clang is a native cross-compiler; only the target sysroots (libc6-dev-*-cross)
# are needed -- the gcc-*-linux-gnu compiler packages are not required.
RUN apt-get update && apt-get install -y --no-install-recommends \
    clang-20 lld-20 llvm-20 \
    gcc-14 libc6-dev make xz-utils \
    git ruby ruby-dev \
    libc6-dev-armhf-cross \
    libc6-dev-arm64-cross \
    libc6-dev-amd64-cross \
    libc6-dev-mips-cross \
    libc6-dev-mips64-cross \
    libc6-dev-riscv64-cross \
    libc6-dev-powerpc-cross \
    && rm -rf /var/lib/apt/lists/*

# ---- Register clang-20 as default ----
RUN update-alternatives --install /usr/bin/clang   clang   /usr/bin/clang-20   100
RUN update-alternatives --install /usr/bin/clang++ clang++ /usr/bin/clang++-20 100
RUN update-alternatives --install /usr/bin/lld     lld     /usr/bin/lld-20     100
RUN update-alternatives --install /usr/bin/ld.lld  ld.lld  /usr/bin/ld.lld-20  100
RUN update-alternatives --install /usr/bin/llvm-ar llvm-ar /usr/bin/llvm-ar-20 100
RUN update-alternatives --install /usr/bin/llvm-ranlib llvm-ranlib /usr/bin/llvm-ranlib-20 100

# ---- Register gcc-14 as default ----
RUN update-alternatives --install /usr/bin/gcc  gcc  /usr/bin/gcc-14  100
RUN update-alternatives --install /usr/bin/gcov gcov /usr/bin/gcov-14 100

# ---- Python 3.14 via uv ----
COPY --from=bootstrap /tmp/bootstrap/bin/uv /usr/local/bin/uv
COPY --from=bootstrap /root/.local/share/uv/python/ /opt/python/

RUN ln -s "$(find /opt/python -name 'python3.14' -type f | head -1)" /usr/local/bin/python3.14
RUN ln -s python3.14 /usr/local/bin/python3

# ---- Ceedling (Ruby test framework) ----
RUN gem install specific_install
RUN gem specific_install -l https://github.com/ThrowTheSwitch/Ceedling.git

# ---- FreeBSD cross-compilation sysroots (14.3-RELEASE) ----
RUN mkdir -p /opt/sysroot/freebsd-amd64 /opt/sysroot/freebsd-arm64

RUN wget -qO /tmp/fb-amd64.txz \
    https://download.freebsd.org/releases/amd64/14.3-RELEASE/base.txz \
    && tar -xJf /tmp/fb-amd64.txz -C /opt/sysroot/freebsd-amd64 \
        ./usr/include ./usr/lib ./lib \
    && rm /tmp/fb-amd64.txz

RUN wget -qO /tmp/fb-arm64.txz \
    https://download.freebsd.org/releases/arm64/aarch64/14.3-RELEASE/base.txz \
    && tar -xJf /tmp/fb-arm64.txz -C /opt/sysroot/freebsd-arm64 \
        ./usr/include ./usr/lib ./lib \
    && rm /tmp/fb-arm64.txz
