#ifndef SOCIAL_NETWORK_MICROSERVICES_SRC_TEXTURLSERVICE_TEXTURLHANDLER_H_
#define SOCIAL_NETWORK_MICROSERVICES_SRC_TEXTURLSERVICE_TEXTURLHANDLER_H_

#include <string>
#include <vector>

#include "../../gen-cpp/TextUrlService.h"
#include "../../gen-cpp/UrlShortenService.h"
#include "../ClientPool.h"
#include "../ThriftClient.h"
#include "../TextUtils.h"
#include "../logger.h"
#include "../tracing.h"

namespace social_network {

class TextUrlHandler : public TextUrlServiceIf {
 public:
  explicit TextUrlHandler(
      ClientPool<ThriftClient<UrlShortenServiceClient>> *url_client_pool)
      : _url_client_pool(url_client_pool) {}

  ~TextUrlHandler() override = default;

  void ComposeTextUrls(TextUrlReturn &_return, int64_t req_id,
                       const std::string &text,
                       const std::map<std::string, std::string> &carrier) override;

 private:
  ClientPool<ThriftClient<UrlShortenServiceClient>> *_url_client_pool;
};

void TextUrlHandler::ComposeTextUrls(
    TextUrlReturn &_return, int64_t req_id, const std::string &text,
    const std::map<std::string, std::string> &carrier) {
  TextMapReader reader(carrier);
  std::map<std::string, std::string> writer_text_map;
  TextMapWriter writer(writer_text_map);
  auto parent_span = opentracing::Tracer::Global()->Extract(reader);
  auto span = opentracing::Tracer::Global()->StartSpan(
      "compose_text_urls_server", {opentracing::ChildOf(parent_span->get())});
  opentracing::Tracer::Global()->Inject(span->context(), writer);

  auto urls = ExtractUrls(text);

  if (urls.empty()) {
    _return.updated_text = text;
    span->Finish();
    return;
  }

  auto url_client_wrapper = _url_client_pool->Pop();
  if (!url_client_wrapper) {
    ServiceException se;
    se.errorCode = ErrorCode::SE_THRIFT_CONN_ERROR;
    se.message = "Failed to connect to url-shorten-service";
    throw se;
  }

  auto url_client = url_client_wrapper->GetClient();
  std::vector<Url> target_urls;
  try {
    url_client->ComposeUrls(target_urls, req_id, urls, writer_text_map);
  } catch (...) {
    LOG(error) << "Failed to send compose-urls to url-shorten-service";
    _url_client_pool->Remove(url_client_wrapper);
    span->Finish();
    throw;
  }

  _url_client_pool->Keepalive(url_client_wrapper);
  _return.updated_text = ReplaceUrlsWithShortened(text, target_urls);
  _return.urls = target_urls;
  span->Finish();
}

}  // namespace social_network

#endif  // SOCIAL_NETWORK_MICROSERVICES_SRC_TEXTURLSERVICE_TEXTURLHANDLER_H_
