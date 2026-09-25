# syntax=docker/dockerfile:1.7
ARG BASE_IMAGE
FROM ${BASE_IMAGE} AS build

ARG DEBIAN_SNAPSHOT
ARG NLOHMANN_JSON_VERSION
ARG NLOHMANN_JSON_SHA256

RUN printf '%s\n' \
      "deb [check-valid-until=no] http://snapshot.debian.org/archive/debian/${DEBIAN_SNAPSHOT}/ bookworm main" \
      "deb [check-valid-until=no] http://snapshot.debian.org/archive/debian/${DEBIAN_SNAPSHOT}/ bookworm-updates main" \
      "deb [check-valid-until=no] http://snapshot.debian.org/archive/debian-security/${DEBIAN_SNAPSHOT}/ bookworm-security main" \
      > /etc/apt/sources.list \
    && rm -f /etc/apt/sources.list.d/debian.sources \
    && apt-get -o Acquire::Check-Valid-Until=false update \
    && apt-get install -y --no-install-recommends \
      ca-certificates cmake curl g++ libsoapysdr-dev ninja-build \
    && rm -rf /var/lib/apt/lists/*
RUN curl --fail --location --proto '=https' --tlsv1.2 \
      "https://github.com/nlohmann/json/releases/download/v${NLOHMANN_JSON_VERSION}/json.tar.xz" \
      --output /tmp/nlohmann-json.tar.xz \
    && echo "${NLOHMANN_JSON_SHA256}  /tmp/nlohmann-json.tar.xz" | sha256sum --check --strict \
    && cmake -E tar xf /tmp/nlohmann-json.tar.xz \
    && cmake -S json -B /tmp/nlohmann-build -G Ninja \
      -DJSON_BuildTests=OFF -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX=/opt/deps \
    && cmake --build /tmp/nlohmann-build --target install

WORKDIR /src
COPY CMakeLists.txt ./
COPY apps apps
COPY include include
COPY src src
COPY third_party/vrt_framework/include third_party/vrt_framework/include
RUN cmake -S . -B /tmp/build -G Ninja \
      -DBUILD_TESTING=OFF \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH=/opt/deps \
      -DCMAKE_INSTALL_PREFIX=/opt/containerlab-vrt \
    && cmake --build /tmp/build --parallel \
    && cmake --install /tmp/build \
    && test -x /opt/containerlab-vrt/bin/radio \
    && test -x /opt/containerlab-vrt/bin/processor \
    && test -x /opt/containerlab-vrt/bin/detector \
    && test -x /opt/containerlab-vrt/bin/recorder

FROM ${BASE_IMAGE} AS runtime
ARG DEBIAN_SNAPSHOT
ARG VRT_REVISION
ARG GRAPHX_REFERENCE_REVISION
RUN printf '%s\n' \
      "deb [check-valid-until=no] http://snapshot.debian.org/archive/debian/${DEBIAN_SNAPSHOT}/ bookworm main" \
      "deb [check-valid-until=no] http://snapshot.debian.org/archive/debian/${DEBIAN_SNAPSHOT}/ bookworm-updates main" \
      "deb [check-valid-until=no] http://snapshot.debian.org/archive/debian-security/${DEBIAN_SNAPSHOT}/ bookworm-security main" \
      > /etc/apt/sources.list \
    && rm -f /etc/apt/sources.list.d/debian.sources \
    && apt-get -o Acquire::Check-Valid-Until=false update \
    && apt-get install -y --no-install-recommends iproute2 libsoapysdr0.8 \
    && rm -rf /var/lib/apt/lists/*
COPY --from=build /opt/containerlab-vrt /opt/containerlab-vrt
COPY container/vrt-source.json /usr/share/doc/containerlab-vrt/vrt-source.json
COPY patches/vrt-runtime-progress.patch /usr/share/doc/containerlab-vrt/vrt-runtime-progress.patch
COPY THIRD_PARTY_NOTICES.md /usr/share/doc/containerlab-vrt/THIRD_PARTY_NOTICES.md
COPY third_party/vrt_framework/LICENSE /usr/share/doc/containerlab-vrt/VRT_FRAMEWORK_LICENSE
ENV PATH=/opt/containerlab-vrt/bin:$PATH
LABEL org.opencontainers.image.title="containerlab-vrt standalone SDR applications" \
      org.opencontainers.image.source="https://github.com/rklinkhammer/containerlab-vrt" \
      org.opencontainers.image.licenses="MIT" \
      org.opencontainers.image.revision.vrt="${VRT_REVISION}" \
      org.opencontainers.image.revision.graphx-reference="${GRAPHX_REFERENCE_REVISION}"
WORKDIR /run/containerlab-vrt
CMD ["/bin/false"]
