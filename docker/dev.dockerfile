FROM ubuntu:22.04

ARG DEBIAN_FRONTEND=noninteractive
ENV TZ=Asia/Shanghai

SHELL ["/bin/bash", "-c"]

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    gcc-11-plugin-dev \
    vim \
    net-tools \
    && rm -rf /var/lib/apt/lists/*

    

