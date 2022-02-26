FROM debian:stretch
LABEL maintainer="Arc Developers <dev@arc.org>"
LABEL description="Dockerised arc, built from Travis"

RUN apt-get update && apt-get -y upgrade && apt-get clean && rm -fr /var/cache/apt/*

COPY bin/* /usr/bin/
