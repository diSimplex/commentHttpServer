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

ENTRYPOINT ["/chs/commentHttpServer", "/comments", "8080", "10"]
