FROM ubuntu:latest

RUN apt-get update && \
    apt-get -y install rpm 

WORKDIR /usr/local/src

RUN git clone https://github.com/7dog123/iQue_SDK && \
    cd iQue_SDK && \
   ./install.sh

