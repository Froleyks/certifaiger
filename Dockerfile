FROM ubuntu:24.04

RUN \
  apt-get update && \
  apt-get install -y --no-install-recommends build-essential cmake && \
  apt-get clean

COPY . /certifaiger
WORKDIR /certifaiger
RUN \
  cmake -DGIT=OFF -DCMAKE_BUILD_TYPE=Release -B build && \
  make -C build -j$(nproc) --no-print-directory

ENTRYPOINT ["/certifaiger/build/check"]
