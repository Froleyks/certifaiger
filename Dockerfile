FROM ubuntu:24.04

RUN \
  apt-get update && \
    apt-get install -y --no-install-recommends \
    build-essential cmake git && \
  apt-get clean

COPY . /certifaiger
WORKDIR /certifaiger
ENV GIT_SSL_NO_VERIFY=true
RUN make

ENTRYPOINT ["/certifaiger/build/check"]
