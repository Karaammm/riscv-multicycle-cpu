FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install --no-install-recommends -y \
        binutils-riscv64-linux-gnu \
        build-essential \
        ca-certificates \
        gcc-riscv64-linux-gnu \
        libcppdap-dev \
        libdw-dev \
        libelf-dev \
        libjsoncpp-dev \
        make \
        verilator \
    && mkdir -p /opt/riscv/bin \
    && ln -s /usr/bin/riscv64-linux-gnu-gcc /opt/riscv/bin/riscv64-unknown-linux-gnu-gcc \
    && ln -s /usr/bin/riscv64-linux-gnu-objcopy /opt/riscv/bin/riscv64-unknown-linux-gnu-objcopy \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace
ENTRYPOINT ["make"]
