FROM golang:1.24

WORKDIR /go/src/app

COPY *.go .

RUN go mod init dispatch && go mod tidy
RUN go build -o /usr/local/bin/dispatch .

CMD ["dispatch"]
