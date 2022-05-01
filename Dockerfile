FROM ubuntu:trusty

RUN apt-get update

RUN DEBIAN_FRONTEND=noninteractive TZ=US/Central \
    apt-get -y install rpm git make \
    linux-headers-3.13.0-24-generic linux-headers-generic \
    linux-libc-dev-mips-cross linux-headers-3.13.0-24

WORKDIR /usr/local/src

RUN git clone https://github.com/7dog123/iQue_SDK && \
    cd iQue_SDK && \
    ./install.sh

