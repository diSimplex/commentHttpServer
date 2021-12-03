# Build step....
#
FROM alpine AS build
RUN apk add --no-cache --update bash make git curl gcc musl-dev

COPY . /chs
WORKDIR /chs
RUN make

# Final image...
#
FROM alpine

RUN mkdir -p /chs /comments

COPY --from=build /chs/commentHttpServer /chs

WORKDIR /chs

ENTRYPOINT ["/chs/commentHttpServer", "/comments", "/logs"]
CMD ["9090", "9091", "9092", "9093", "9094", "9095", "9096", "9097", "9098", "9099"]
