# Use Ubuntu 22.04 as the base image
FROM ubuntu:22.04

# Prevent interactive prompts during package installation
ENV DEBIAN_FRONTEND=noninteractive

# Install build essentials and dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    gcc \
    git \
    liburing-dev \
    && rm -rf /var/lib/apt/lists/*

# Set the working directory
WORKDIR /app

# Copy the entire project
COPY . .

# Create a build directory
RUN mkdir build && cd build && \
    cmake .. && \
    make -j$(nproc)

# Default command: run the CLI
ENTRYPOINT ["./build/lomo_cli"]
