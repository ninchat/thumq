#!/bin/sh

uid=`id -u`
name=`whoami`

make clean

docker run -i --rm -v $PWD:$PWD -w $PWD debian sh -c "apt-get update && apt-get -y upgrade && apt-get -y --no-install-recommends install g++ libgraphicsmagick++-dev libprotobuf-dev libzmq3-dev make pkg-config protobuf-compiler sudo && adduser --uid $uid --disabled-password --gecos $name --quiet $name && sudo --set-home -u $name make"

docker build -t thumq .
