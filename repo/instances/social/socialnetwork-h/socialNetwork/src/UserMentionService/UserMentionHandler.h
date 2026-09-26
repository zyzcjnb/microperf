#ifndef SOCIAL_NETWORK_MICROSERVICES_SRC_USERMENTIONSERVICE_USERMENTIONHANDLER_H_
#define SOCIAL_NETWORK_MICROSERVICES_SRC_USERMENTIONSERVICE_USERMENTIONHANDLER_H_

#include <bson.h>
#include <libmemcached/memcached.h>
#include <libmemcached/util.h>
#include <mongoc.h>

#include "../../gen-cpp/UserMentionService.h"
#include "../../gen-cpp/UserService.h"
#include "../../gen-cpp/social_network_types.h"
#include "../ClientPool.h"
#include "../ThriftClient.h"
#include "../logger.h"
#include "../tracing.h"
#include "../utils.h"

namespace social_network {

class UserMentionHandler : public UserMentionServiceIf {
 public:
  UserMentionHandler(
      memcached_pool_st *, mongoc_client_pool_t *,
      ClientPool<ThriftClient<UserServiceClient>> *);
  ~UserMentionHandler() override = default;

  void ComposeUserMentions(std::vector<UserMention> &_return, int64_t,
                           const std::vector<std::string> &,
                           const std::map<std::string, std::string> &) override;

 private:
  memcached_pool_st *_memcached_client_pool;
  mongoc_client_pool_t *_mongodb_client_pool;
  ClientPool<ThriftClient<UserServiceClient>> *_user_service_client_pool;
};

UserMentionHandler::UserMentionHandler(
    memcached_pool_st *memcached_client_pool,
    mongoc_client_pool_t *mongodb_client_pool,
    ClientPool<ThriftClient<UserServiceClient>> *user_service_client_pool) {
  _memcached_client_pool = memcached_client_pool;
  _mongodb_client_pool = mongodb_client_pool;
  _user_service_client_pool = user_service_client_pool;
}

void UserMentionHandler::ComposeUserMentions(
    std::vector<UserMention> &_return, int64_t req_id,
    const std::vector<std::string> &usernames,
    const std::map<std::string, std::string> &carrier) {
  // Initialize a span
  TextMapReader reader(carrier);
  std::map<std::string, std::string> writer_text_map;
  TextMapWriter writer(writer_text_map);
  auto parent_span = opentracing::Tracer::Global()->Extract(reader);
  auto span = opentracing::Tracer::Global()->StartSpan(
      "compose_user_mentions_server",
      {opentracing::ChildOf(parent_span->get())});
  opentracing::Tracer::Global()->Inject(span->context(), writer);

  std::vector<UserMention> user_mentions;
  if (!usernames.empty()) {
    for (const auto &username : usernames) {
      auto user_client_wrapper = _user_service_client_pool->Pop();
      if (!user_client_wrapper) {
        ServiceException se;
        se.errorCode = ErrorCode::SE_THRIFT_CONN_ERROR;
        se.message = "Failed to connect to user-service";
        throw se;
      }
      auto user_client = user_client_wrapper->GetClient();
      int64_t user_id = -1;
      try {
        user_id = user_client->GetUserId(req_id, username, writer_text_map);
      } catch (...) {
        _user_service_client_pool->Remove(user_client_wrapper);
        LOG(error) << "Failed to get user_id from user-service for " << username;
        throw;
      }
      _user_service_client_pool->Keepalive(user_client_wrapper);

      if (user_id >= 0) {
        UserMention new_user_mention;
        new_user_mention.username = username;
        new_user_mention.user_id = user_id;
        user_mentions.emplace_back(new_user_mention);
      }
    }
  }

  _return = user_mentions;
  span->Finish();
}

}  // namespace social_network

#endif  // SOCIAL_NETWORK_MICROSERVICES_SRC_USERMENTIONSERVICE_USERMENTIONHANDLER_H_
