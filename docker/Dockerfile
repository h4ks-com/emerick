# syntax=docker/dockerfile:1
FROM ubuntu:24.04 AS toolchain
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates build-essential g++-14 cmake ninja-build ccache git pkg-config curl python3 python3-pip jq \
        libsndfile1-dev liblua5.4-dev libasound2-dev \
        libgl1-mesa-dev libgl1-mesa-dri libegl1 libglx-mesa0 libxkbcommon0 libfontconfig1 libdbus-1-3 libglib2.0-0t64 \
        fontconfig fonts-dejavu-core fonts-inter \
    && rm -rf /var/lib/apt/lists/*
ENV CC=gcc-14 CXX=g++-14

FROM toolchain AS qt
ARG TARGETARCH
ARG QT_VERSION=6.11.3
RUN pip install --break-system-packages --no-cache-dir aqtinstall==3.3.0
RUN if [ "$TARGETARCH" = "arm64" ]; then HOST=linux_arm64; ARCH=linux_gcc_arm64; else HOST=linux; ARCH=linux_gcc_64; fi \
    && aqt install-qt "$HOST" desktop "$QT_VERSION" "$ARCH" -m qthttpserver qtwebsockets --outputdir /opt/qt \
    && ln -s /opt/qt/"$QT_VERSION"/gcc_* /opt/qt/current
ENV CMAKE_PREFIX_PATH=/opt/qt/current \
    PATH=/opt/qt/current/bin:$PATH \
    QT_QPA_PLATFORM=offscreen \
    QT_QUICK_BACKEND=software \
    CCACHE_DIR=/ccache

FROM qt AS build
WORKDIR /src
COPY . .
RUN --mount=type=cache,target=/ccache \
    cmake --preset dev && cmake --build --preset dev && ctest --preset dev

FROM build AS runtime
COPY docker/entrypoint.sh /usr/local/bin/entrypoint.sh
EXPOSE 7777
ENTRYPOINT ["/usr/local/bin/entrypoint.sh"]
