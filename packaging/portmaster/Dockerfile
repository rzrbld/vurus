# PortMaster's glibc 2.31 ARM64 userspace, pinned after local inspection.
FROM ghcr.io/monkeyx-net/portmaster-build-templates/portmaster-builder@sha256:c6783785614cf8cb57e265976c180b6cc087e305f9d709637c5df6b47a90528c
USER root
RUN apt-get update && apt-get install -y --no-install-recommends \
    libc++-18-dev libc++abi-18-dev curl ca-certificates zip \
    && rm -rf /var/lib/apt/lists/*
RUN curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs -o /tmp/rustup.sh \
    && sh /tmp/rustup.sh -y --profile minimal --default-toolchain 1.85.1 \
    && rm /tmp/rustup.sh
ENV PATH="/root/.cargo/bin:${PATH}"
ENV CC=clang-18 CXX=clang++-18
ENV CXXFLAGS="-stdlib=libc++"
ENV LDFLAGS="-stdlib=libc++ -static-libstdc++ -static-libgcc -pthread"
WORKDIR /source
