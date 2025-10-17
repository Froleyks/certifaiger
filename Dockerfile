FROM debian:trixie-slim
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && \
    apt-get install -y --no-install-recommends \
      bash ca-certificates git \
      build-essential cmake

WORKDIR /certifaiger
COPY . .
RUN make
