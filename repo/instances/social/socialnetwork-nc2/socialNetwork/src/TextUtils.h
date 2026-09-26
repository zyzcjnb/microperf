#ifndef SOCIAL_NETWORK_MICROSERVICES_SRC_TEXTUTILS_H_
#define SOCIAL_NETWORK_MICROSERVICES_SRC_TEXTUTILS_H_

#include <regex>
#include <string>
#include <vector>

#include "../gen-cpp/social_network_types.h"

namespace social_network {

inline std::vector<std::string> ExtractUsernames(const std::string &text) {
  std::vector<std::string> usernames;
  std::smatch match;
  std::regex regex_pattern("@[a-zA-Z0-9-_]+");
  auto remaining = text;

  while (std::regex_search(remaining, match, regex_pattern)) {
    auto username = match.str().substr(1);
    usernames.emplace_back(username);
    remaining = match.suffix().str();
  }

  return usernames;
}

inline std::vector<std::string> ExtractUrls(const std::string &text) {
  std::vector<std::string> urls;
  std::smatch match;
  std::regex regex_pattern("(http://|https://)([a-zA-Z0-9_!~*'().&=+$%-]+)");
  auto remaining = text;

  while (std::regex_search(remaining, match, regex_pattern)) {
    urls.emplace_back(match.str());
    remaining = match.suffix().str();
  }

  return urls;
}

inline std::string ReplaceUrlsWithShortened(
    const std::string &text, const std::vector<Url> &shortened_urls) {
  if (shortened_urls.empty()) {
    return text;
  }

  std::string updated_text;
  std::smatch match;
  std::regex regex_pattern("(http://|https://)([a-zA-Z0-9_!~*'().&=+$%-]+)");
  auto remaining = text;
  size_t index = 0;

  while (std::regex_search(remaining, match, regex_pattern)) {
    updated_text += match.prefix().str() + shortened_urls[index].shortened_url;
    remaining = match.suffix().str();
    index++;
  }

  updated_text += remaining;
  return updated_text;
}

}  // namespace social_network

#endif  // SOCIAL_NETWORK_MICROSERVICES_SRC_TEXTUTILS_H_