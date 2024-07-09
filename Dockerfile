FROM ubuntu:24.04

RUN \
  apt-get update && \
  apt-get install -y --no-install-recommends build-essential cmake git zlib1g-dev && \
  apt-get clean

COPY . /certifaiger
WORKDIR /certifaiger
# quabs clones CMS during build and there is a problem with the certificate
ENV GIT_SSL_NO_VERIFY=true
RUN \
  cmake -DGIT=OFF -DQBF=ON -DCMAKE_BUILD_TYPE=Release -B build && \
  make -C build -j$(nproc) --no-print-directory

ENTRYPOINT ["/certifaiger/build/check"]
