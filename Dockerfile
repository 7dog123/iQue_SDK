FROM ubuntu:focal

RUN apt-get update

RUN DEBIAN_FRONTEND=noninteractive TZ=US/Central \
    apt-get -y install rpm git make \
    linux-headers-5.4.0-26 linux-headers-generic

WORKDIR /usr/local/src

RUN git clone https://github.com/7dog123/iQue_SDK && \
    cd iQue_SDK && \
   ./install.sh

