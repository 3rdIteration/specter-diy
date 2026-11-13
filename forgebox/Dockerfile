FROM ubuntu:22.04 AS toolchain
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential gcc-arm-none-eabi binutils-arm-none-eabi libnewlib-arm-none-eabi \
    cmake python3 python3-pip git libsdl2-dev libffi-dev pkg-config ca-certificates \
    && rm -rf /var/lib/apt/lists/*
RUN pip3 install pyelftools pytest
WORKDIR /src
CMD ["python3", "tools/build.py"]
