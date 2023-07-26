FROM ubuntu:20.04
ARG DEBIAN_FRONTEND=noninteractive

RUN mkdir -p /build/avs
WORKDIR /build/avs
ENV HOME /build/avs

COPY scripts/ubuntu_20.04_dependencies.sh /build/avs

RUN /build/avs/ubuntu_20.04_dependencies.sh

# Needed to workaround JENKINS-38438
RUN chown -R 1001:1001 /build/avs

ENV PATH="/build/avs/.cargo/bin:${PATH}"

CMD make DIST=1 && build/linux-x86_64/bin/ztest && cp -R build/* /out/

