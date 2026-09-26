#ifndef SOCIAL_NETWORK_MICROSERVICES_SRC_TEXTMENTIONSERVICE_TEXTMENTIONHANDLER_H_
#define SOCIAL_NETWORK_MICROSERVICES_SRC_TEXTMENTIONSERVICE_TEXTMENTIONHANDLER_H_

#include <string>
#include <vector>

#include "../../gen-cpp/TextMentionService.h"
#include "../../gen-cpp/UserMentionService.h"
#include "../ClientPool.h"
#include "../ThriftClient.h"
#include "../TextUtils.h"
#include "../logger.h"
#include "../tracing.h"

namespace social_network {

class TextMentionHandler : public TextMentionServiceIf {
 public:
  explicit TextMentionHandler(
      ClientPool<ThriftClient<UserMentionServiceClient>> *user_mention_pool)
      : _user_mention_client_pool(user_mention_pool) {}

  ~TextMentionHandler() override = default;

  void ComposeTextMentions(std::vector<UserMention> &_return, int64_t req_id,
                           const std::string &text,
                           const std::map<std::string, std::string> &carrier) override;

 private:
  ClientPool<ThriftClient<UserMentionServiceClient>> *_user_mention_client_pool;
};

void TextMentionHandler::ComposeTextMentions(
    std::vector<UserMention> &_return, int64_t req_id, const std::string &text,
    const std::map<std::string, std::string> &carrier) {
  TextMapReader reader(carrier);
  std::map<std::string, std::string> writer_text_map;
  TextMapWriter writer(writer_text_map);
  auto parent_span = opentracing::Tracer::Global()->Extract(reader);
  auto span = opentracing::Tracer::Global()->StartSpan(
      "compose_text_mentions_server",
      {opentracing::ChildOf(parent_span->get())});
  opentracing::Tracer::Global()->Inject(span->context(), writer);

  auto usernames = ExtractUsernames(text);

  if (usernames.empty()) {
    span->Finish();
    return;
  }

  auto user_mention_client_wrapper = _user_mention_client_pool->Pop();
  if (!user_mention_client_wrapper) {
    ServiceException se;
    se.errorCode = ErrorCode::SE_THRIFT_CONN_ERROR;
    se.message = "Failed to connect to user-mention-service";
    throw se;
  }

  auto user_mention_client = user_mention_client_wrapper->GetClient();
  try {
    user_mention_client->ComposeUserMentions(_return, req_id, usernames,
                                             writer_text_map);
  } catch (...) {
    LOG(error) << "Failed to send compose-user-mentions to user-mention-service";
    _user_mention_client_pool->Remove(user_mention_client_wrapper);
    span->Finish();
    throw;
  }

  _user_mention_client_pool->Keepalive(user_mention_client_wrapper);
  span->Finish();
}

}  // namespace social_network

#endif  // SOCIAL_NETWORK_MICROSERVICES_SRC_TEXTMENTIONSERVICE_TEXTMENTIONHANDLER_H_
