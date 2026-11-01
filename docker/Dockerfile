# syntax=docker/dockerfile:1
FROM ubuntu:24.04 AS toolchain
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates build-essential g++-14 cmake ninja-build ccache git pkg-config curl python3 python3-pip jq \
        libsndfile1-dev libfluidsynth-dev liblua5.4-dev libasound2-dev \
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
ARG JOBS=4
WORKDIR /src
COPY --exclude=service --exclude=e2e --exclude=docs --exclude=deploy --exclude=.github --exclude=skills \
    --exclude=.claude-plugin --exclude=*.md . .
RUN --mount=type=cache,target=/ccache \
    cmake --preset release -DEMERICK_BUILD_TESTS=ON \
    && cmake --build --preset release -j "$JOBS" \
    && ctest --test-dir build/release -j "$JOBS" --output-on-failure

# We copy the app with only the Qt libraries and plugins it links, and keep Qt's folder layout so Qt finds its plugins.
FROM build AS bundle
RUN set -e; app=build/release/app; qt=$(readlink -f /opt/qt/current); \
    mkdir -p /out/opt/emerick "/out$qt/lib" "/out$qt/plugins/platforms"; ln -s "$qt" /out/opt/qt/current; \
    cp -a "$app/emerick" "$app/emerick-plugin-scanner" "$app/clap" /out/opt/emerick/; \
    cp "$qt/plugins/platforms/libqoffscreen.so" "/out$qt/plugins/platforms/"; \
    find /out/opt -type f \( -name 'emerick*' -o -name '*.clap' -o -name '*.so' \) -exec strip --strip-unneeded {} +; \
    find /out/opt -type f \( -name 'emerick*' -o -name '*.clap' -o -name '*.so' \) -exec ldd {} + \
        | awk '$3 ~ /^\/opt\/qt\// {print $3}' | sort -u \
        | xargs -I{} cp -L {} "/out$qt/lib/"

FROM ubuntu:24.04 AS service
COPY --from=ghcr.io/astral-sh/uv:0.13.5 /uv /usr/local/bin/uv
RUN apt-get update && apt-get install -y --no-install-recommends python3 && rm -rf /var/lib/apt/lists/*
ENV UV_PYTHON_DOWNLOADS=never UV_PYTHON=/usr/bin/python3 UV_PROJECT_ENVIRONMENT=/opt/service UV_COMPILE_BYTECODE=1
WORKDIR /src/service
COPY service/pyproject.toml service/uv.lock ./
RUN uv sync --frozen --no-dev --no-install-project
COPY service/ ./
RUN uv sync --frozen --no-dev --no-editable

FROM ubuntu:24.04 AS runtime
ENV DEBIAN_FRONTEND=noninteractive
# Qt links the GL dispatch libraries, but headless sessions never create a GL context, so we drop the Mesa drivers.
RUN apt-get update && apt-get install -y --no-install-recommends \
        python3 libsndfile1 libfluidsynth3 libasound2t64 libgl1 libegl1 libopengl0 libxkbcommon0 libfontconfig1 \
        libdbus-1-3 libglib2.0-0t64 fontconfig fonts-dejavu-core \
    && dpkg --purge --force-depends libglx-mesa0 libegl-mesa0 libgl1-mesa-dri mesa-libgallium \
        $(dpkg-query -W -f '${Package}\n' 'libllvm*') \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --uid 10001 --create-home --home-dir /data emerick
COPY --from=bundle /out/ /
COPY --from=service /opt/service /opt/service
COPY docker/entrypoint.sh /usr/local/bin/entrypoint.sh
RUN ! find /opt/emerick /opt/qt -type f \( -name 'emerick*' -o -name '*.clap' -o -name '*.so*' \) -exec ldd {} + \
        | grep 'not found'
ENV PATH=/opt/emerick:/opt/service/bin:$PATH \
    QT_QPA_PLATFORM=offscreen \
    QT_QUICK_BACKEND=software \
    LC_ALL=C.UTF-8 \
    EMERICK_BIN=/opt/emerick/emerick \
    EMERICK_DATA_DIR=/data \
    EMERICK_WORK_DIR=/tmp/emerick-sessions \
    EMERICK_HOST=0.0.0.0 \
    EMERICK_PORT=8080
USER emerick
WORKDIR /data
VOLUME /data
EXPOSE 8080 8888
HEALTHCHECK --interval=30s --timeout=5s --start-period=10s \
    CMD ["python3", "-c", "import urllib.request; urllib.request.urlopen('http://127.0.0.1:8080/health', timeout=4)"]
ENTRYPOINT ["/usr/local/bin/entrypoint.sh"]
