# SPDX-License-Identifier: GPL-2.0-only
# Build environment for the portable AppImage (tools/appimage/build_portable.sh): Ubuntu 22.04
# (glibc 2.35), so the AppImage runs on it and on every newer distribution. What 22.04 packages
# too old for the port (GCC 14 for C++23, CMake, Vulkan headers, glslang, SDL3, fmt, Zydis,
# robin-map, FFmpeg) is built from pinned sources into /usr/local.
FROM ubuntu:22.04
ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends software-properties-common gpg-agent \
    && add-apt-repository -y ppa:ubuntu-toolchain-r/test && apt-get update \
    && apt-get install -y --no-install-recommends \
        gcc-14 g++-14 make ninja-build pkg-config git curl ca-certificates file binutils nasm \
        python3 python3-pip python3-gi python3-gi-cairo python3-cairo \
        gir1.2-gtk-4.0 gir1.2-adw-1 libgtk-4-1 libadwaita-1-0 libglib2.0-bin \
        libvulkan-dev zlib1g-dev libx11-dev libboost-dev libxxhash-dev libssl-dev \
        libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxss-dev libxfixes-dev libxtst-dev \
        libxkbcommon-dev libwayland-dev wayland-protocols libdecor-0-dev libegl-dev \
        libpulse-dev libasound2-dev libpipewire-0.3-dev libudev-dev libdbus-1-dev \
    && rm -rf /var/lib/apt/lists/* \
    && update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-14 100 \
        --slave /usr/bin/g++ g++ /usr/bin/g++-14 \
    && update-alternatives --install /usr/bin/cc cc /usr/bin/gcc-14 100 \
    && update-alternatives --install /usr/bin/c++ c++ /usr/bin/g++-14 100 \
    && pip3 install --no-cache-dir cmake==3.31.6

WORKDIR /deps
ARG VULKAN_SDK=vulkan-sdk-1.4.363.0
RUN git clone --depth 1 --branch $VULKAN_SDK https://github.com/KhronosGroup/Vulkan-Headers.git \
    && cmake -S Vulkan-Headers -B Vulkan-Headers/build -G Ninja && cmake --install Vulkan-Headers/build \
    && git clone --depth 1 --branch $VULKAN_SDK https://github.com/KhronosGroup/glslang.git \
    && cd glslang && python3 update_glslang_sources.py && cd .. \
    && cmake -S glslang -B glslang/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DENABLE_OPT=ON \
        -DGLSLANG_TESTS=OFF -DSPIRV_SKIP_TESTS=ON -DSPIRV_SKIP_EXECUTABLES=ON \
    && ninja -C glslang/build && cmake --install glslang/build && rm -rf /deps/*
RUN git clone --depth 1 --branch release-3.4.20 https://github.com/libsdl-org/SDL.git \
    && cmake -S SDL -B SDL/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF \
    && ninja -C SDL/build && cmake --install SDL/build && rm -rf /deps/*
RUN git clone --depth 1 --branch 12.2.1 https://github.com/fmtlib/fmt.git \
    && cmake -S fmt -B fmt/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DFMT_TEST=OFF -DFMT_DOC=OFF \
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    && ninja -C fmt/build && cmake --install fmt/build \
    && git clone --depth 1 --branch v4.1.1 --recursive https://github.com/zyantific/zydis.git \
    && cmake -S zydis/dependencies/zycore -B zycore-build -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    && ninja -C zycore-build && cmake --install zycore-build \
    && cmake -S zydis -B zydis/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DZYAN_SYSTEM_ZYCORE=ON -DZYDIS_BUILD_TOOLS=OFF \
        -DZYDIS_BUILD_EXAMPLES=OFF -DZYDIS_BUILD_DOXYGEN=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    && ninja -C zydis/build && cmake --install zydis/build \
    && git clone --depth 1 --branch v1.4.1 https://github.com/Tessil/robin-map.git \
    && cmake -S robin-map -B robin-map/build -G Ninja && cmake --install robin-map/build && rm -rf /deps/*
RUN git clone --depth 1 --branch n8.1.3 https://github.com/FFmpeg/FFmpeg.git ffmpeg && cd ffmpeg \
    && ./configure --prefix=/usr/local --enable-shared --disable-static --disable-programs --disable-doc \
        --disable-autodetect --disable-network --disable-avdevice --disable-avfilter \
    && make -j"$(nproc)" && make install && cd / && rm -rf /deps/* && ldconfig
# Boost 1.74's static_vector does not convert to std::span (Sirit needs it): newer headers instead.
RUN apt-get purge -y libboost-dev libboost1.74-dev && apt-get autoremove -y \
    && curl -fsSL https://archives.boost.io/release/1.86.0/source/boost_1_86_0.tar.gz -o boost.tar.gz \
    && echo "2575e74ffc3ef1cd0babac2c1ee8bdb5782a0ee672b1912da40e5b4b591ca01f  boost.tar.gz" | sha256sum -c \
    && tar -xzf boost.tar.gz boost_1_86_0/boost && mv boost_1_86_0/boost /usr/local/include/ \
    && rm -rf /deps/*
# appimagetool validates the desktop entry and stops without this.
RUN apt-get update && apt-get install -y --no-install-recommends desktop-file-utils && rm -rf /var/lib/apt/lists/*
