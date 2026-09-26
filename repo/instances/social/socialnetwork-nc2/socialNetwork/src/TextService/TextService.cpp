#include <signal.h>
#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/server/TThreadedServer.h>
#include <thrift/transport/TBufferTransports.h>
#include <thrift/transport/TServerSocket.h>

#include "../utils.h"
#include "../utils_thrift.h"
#include "TextHandler.h"

using apache::thrift::protocol::TBinaryProtocolFactory;
using apache::thrift::server::TThreadedServer;
using apache::thrift::transport::TFramedTransportFactory;
using apache::thrift::transport::TServerSocket;
using namespace social_network;

void sigintHandler(int sig) { exit(EXIT_SUCCESS); }

int main(int argc, char *argv[]) {
  signal(SIGINT, sigintHandler);
  init_logger();
  SetUpTracer("config/jaeger-config.yml", "text-service");

  json config_json;
  if (load_config_file("config/service-config.json", &config_json) == 0) {
    int port = config_json["text-service"]["port"];

    std::string text_mention_addr = config_json["text-mention-service"]["addr"];
    int text_mention_port = config_json["text-mention-service"]["port"];
    int text_mention_conns = config_json["text-mention-service"]["connections"];
    int text_mention_timeout = config_json["text-mention-service"]["timeout_ms"];
    int text_mention_keepalive =
        config_json["text-mention-service"]["keepalive_ms"];

    std::string text_url_addr = config_json["text-url-service"]["addr"];
    int text_url_port = config_json["text-url-service"]["port"];
    int text_url_conns = config_json["text-url-service"]["connections"];
    int text_url_timeout = config_json["text-url-service"]["timeout_ms"];
    int text_url_keepalive = config_json["text-url-service"]["keepalive_ms"];

    ClientPool<ThriftClient<TextMentionServiceClient>> text_mention_pool(
        "text-mention-service", text_mention_addr, text_mention_port, 0,
        text_mention_conns, text_mention_timeout, text_mention_keepalive,
        config_json);

    ClientPool<ThriftClient<TextUrlServiceClient>> text_url_pool(
        "text-url-service", text_url_addr, text_url_port, 0, text_url_conns,
        text_url_timeout, text_url_keepalive, config_json);

    std::shared_ptr<TServerSocket> server_socket = get_server_socket(config_json, "0.0.0.0", port);
    TThreadedServer server(
        std::make_shared<TextServiceProcessor>(std::make_shared<TextHandler>(
            &text_mention_pool, &text_url_pool)),
        server_socket,
        std::make_shared<TFramedTransportFactory>(),
        std::make_shared<TBinaryProtocolFactory>());

    LOG(info) << "Starting the text-service server...";
    server.serve();
  } else
    exit(EXIT_FAILURE);
}
