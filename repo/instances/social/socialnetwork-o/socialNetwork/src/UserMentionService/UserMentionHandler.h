#ifndef SOCIAL_NETWORK_MICROSERVICES_SRC_USERMENTIONSERVICE_USERMENTIONHANDLER_H_
#define SOCIAL_NETWORK_MICROSERVICES_SRC_USERMENTIONSERVICE_USERMENTIONHANDLER_H_

#include <bson.h>
#include <libmemcached/memcached.h>
#include <libmemcached/util.h>
#include <mongoc.h>

#include "../../gen-cpp/UserMentionService.h"
#include "../../gen-cpp/social_network_types.h"
#include "../ClientPool.h"
#include "../logger.h"
#include "../tracing.h"
#include "../utils.h"

namespace social_network {

class UserMentionHandler : public UserMentionServiceIf {
 public:
  UserMentionHandler(memcached_pool_st *, mongoc_client_pool_t *);
  ~UserMentionHandler() override = default;

  void ComposeUserMentions(std::vector<UserMention> &_return, int64_t,
                           const std::vector<std::string> &,
                           const std::map<std::string, std::string> &) override;

 private:
  memcached_pool_st *_memcached_client_pool;
  mongoc_client_pool_t *_mongodb_client_pool;
};

UserMentionHandler::UserMentionHandler(
    memcached_pool_st *memcached_client_pool,
    mongoc_client_pool_t *mongodb_client_pool) {
  _memcached_client_pool = memcached_client_pool;
  _mongodb_client_pool = mongodb_client_pool;
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
    std::map<std::string, bool> usernames_not_cached;

    for (auto &username : usernames) {
      usernames_not_cached.emplace(std::make_pair(username, false));
    }

    // Find in Memcached one username at a time
    memcached_return_t rc;
    auto client = memcached_pool_pop(_memcached_client_pool, true, &rc);
    if (!client) {
      ServiceException se;
      se.errorCode = ErrorCode::SE_MEMCACHED_ERROR;
      se.message = "Failed to pop a client from memcached pool";
      throw se;
    }

    for (auto &username : usernames) {
      std::string key_str = username + ":user_id";
      size_t return_value_length;
      uint32_t flags;
      auto get_span = opentracing::Tracer::Global()->StartSpan(
          "compose_user_mentions_memcached_get_client",
          {opentracing::ChildOf(&span->context())});
      char *return_value = memcached_get(
          client, key_str.c_str(), key_str.length(),
          &return_value_length, &flags, &rc);
      if (!return_value && rc != MEMCACHED_NOTFOUND) {
        memcached_pool_push(_memcached_client_pool, client);
        LOG(error) << "Cannot get username " << username
                   << " of request " << req_id;
        ServiceException se;
        se.errorCode = ErrorCode::SE_MEMCACHED_ERROR;
        se.message = memcached_strerror(client, rc);
        get_span->Finish();
        throw se;
      }
      get_span->Finish();

      if (return_value) {
        UserMention new_user_mention;
        new_user_mention.username = username;
        new_user_mention.user_id = std::stoul(
            std::string(return_value, return_value + return_value_length));
        user_mentions.emplace_back(new_user_mention);
        usernames_not_cached.erase(username);
        free(return_value);
      }
    }
    memcached_pool_push(_memcached_client_pool, client);

    // Find the rest in MongoDB one document at a time
    if (!usernames_not_cached.empty()) {
      mongoc_client_t *mongodb_client =
          mongoc_client_pool_pop(_mongodb_client_pool);
      if (!mongodb_client) {
        ServiceException se;
        se.errorCode = ErrorCode::SE_MONGODB_ERROR;
        se.message = "Failed to pop a client from MongoDB pool";
        throw se;
      }

      auto collection =
          mongoc_client_get_collection(mongodb_client, "user", "user");
      if (!collection) {
        ServiceException se;
        se.errorCode = ErrorCode::SE_MONGODB_ERROR;
        se.message = "Failed to create collection user from DB user";
        mongoc_client_pool_push(_mongodb_client_pool, mongodb_client);
        throw se;
      }

      for (auto &item : usernames_not_cached) {
        bson_t *query = bson_new();
        BSON_APPEND_UTF8(query, "username", item.first.c_str());

        auto find_span = opentracing::Tracer::Global()->StartSpan(
            "compose_user_mentions_mongo_find_client",
            {opentracing::ChildOf(&span->context())});
        mongoc_cursor_t *cursor =
            mongoc_collection_find_with_opts(collection, query, nullptr, nullptr);
        const bson_t *doc;

        while (mongoc_cursor_next(cursor, &doc)) {
          bson_iter_t iter;
          UserMention new_user_mention;
          if (bson_iter_init_find(&iter, doc, "user_id")) {
            new_user_mention.user_id = bson_iter_value(&iter)->value.v_int64;
          } else {
            ServiceException se;
            se.errorCode = ErrorCode::SE_MONGODB_ERROR;
            se.message = "Attribute of MongoDB item is not complete";
            bson_destroy(query);
            mongoc_cursor_destroy(cursor);
            mongoc_collection_destroy(collection);
            mongoc_client_pool_push(_mongodb_client_pool, mongodb_client);
            find_span->Finish();
            throw se;
          }
          if (bson_iter_init_find(&iter, doc, "username")) {
            new_user_mention.username = bson_iter_value(&iter)->value.v_utf8.str;
          } else {
            ServiceException se;
            se.errorCode = ErrorCode::SE_MONGODB_ERROR;
            se.message = "Attribute of MongoDB item is not complete";
            bson_destroy(query);
            mongoc_cursor_destroy(cursor);
            mongoc_collection_destroy(collection);
            mongoc_client_pool_push(_mongodb_client_pool, mongodb_client);
            find_span->Finish();
            throw se;
          }
          user_mentions.emplace_back(new_user_mention);
        }
        bson_destroy(query);
        mongoc_cursor_destroy(cursor);
        find_span->Finish();
      }

      mongoc_collection_destroy(collection);
      mongoc_client_pool_push(_mongodb_client_pool, mongodb_client);
    }
  }

  _return = user_mentions;
  span->Finish();
}

}  // namespace social_network

#endif  // SOCIAL_NETWORK_MICROSERVICES_SRC_USERMENTIONSERVICE_USERMENTIONHANDLER_H_
