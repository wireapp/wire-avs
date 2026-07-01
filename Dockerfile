FROM ubuntu:22.04
ARG DEBIAN_FRONTEND=noninteractive

RUN mkdir -p /build/avs
WORKDIR /build/avs
ENV HOME /build/avs

COPY scripts/ubuntu_20.04_dependencies.sh /build/avs

RUN /build/avs/ubuntu_20.04_dependencies.sh

# Needed to workaround JENKINS-38438
RUN chown -R 1015:1015 /build/avs

ENV PATH="/usr/share/cargo/bin:/build/avs/.cargo/bin:${PATH}"

CMD make DIST=1 && build/linux-x86_64/bin/ztest && build/linux-x86_64/bin/ztest-slow && cp -R build/* /out/

