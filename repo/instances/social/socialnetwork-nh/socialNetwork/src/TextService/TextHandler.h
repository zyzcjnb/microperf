#ifndef SOCIAL_NETWORK_MICROSERVICES_TEXTHANDLER_H
#define SOCIAL_NETWORK_MICROSERVICES_TEXTHANDLER_H

#include <future>
#include <iostream>
#include <string>
#include <vector>

#include "../../gen-cpp/TextMentionService.h"
#include "../../gen-cpp/TextService.h"
#include "../../gen-cpp/TextUrlService.h"
#include "../ClientPool.h"
#include "../ThriftClient.h"
#include "../logger.h"
#include "../tracing.h"

namespace social_network {

class TextHandler : public TextServiceIf {
 public:
  TextHandler(ClientPool<ThriftClient<TextMentionServiceClient>> *,
              ClientPool<ThriftClient<TextUrlServiceClient>> *);
  ~TextHandler() override = default;

  void ComposeText(TextServiceReturn &_return, int64_t, const std::string &,
                   const std::map<std::string, std::string> &) override;

 private:
  ClientPool<ThriftClient<TextMentionServiceClient>> *_text_mention_client_pool;
  ClientPool<ThriftClient<TextUrlServiceClient>> *_text_url_client_pool;

  std::vector<UserMention> _ComposeTextMentionsHelper(
      int64_t req_id, const std::string &text,
      const std::map<std::string, std::string> &carrier);

  TextUrlReturn _ComposeTextUrlsHelper(
      int64_t req_id, const std::string &text,
      const std::map<std::string, std::string> &carrier);
};

TextHandler::TextHandler(
    ClientPool<social_network::ThriftClient<TextMentionServiceClient>> *text_mention_client_pool,
    ClientPool<social_network::ThriftClient<TextUrlServiceClient>> *text_url_client_pool) {
  _text_mention_client_pool = text_mention_client_pool;
  _text_url_client_pool = text_url_client_pool;
}

std::vector<UserMention> TextHandler::_ComposeTextMentionsHelper(
    int64_t req_id, const std::string &text,
    const std::map<std::string, std::string> &carrier) {
  TextMapReader reader(carrier);
  std::map<std::string, std::string> writer_text_map;
  TextMapWriter writer(writer_text_map);
  auto parent_span = opentracing::Tracer::Global()->Extract(reader);
  auto span = opentracing::Tracer::Global()->StartSpan(
      "compose_text_mentions_client",
      {opentracing::ChildOf(parent_span->get())});
  opentracing::Tracer::Global()->Inject(span->context(), writer);

  auto text_mention_client_wrapper = _text_mention_client_pool->Pop();
  if (!text_mention_client_wrapper) {
    ServiceException se;
    se.errorCode = ErrorCode::SE_THRIFT_CONN_ERROR;
    se.message = "Failed to connect to text-mention-service";
    span->Finish();
    throw se;
  }

  auto text_mention_client = text_mention_client_wrapper->GetClient();
  std::vector<UserMention> mention_return;
  try {
    text_mention_client->ComposeTextMentions(mention_return, req_id, text,
                                             writer_text_map);
  } catch (...) {
    LOG(error) << "Failed to send compose-text-mentions to text-mention-service";
    _text_mention_client_pool->Remove(text_mention_client_wrapper);
    span->Finish();
    throw;
  }
  _text_mention_client_pool->Keepalive(text_mention_client_wrapper);
  span->Finish();
  return mention_return;
}

TextUrlReturn TextHandler::_ComposeTextUrlsHelper(
    int64_t req_id, const std::string &text,
    const std::map<std::string, std::string> &carrier) {
  TextMapReader reader(carrier);
  std::map<std::string, std::string> writer_text_map;
  TextMapWriter writer(writer_text_map);
  auto parent_span = opentracing::Tracer::Global()->Extract(reader);
  auto span = opentracing::Tracer::Global()->StartSpan(
      "compose_text_urls_client", {opentracing::ChildOf(parent_span->get())});
  opentracing::Tracer::Global()->Inject(span->context(), writer);

  auto text_url_client_wrapper = _text_url_client_pool->Pop();
  if (!text_url_client_wrapper) {
    ServiceException se;
    se.errorCode = ErrorCode::SE_THRIFT_CONN_ERROR;
    se.message = "Failed to connect to text-url-service";
    span->Finish();
    throw se;
  }

  auto text_url_client = text_url_client_wrapper->GetClient();
  TextUrlReturn url_return;
  try {
    text_url_client->ComposeTextUrls(url_return, req_id, text, writer_text_map);
  } catch (...) {
    LOG(error) << "Failed to send compose-text-urls to text-url-service";
    _text_url_client_pool->Remove(text_url_client_wrapper);
    span->Finish();
    throw;
  }
  _text_url_client_pool->Keepalive(text_url_client_wrapper);
  span->Finish();
  return url_return;
}

void TextHandler::ComposeText(
    TextServiceReturn &_return, int64_t req_id, const std::string &text,
    const std::map<std::string, std::string> &carrier) {
  TextMapReader reader(carrier);
  std::map<std::string, std::string> writer_text_map;
  TextMapWriter writer(writer_text_map);
  auto parent_span = opentracing::Tracer::Global()->Extract(reader);
  auto span = opentracing::Tracer::Global()->StartSpan(
      "compose_text_server", {opentracing::ChildOf(parent_span->get())});
  opentracing::Tracer::Global()->Inject(span->context(), writer);

  auto mention_future = std::async(
      std::launch::async, &TextHandler::_ComposeTextMentionsHelper, this,
      req_id, text, writer_text_map);
  auto url_future = std::async(std::launch::async,
                               &TextHandler::_ComposeTextUrlsHelper, this,
                               req_id, text, writer_text_map);

  auto mention_return = mention_future.get();
  auto url_return = url_future.get();

  _return.text = url_return.updated_text;
  _return.urls = url_return.urls;
  _return.user_mentions = mention_return;
  span->Finish();
}

}  // namespace social_network

#endif  // SOCIAL_NETWORK_MICROSERVICES_TEXTHANDLER_H
