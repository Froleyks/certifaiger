FROM alpine:3.22 AS builder
RUN apk add --no-cache \
      bash coreutils \
      build-base cmake \
      git ca-certificates

WORKDIR /work
COPY . .
RUN rm -rf build bin
RUN make

FROM alpine:3.22
RUN apk add --no-cache bash
COPY --from=builder /work/bin/* /usr/local/bin
ENTRYPOINT ["check"]
