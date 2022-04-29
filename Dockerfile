FROM ubuntu:impish

RUN apt-get update

RUN DEBIAN_FRONTEND=noninteractive TZ=US/Central \
    apt-get -y install rpm git make \
    linux-headers-5.13.0-19 linux-headers-generic \
    linux-libc-dev-mips-cross linux-headers-5.13.0-19-generic

WORKDIR /usr/local/src

RUN git clone https://github.com/7dog123/iQue_SDK && \
    cd iQue_SDK && \
    ./install.sh

