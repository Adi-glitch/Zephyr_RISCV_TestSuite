FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update -qq && \
    apt-get install -y -qq \
    wget tar make device-tree-compiler git g++ \
    libmpc3 libmpfr6 libgmp10 python3 && \
    rm -rf /var/lib/apt/lists/*

ENV RISCV_DIR=/opt/riscv
RUN mkdir -p $RISCV_DIR

RUN wget -q https://github.com/riscv-collab/riscv-gnu-toolchain/releases/download/2024.09.03/riscv64-elf-ubuntu-22.04-gcc-nightly-2024.09.03-nightly.tar.gz -O /tmp/tc.tar.gz && \
    tar -xzf /tmp/tc.tar.gz -C $RISCV_DIR --strip-components=1 && \
    rm /tmp/tc.tar.gz

RUN git clone --depth 1 https://github.com/riscv-software-src/riscv-isa-sim.git /tmp/riscv-isa-sim && \
    cd /tmp/riscv-isa-sim && \
    mkdir build && cd build && \
    ../configure --prefix=$RISCV_DIR && \
    make -j$(nproc) && \
    make install && \
    rm -rf /tmp/riscv-isa-sim

ENV PATH="$RISCV_DIR/bin:$PATH"
ENV LD_LIBRARY_PATH="$RISCV_DIR/lib:$LD_LIBRARY_PATH"
