# See here for image contents: https://github.com/microsoft/vscode-dev-containers/tree/v0.234.0/containers/debian/.devcontainer/base.Dockerfile

# [Choice] Debian version: bookworm (required for arm64/Apple Silicon — buster has no arm64 manifest, bullseye apt mirrors 404 on the riscv toolchain).
# Note: the legacy `vscode/devcontainers/base` namespace was deprecated; use the current `devcontainers/base` images which publish arm64 manifests.
ARG VARIANT="bookworm"
FROM mcr.microsoft.com/devcontainers/base:${VARIANT}

# Install the xv6/riscv toolchain in a single clean apt layer.
RUN apt-get update \
    && DEBIAN_FRONTEND=noninteractive apt-get -y install --no-install-recommends \
        git \
        build-essential \
        gdb-multiarch \
        qemu-system-misc \
        gcc-riscv64-linux-gnu \
        binutils-riscv64-linux-gnu \
        novnc \
        websockify \
    && rm -rf /var/lib/apt/lists/*
# enable loading custom .gdbinit per folder.
# RUN echo set auto-load safe-path / > /root/.gdbinit
RUN echo set architecture riscv:rv64 > /root/.gdbinit