FROM debian

COPY thumq /usr/local/bin/

RUN apt-get update && apt-get -y upgrade && apt-get -y --no-install-recommends install libgraphicsmagick++3 libprotobuf9 libzmq3

USER daemon

ENTRYPOINT ["thumq"]
