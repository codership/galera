# Use a base image with a Linux distribution of your choice
# Default: Debian testing for most recent compilers

ARG base=testing-slim
FROM ${base}

# Install necessary packages to build DEB
RUN apt update && apt upgrade -y && \
    apt install -y \
        g++ clang cmake check libatomic1 \
        libasio-dev \
        libboost-atomic-dev \
        libboost-program-options-dev \
        && \
    apt clean && rm -rf /var/lib/apt/lists/*

# Set up the build script to be executed on docker run
COPY entrypoint.sh /root
WORKDIR /root
ENTRYPOINT /root/entrypoint.sh
