#include <signal.h>

#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/server/TThreadedServer.h>
#include <thrift/transport/TBufferTransports.h>
#include <thrift/transport/TServerSocket.h>

#include <mutex>
#include <thread>

#include "../../gen-cpp/ComposePostService.h"
#include "../../gen-cpp/HomeTimelineService.h"
#include "../../gen-cpp/MediaService.h"
#include "../../gen-cpp/PostStorageService.h"
#include "../../gen-cpp/SocialGraphService.h"
#include "../../gen-cpp/TextService.h"
#include "../../gen-cpp/UniqueIdService.h"
#include "../../gen-cpp/UrlShortenService.h"
#include "../../gen-cpp/UserMentionService.h"
#include "../../gen-cpp/UserService.h"
#include "../../gen-cpp/UserTimelineService.h"

#include "../ClientPool.h"
#include "../ComposePostService/ComposePostHandler.h"
#include "../HomeTimelineService/HomeTimelineHandler.h"
#include "../PostStorageService/PostStorageHandler.h"
#include "../TextService/TextHandler.h"
#include "../UrlShortenService/UrlShortenHandler.h"
#include "../UserMentionService/UserMentionHandler.h"
#include "../UserTimelineService/UserTimelineHandler.h"
#include "../logger.h"
#include "../tracing.h"
#include "../utils.h"
#include "../utils_memcached.h"
#include "../utils_mongodb.h"
#include "../utils_redis.h"
#include "../utils_thrift.h"

using apache::thrift::protocol::TBinaryProtocolFactory;
using apache::thrift::server::TThreadedServer;
using apache::thrift::transport::TFramedTransportFactory;
using apache::thrift::transport::TServerSocket;
using namespace social_network;

void sigintHandler(int sig) { exit(EXIT_SUCCESS); }

static bool CreateMongoIndex(mongoc_client_pool_t *pool, const char *db,
                             const char *collection, const char *field) {
  mongoc_client_t *client = mongoc_client_pool_pop(pool);
  if (!client) {
    LOG(fatal) << "Failed to pop mongoc client for index creation";
    return false;
  }
  bool ok = false;
  while (!ok) {
    ok = CreateIndex(client, collection, field, true);
    if (!ok) {
      LOG(error) << "Failed to create index on " << collection << "." << field
                 << ", retrying...";
      sleep(1);
    }
  }
  mongoc_client_pool_push(pool, client);
  return true;
}

int main(int argc, char *argv[]) {
  signal(SIGINT, sigintHandler);
  init_logger();
  SetUpTracer("config/jaeger-config.yml", "m-service");

  json config_json;
  if (load_config_file("config/service-config.json", &config_json) != 0) {
    exit(EXIT_FAILURE);
  }

  // Ports for all merged services (must be distinct within the same container).
  int home_timeline_port = config_json["home-timeline-service"]["port"];
  int user_timeline_port = config_json["user-timeline-service"]["port"];
  int post_storage_port = config_json["post-storage-service"]["port"];
  int text_service_port = config_json["text-service"]["port"];
  int url_shorten_port = config_json["url-shorten-service"]["port"];
  int user_mention_port = config_json["user-mention-service"]["port"];
  int compose_post_port = config_json["compose-post-service"]["port"];

  // Redis client pools for home-timeline and user-timeline.
  Redis home_redis_client_pool = init_redis_client_pool(config_json, "home-timeline");
  Redis user_redis_client_pool = init_redis_client_pool(config_json, "user-timeline");

  // MongoDB client pools.
  int user_timeline_mongodb_conns =
      config_json["user-timeline-mongodb"]["connections"];
  int post_storage_mongodb_conns =
      config_json["post-storage-mongodb"]["connections"];
  int url_shorten_mongodb_conns =
      config_json["url-shorten-mongodb"]["connections"];
  int user_mongodb_conns = config_json["user-mongodb"]["connections"];

  mongoc_client_pool_t *user_timeline_mongodb_client_pool =
      init_mongodb_client_pool(config_json, "user-timeline",
                               static_cast<uint32_t>(user_timeline_mongodb_conns));
  mongoc_client_pool_t *post_storage_mongodb_client_pool =
      init_mongodb_client_pool(config_json, "post-storage",
                               static_cast<uint32_t>(post_storage_mongodb_conns));
  mongoc_client_pool_t *url_shorten_mongodb_client_pool =
      init_mongodb_client_pool(config_json, "url-shorten",
                               static_cast<uint32_t>(url_shorten_mongodb_conns));
  mongoc_client_pool_t *user_mongodb_client_pool =
      init_mongodb_client_pool(config_json, "user",
                               static_cast<uint32_t>(user_mongodb_conns));

  if (user_timeline_mongodb_client_pool == nullptr ||
      post_storage_mongodb_client_pool == nullptr ||
      url_shorten_mongodb_client_pool == nullptr ||
      user_mongodb_client_pool == nullptr) {
    return EXIT_FAILURE;
  }

  // Memcached client pools.
  int post_storage_memcached_conns =
      config_json["post-storage-memcached"]["connections"];
  int url_shorten_memcached_conns =
      config_json["url-shorten-memcached"]["connections"];
  int user_memcached_conns = config_json["user-memcached"]["connections"];

  memcached_pool_st *post_storage_memcached_client_pool =
      init_memcached_client_pool(config_json, "post-storage", 32,
                                 static_cast<uint32_t>(post_storage_memcached_conns));
  memcached_pool_st *url_shorten_memcached_client_pool =
      init_memcached_client_pool(config_json, "url-shorten", 32,
                                 static_cast<uint32_t>(url_shorten_memcached_conns));
  memcached_pool_st *user_memcached_client_pool =
      init_memcached_client_pool(config_json, "user", 32,
                                 static_cast<uint32_t>(user_memcached_conns));

  if (post_storage_memcached_client_pool == nullptr ||
      url_shorten_memcached_client_pool == nullptr ||
      user_memcached_client_pool == nullptr) {
    return EXIT_FAILURE;
  }

  // Create required MongoDB indexes.
  if (!CreateMongoIndex(user_timeline_mongodb_client_pool, "user-timeline",
                        "user-timeline", "user_id")) {
    return EXIT_FAILURE;
  }
  if (!CreateMongoIndex(post_storage_mongodb_client_pool, "post", "post",
                        "post_id")) {
    return EXIT_FAILURE;
  }
  if (!CreateMongoIndex(url_shorten_mongodb_client_pool, "url-shorten",
                        "url-shorten", "shortened_url")) {
    return EXIT_FAILURE;
  }

  // Downstream Thrift client pools used by the merged services.
  auto make_thrift_pool = [&](const std::string &service_name) {
    // Helper is not used directly because of template type differences.
  };

  std::string post_storage_addr = config_json["post-storage-service"]["addr"];
  int post_storage_conns = config_json["post-storage-service"]["connections"];
  int post_storage_timeout = config_json["post-storage-service"]["timeout_ms"];
  int post_storage_keepalive =
      config_json["post-storage-service"]["keepalive_ms"];

  std::string social_graph_addr = config_json["social-graph-service"]["addr"];
  int social_graph_port = config_json["social-graph-service"]["port"];
  int social_graph_conns = config_json["social-graph-service"]["connections"];
  int social_graph_timeout = config_json["social-graph-service"]["timeout_ms"];
  int social_graph_keepalive =
      config_json["social-graph-service"]["keepalive_ms"];

  std::string user_service_addr = config_json["user-service"]["addr"];
  int user_service_port = config_json["user-service"]["port"];
  int user_service_conns = config_json["user-service"]["connections"];
  int user_service_timeout = config_json["user-service"]["timeout_ms"];
  int user_service_keepalive = config_json["user-service"]["keepalive_ms"];

  std::string unique_id_addr = config_json["unique-id-service"]["addr"];
  int unique_id_port = config_json["unique-id-service"]["port"];
  int unique_id_conns = config_json["unique-id-service"]["connections"];
  int unique_id_timeout = config_json["unique-id-service"]["timeout_ms"];
  int unique_id_keepalive = config_json["unique-id-service"]["keepalive_ms"];

  std::string media_service_addr = config_json["media-service"]["addr"];
  int media_service_port = config_json["media-service"]["port"];
  int media_service_conns = config_json["media-service"]["connections"];
  int media_service_timeout = config_json["media-service"]["timeout_ms"];
  int media_service_keepalive = config_json["media-service"]["keepalive_ms"];

  ClientPool<ThriftClient<PostStorageServiceClient>> post_storage_client_pool(
      "post-storage-client", post_storage_addr, post_storage_port, 0,
      post_storage_conns, post_storage_timeout, post_storage_keepalive,
      config_json);

  ClientPool<ThriftClient<SocialGraphServiceClient>> social_graph_client_pool(
      "social-graph-client", social_graph_addr, social_graph_port, 0,
      social_graph_conns, social_graph_timeout, social_graph_keepalive,
      config_json);

  ClientPool<ThriftClient<UserServiceClient>> user_service_client_pool(
      "user-service-client", user_service_addr, user_service_port, 0,
      user_service_conns, user_service_timeout, user_service_keepalive,
      config_json);

  ClientPool<ThriftClient<UniqueIdServiceClient>> unique_id_client_pool(
      "unique-id-client", unique_id_addr, unique_id_port, 0,
      unique_id_conns, unique_id_timeout, unique_id_keepalive, config_json);

  ClientPool<ThriftClient<MediaServiceClient>> media_service_client_pool(
      "media-service-client", media_service_addr, media_service_port, 0,
      media_service_conns, media_service_timeout, media_service_keepalive,
      config_json);

  ClientPool<ThriftClient<HomeTimelineServiceClient>> home_timeline_client_pool(
      "home-timeline-client", post_storage_addr, home_timeline_port, 0,
      config_json["home-timeline-service"]["connections"],
      config_json["home-timeline-service"]["timeout_ms"],
      config_json["home-timeline-service"]["keepalive_ms"], config_json);

  ClientPool<ThriftClient<UserTimelineServiceClient>> user_timeline_client_pool(
      "user-timeline-client", post_storage_addr, user_timeline_port, 0,
      config_json["user-timeline-service"]["connections"],
      config_json["user-timeline-service"]["timeout_ms"],
      config_json["user-timeline-service"]["keepalive_ms"], config_json);

  ClientPool<ThriftClient<TextServiceClient>> text_service_client_pool(
      "text-service-client", post_storage_addr, text_service_port, 0,
      config_json["text-service"]["connections"],
      config_json["text-service"]["timeout_ms"],
      config_json["text-service"]["keepalive_ms"], config_json);

  ClientPool<ThriftClient<UrlShortenServiceClient>> url_shorten_client_pool(
      "url-shorten-client", post_storage_addr, url_shorten_port, 0,
      config_json["url-shorten-service"]["connections"],
      config_json["url-shorten-service"]["timeout_ms"],
      config_json["url-shorten-service"]["keepalive_ms"], config_json);

  ClientPool<ThriftClient<UserMentionServiceClient>> user_mention_client_pool(
      "user-mention-client", post_storage_addr, user_mention_port, 0,
      config_json["user-mention-service"]["connections"],
      config_json["user-mention-service"]["timeout_ms"],
      config_json["user-mention-service"]["keepalive_ms"], config_json);

  // Shared Thrift server factories.
  auto transport_factory = std::make_shared<TFramedTransportFactory>();
  auto protocol_factory = std::make_shared<TBinaryProtocolFactory>();

  // 1. Home-timeline server.
  std::shared_ptr<TServerSocket> home_server_socket =
      get_server_socket(config_json, "0.0.0.0", home_timeline_port);
  TThreadedServer home_timeline_server(
      std::make_shared<HomeTimelineServiceProcessor>(
          std::make_shared<HomeTimelineHandler>(
              &home_redis_client_pool, &post_storage_client_pool,
              &social_graph_client_pool)),
      home_server_socket, transport_factory, protocol_factory);

  // 2. User-timeline server.
  std::shared_ptr<TServerSocket> user_server_socket =
      get_server_socket(config_json, "0.0.0.0", user_timeline_port);
  TThreadedServer user_timeline_server(
      std::make_shared<UserTimelineServiceProcessor>(
          std::make_shared<UserTimelineHandler>(
              &user_redis_client_pool, user_timeline_mongodb_client_pool,
              &post_storage_client_pool)),
      user_server_socket, transport_factory, protocol_factory);

  // 3. Post-storage server.
  std::shared_ptr<TServerSocket> post_server_socket =
      get_server_socket(config_json, "0.0.0.0", post_storage_port);
  TThreadedServer post_storage_server(
      std::make_shared<PostStorageServiceProcessor>(
          std::make_shared<PostStorageHandler>(
              post_storage_memcached_client_pool,
              post_storage_mongodb_client_pool)),
      post_server_socket, transport_factory, protocol_factory);

  // 4. URL-shorten server.
  std::mutex url_shorten_thread_lock;
  std::shared_ptr<TServerSocket> url_server_socket =
      get_server_socket(config_json, "0.0.0.0", url_shorten_port);
  TThreadedServer url_shorten_server(
      std::make_shared<UrlShortenServiceProcessor>(
          std::make_shared<UrlShortenHandler>(
              url_shorten_memcached_client_pool,
              url_shorten_mongodb_client_pool, &url_shorten_thread_lock)),
      url_server_socket, transport_factory, protocol_factory);

  // 5. User-mention server.
  std::shared_ptr<TServerSocket> mention_server_socket =
      get_server_socket(config_json, "0.0.0.0", user_mention_port);
  TThreadedServer user_mention_server(
      std::make_shared<UserMentionServiceProcessor>(
          std::make_shared<UserMentionHandler>(user_memcached_client_pool,
                                               user_mongodb_client_pool)),
      mention_server_socket, transport_factory, protocol_factory);

  // 6. Text server.
  std::shared_ptr<TServerSocket> text_server_socket =
      get_server_socket(config_json, "0.0.0.0", text_service_port);
  TThreadedServer text_service_server(
      std::make_shared<TextServiceProcessor>(std::make_shared<TextHandler>(
          &url_shorten_client_pool, &user_mention_client_pool)),
      text_server_socket, transport_factory, protocol_factory);

  // 7. Compose-post server.
  std::shared_ptr<TServerSocket> compose_server_socket =
      get_server_socket(config_json, "0.0.0.0", compose_post_port);
  TThreadedServer compose_post_server(
      std::make_shared<ComposePostServiceProcessor>(
          std::make_shared<ComposePostHandler>(
              &post_storage_client_pool, &user_timeline_client_pool,
              &user_service_client_pool, &unique_id_client_pool,
              &media_service_client_pool, &text_service_client_pool,
              &home_timeline_client_pool)),
      compose_server_socket, transport_factory, protocol_factory);

  // Run all seven services in a single process.
  std::thread home_thread([&home_timeline_server]() {
    LOG(info) << "[m] Starting home-timeline server...";
    home_timeline_server.serve();
  });
  std::thread user_thread([&user_timeline_server]() {
    LOG(info) << "[m] Starting user-timeline server...";
    user_timeline_server.serve();
  });
  std::thread post_thread([&post_storage_server]() {
    LOG(info) << "[m] Starting post-storage server...";
    post_storage_server.serve();
  });
  std::thread url_thread([&url_shorten_server]() {
    LOG(info) << "[m] Starting url-shorten server...";
    url_shorten_server.serve();
  });
  std::thread mention_thread([&user_mention_server]() {
    LOG(info) << "[m] Starting user-mention server...";
    user_mention_server.serve();
  });
  std::thread text_thread([&text_service_server]() {
    LOG(info) << "[m] Starting text server...";
    text_service_server.serve();
  });
  std::thread compose_thread([&compose_post_server]() {
    LOG(info) << "[m] Starting compose-post server...";
    compose_post_server.serve();
  });

  home_thread.join();
  user_thread.join();
  post_thread.join();
  url_thread.join();
  mention_thread.join();
  text_thread.join();
  compose_thread.join();

  return 0;
}
