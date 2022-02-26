#!/usr/bin/env bash

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd $DIR/..

DOCKER_IMAGE=${DOCKER_IMAGE:-AdvancedTechnologyCoin/arc_cored-develop}
DOCKER_TAG=${DOCKER_TAG:-latest}

BUILD_DIR=${BUILD_DIR:-.}

rm docker/bin/*
mkdir docker/bin
cp $BUILD_DIR/src/arcd docker/bin/
cp $BUILD_DIR/src/arc-cli docker/bin/
cp $BUILD_DIR/src/arc-tx docker/bin/
strip docker/bin/arcd
strip docker/bin/arc-cli
strip docker/bin/arc-tx

docker build --pull -t $DOCKER_IMAGE:$DOCKER_TAG -f docker/Dockerfile docker
