FROM alpine:3.22 AS builder
RUN apk add --no-cache \
      bash coreutils \
      build-base cmake \
      boost-dev boost-static \
      clang lld \
      git ca-certificates \
      linux-headers
RUN git config --global url."https://github.com/".insteadOf git@github.com:

WORKDIR /work
COPY . .
RUN rm -rf build bin
RUN make lrat_isa

FROM alpine:3.22
RUN apk add --no-cache bash
COPY --from=builder /work/bin/* /usr/local/bin/
ENTRYPOINT ["check"]
