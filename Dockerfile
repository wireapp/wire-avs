FROM ubuntu:24.04
ARG DEBIAN_FRONTEND=noninteractive

RUN mkdir -p /build/avs
WORKDIR /build/avs
ENV HOME /build/avs

COPY scripts/ubuntu_20.04_dependencies.sh /build/avs

RUN /build/avs/ubuntu_20.04_dependencies.sh

# Needed to workaround JENKINS-38438
RUN chown -R 1015:1015 /build/avs

ENV PATH="/usr/share/cargo/bin:/build/avs/.cargo/bin:${PATH}"
