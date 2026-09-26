#include <signal.h>
#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/server/TThreadedServer.h>
#include <thrift/transport/TBufferTransports.h>
#include <thrift/transport/TServerSocket.h>

#include "../utils.h"
#include "../utils_thrift.h"
#include "TextUrlHandler.h"

using apache::thrift::protocol::TBinaryProtocolFactory;
using apache::thrift::server::TThreadedServer;
using apache::thrift::transport::TFramedTransportFactory;
using apache::thrift::transport::TServerSocket;
using namespace social_network;

void sigintHandler(int sig) { exit(EXIT_SUCCESS); }

int main(int argc, char *argv[]) {
  signal(SIGINT, sigintHandler);
  init_logger();
  SetUpTracer("config/jaeger-config.yml", "text-url-service");

  json config_json;
  if (load_config_file("config/service-config.json", &config_json) == 0) {
    int port = config_json["text-url-service"]["port"];

    int url_port = config_json["url-shorten-service"]["port"];
    std::string url_addr = config_json["url-shorten-service"]["addr"];
    int url_conns = config_json["url-shorten-service"]["connections"];
    int url_timeout = config_json["url-shorten-service"]["timeout_ms"];
    int url_keepalive = config_json["url-shorten-service"]["keepalive_ms"];

    ClientPool<ThriftClient<UrlShortenServiceClient>> url_client_pool(
        "url-shorten-service-client", url_addr, url_port, 0, url_conns,
        url_timeout, url_keepalive, config_json);

    std::shared_ptr<TServerSocket> server_socket =
        get_server_socket(config_json, "0.0.0.0", port);
    TThreadedServer server(
        std::make_shared<TextUrlServiceProcessor>(
            std::make_shared<TextUrlHandler>(&url_client_pool)),
        server_socket, std::make_shared<TFramedTransportFactory>(),
        std::make_shared<TBinaryProtocolFactory>());

    LOG(info) << "Starting the text-url-service server...";
    server.serve();
  } else {
    exit(EXIT_FAILURE);
  }
}
