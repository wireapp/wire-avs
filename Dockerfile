FROM ubuntu:20.04
ARG DEBIAN_FRONTEND=noninteractive

COPY . /build/avs
WORKDIR /build/avs

RUN /build/avs/scripts/ubuntu_20.04_dependencies.sh

CMD make DIST=1 && build/linux-x86_64/bin/ztest && cp -R build/* /out/

