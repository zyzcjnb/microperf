#ifndef MEDIA_MICROSERVICES_COMPOSEREVIEWHANDLER_H
#define MEDIA_MICROSERVICES_COMPOSEREVIEWHANDLER_H

#include <iostream>
#include <string>
#include <chrono>
#include <future>

#include <libmemcached/memcached.h>
#include <libmemcached/util.h>

#include "../../gen-cpp/ComposeReviewService.h"
#include "../../gen-cpp/media_service_types.h"
#include "../../gen-cpp/ReviewStorageService.h"
#include "../../gen-cpp/UserReviewService.h"
#include "../../gen-cpp/MovieReviewService.h"
#include "../ClientPool.h"
#include "../ThriftClient.h"
#include "../logger.h"
#include "../tracing.h"

namespace media_service {
#define NUM_COMPONENTS 5
#define MMC_EXP_TIME 10

using std::chrono::milliseconds;
using std::chrono::duration_cast;
using std::chrono::system_clock;

class ComposeReviewHandler : public ComposeReviewServiceIf {
 public:
  ComposeReviewHandler(
      memcached_pool_st *,
      ClientPool<ThriftClient<ReviewStorageServiceClient>> *,
      ClientPool<ThriftClient<UserReviewServiceClient>> *,
      ClientPool<ThriftClient<MovieReviewServiceClient>> *);
  ~ComposeReviewHandler() override = default;

  void UploadText(int64_t, const std::string &,
      const std::map<std::string, std::string> &) override;
  void UploadRating(int64_t, int32_t,
      const std::map<std::string, std::string> &) override;
  void UploadUniqueId(int64_t, int64_t,
      const std::map<std::string, std::string> &) override;
  void UploadMovieId(int64_t, const std::string &,
                     const std::map<std::string, std::string> &) override;
  void UploadUserId(int64_t, int64_t,
      const std::map<std::string, std::string> &) override;


 private:
  memcached_pool_st *_memcached_client_pool;
  ClientPool<ThriftClient<ReviewStorageServiceClient>>
      *_review_storage_client_pool;
  ClientPool<ThriftClient<UserReviewServiceClient>>
      *_user_review_client_pool;
  ClientPool<ThriftClient<MovieReviewServiceClient>>
      *_movie_review_client_pool;
  void _ComposeAndUpload(int64_t, const std::map<std::string, std::string> &);
};

ComposeReviewHandler::ComposeReviewHandler(
    memcached_pool_st *memcached_client_pool,
    ClientPool<ThriftClient<ReviewStorageServiceClient>> 
        *review_storage_client_pool,
    ClientPool<ThriftClient<UserReviewServiceClient>>
        *user_review_client_pool,
    ClientPool<ThriftClient<MovieReviewServiceClient>>
        *movie_review_client_pool ) {
  _memcached_client_pool = memcached_client_pool;
  _review_storage_client_pool = review_storage_client_pool;
  _user_review_client_pool = user_review_client_pool;
  _movie_review_client_pool = movie_review_client_pool;
}

void ComposeReviewHandler::_ComposeAndUpload(
    int64_t req_id, const std::map<std::string, std::string> &writer_text_map) {

  std::string key_unique_id = std::to_string(req_id) + ":review_id";
  std::string key_movie_id = std::to_string(req_id) + ":movie_id";
  std::string key_user_id = std::to_string(req_id) + ":user_id";
  std::string key_text = std::to_string(req_id) + ":text";
  std::string key_rating = std::to_string(req_id) + ":rating";

  const char* keys[NUM_COMPONENTS] = {
      key_unique_id.c_str(),
      key_movie_id.c_str(),
      key_user_id.c_str(),
      key_text.c_str(),
      key_rating.c_str()
  };

  size_t key_sizes[NUM_COMPONENTS] = {
      key_unique_id.size(),
      key_movie_id.size(),
      key_user_id.size(),
      key_text.size(),
      key_rating.size()
  };

  // Compose a review from the components obtained from memcached
  memcached_return_t rc;
  auto client = memcached_pool_pop(_memcached_client_pool, true, &rc);
  if (!client) {
    ServiceException se;
    se.errorCode = ErrorCode::SE_MEMCACHED_ERROR;
    se.message = "Failed to pop a client from memcached pool";
    throw se;
  }

  Review new_review;

  // Fetch each review component individually for clearer error handling
  size_t val_len;
  uint32_t val_flags;
  memcached_return_t get_rc;

  // Fetch review_id
  char *val_review_id = memcached_get(client, key_unique_id.c_str(),
      key_unique_id.size(), &val_len, &val_flags, &get_rc);
  if (get_rc != MEMCACHED_SUCCESS && get_rc != MEMCACHED_NOTFOUND) {
    LOG(error) << "Cannot get review_id of request " << req_id;
    ServiceException se;
    se.errorCode = ErrorCode::SE_MEMCACHED_ERROR;
    se.message = memcached_strerror(client, get_rc);
    memcached_pool_push(_memcached_client_pool, client);
    throw se;
  }
  if (val_review_id) {
    new_review.review_id = std::stoul(std::string(val_review_id, val_len));
    free(val_review_id);
  }

  // Fetch movie_id
  char *val_movie = memcached_get(client, key_movie_id.c_str(),
      key_movie_id.size(), &val_len, &val_flags, &get_rc);
  if (get_rc != MEMCACHED_SUCCESS && get_rc != MEMCACHED_NOTFOUND) {
    LOG(error) << "Cannot get movie_id of request " << req_id;
    ServiceException se;
    se.errorCode = ErrorCode::SE_MEMCACHED_ERROR;
    se.message = memcached_strerror(client, get_rc);
    memcached_pool_push(_memcached_client_pool, client);
    throw se;
  }
  if (val_movie) {
    new_review.movie_id = std::string(val_movie, val_len);
    free(val_movie);
  }

  // Fetch text
  char *val_text = memcached_get(client, key_text.c_str(),
      key_text.size(), &val_len, &val_flags, &get_rc);
  if (get_rc != MEMCACHED_SUCCESS && get_rc != MEMCACHED_NOTFOUND) {
    LOG(error) << "Cannot get text of request " << req_id;
    ServiceException se;
    se.errorCode = ErrorCode::SE_MEMCACHED_ERROR;
    se.message = memcached_strerror(client, get_rc);
    memcached_pool_push(_memcached_client_pool, client);
    throw se;
  }
  if (val_text) {
    new_review.text = std::string(val_text, val_len);
    free(val_text);
  }

  // Fetch user_id
  char *val_user = memcached_get(client, key_user_id.c_str(),
      key_user_id.size(), &val_len, &val_flags, &get_rc);
  if (get_rc != MEMCACHED_SUCCESS && get_rc != MEMCACHED_NOTFOUND) {
    LOG(error) << "Cannot get user_id of request " << req_id;
    ServiceException se;
    se.errorCode = ErrorCode::SE_MEMCACHED_ERROR;
    se.message = memcached_strerror(client, get_rc);
    memcached_pool_push(_memcached_client_pool, client);
    throw se;
  }
  if (val_user) {
    new_review.user_id = std::stoul(std::string(val_user, val_len));
    free(val_user);
  }

  // Fetch rating
  char *val_rating = memcached_get(client, key_rating.c_str(),
      key_rating.size(), &val_len, &val_flags, &get_rc);
  if (get_rc != MEMCACHED_SUCCESS && get_rc != MEMCACHED_NOTFOUND) {
    LOG(error) << "Cannot get rating of request " << req_id;
    ServiceException se;
    se.errorCode = ErrorCode::SE_MEMCACHED_ERROR;
    se.message = memcached_strerror(client, get_rc);
    memcached_pool_push(_memcached_client_pool, client);
    throw se;
  }
  if (val_rating) {
    new_review.rating = std::stoi(std::string(val_rating, val_len));
    free(val_rating);
  }

  memcached_quit(client);
  memcached_pool_push(_memcached_client_pool, client);

  new_review.timestamp = duration_cast<milliseconds>(
      system_clock::now().time_since_epoch()).count();
  new_review.req_id = req_id;

  auto review_storage_client_wrapper = _review_storage_client_pool->Pop();
  if (!review_storage_client_wrapper) {
    ServiceException se;
    se.errorCode = ErrorCode::SE_THRIFT_CONN_ERROR;
    se.message = "Failed to connected to review-storage-service";
    throw se;
  }
  auto review_storage_client = review_storage_client_wrapper->GetClient();
  try {
    Review partial1;
    partial1.__set_review_id(new_review.review_id);
    review_storage_client->StoreReview(req_id, partial1, writer_text_map);
  } catch (...) {
    _review_storage_client_pool->Push(review_storage_client_wrapper);
    LOG(error) << "Failed to store review_id to review-storage-service";
    throw;
  }

  try {
    Review partial2;
    partial2.__set_review_id(new_review.review_id);
    partial2.__set_user_id(new_review.user_id);
    review_storage_client->StoreReview(req_id, partial2, writer_text_map);
  } catch (...) {
    _review_storage_client_pool->Push(review_storage_client_wrapper);
    LOG(error) << "Failed to store user_id to review-storage-service";
    throw;
  }

  try {
    Review partial3;
    partial3.__set_review_id(new_review.review_id);
    partial3.__set_movie_id(new_review.movie_id);
    review_storage_client->StoreReview(req_id, partial3, writer_text_map);
  } catch (...) {
    _review_storage_client_pool->Push(review_storage_client_wrapper);
    LOG(error) << "Failed to store movie_id to review-storage-service";
    throw;
  }

  try {
    Review partial4;
    partial4.__set_review_id(new_review.review_id);
    partial4.__set_text(new_review.text);
    review_storage_client->StoreReview(req_id, partial4, writer_text_map);
  } catch (...) {
    _review_storage_client_pool->Push(review_storage_client_wrapper);
    LOG(error) << "Failed to store text to review-storage-service";
    throw;
  }

  try {
    Review partial5;
    partial5.__set_review_id(new_review.review_id);
    partial5.__set_rating(new_review.rating);
    review_storage_client->StoreReview(req_id, partial5, writer_text_map);
  } catch (...) {
    _review_storage_client_pool->Push(review_storage_client_wrapper);
    LOG(error) << "Failed to store rating to review-storage-service";
    throw;
  }

  try {
    Review partial6;
    partial6.__set_review_id(new_review.review_id);
    partial6.__set_timestamp(new_review.timestamp);
    review_storage_client->StoreReview(req_id, partial6, writer_text_map);
  } catch (...) {
    _review_storage_client_pool->Push(review_storage_client_wrapper);
    LOG(error) << "Failed to store timestamp to review-storage-service";
    throw;
  }

  try {
    Review partial7;
    partial7.__set_review_id(new_review.review_id);
    partial7.__set_req_id(new_review.req_id);
    review_storage_client->StoreReview(req_id, partial7, writer_text_map);
  } catch (...) {
    _review_storage_client_pool->Push(review_storage_client_wrapper);
    LOG(error) << "Failed to store req_id to review-storage-service";
    throw;
  }

  _review_storage_client_pool->Push(review_storage_client_wrapper);

  {
    auto user_review_client_wrapper = _user_review_client_pool->Pop();
    if (!user_review_client_wrapper) {
      ServiceException se;
      se.errorCode = ErrorCode::SE_THRIFT_CONN_ERROR;
      se.message = "Failed to connected to user-review-service";
      throw se;
    }
    auto user_review_client = user_review_client_wrapper->GetClient();
    try {
      user_review_client->UploadUserReview(req_id, new_review.user_id,
          new_review.review_id, new_review.timestamp, writer_text_map);
    } catch (...) {
      _user_review_client_pool->Push(user_review_client_wrapper);
      LOG(error) << "Failed to upload review to user-review-service";
      throw;
    }
    _user_review_client_pool->Push(user_review_client_wrapper);
  }

  {
    auto movie_review_client_wrapper = _movie_review_client_pool->Pop();
    if (!movie_review_client_wrapper) {
      ServiceException se;
      se.errorCode = ErrorCode::SE_THRIFT_CONN_ERROR;
      se.message = "Failed to connected to movie-review-service";
      throw se;
    }
    auto movie_review_client = movie_review_client_wrapper->GetClient();
    try {
      movie_review_client->UploadMovieReview(req_id, new_review.movie_id,
          new_review.review_id, new_review.timestamp, writer_text_map);
    } catch (...) {
      _movie_review_client_pool->Push(movie_review_client_wrapper);
      LOG(error) << "Failed to upload review to movie-review-service";
      throw;
    }
    _movie_review_client_pool->Push(movie_review_client_wrapper);
  }
}

void ComposeReviewHandler::UploadMovieId(
    int64_t req_id,
    const std::string &movie_id,
    const std::map<std::string, std::string> & carrier) {

  // Initialize a span
  TextMapReader reader(carrier);
  std::map<std::string, std::string> writer_text_map;
  TextMapWriter writer(writer_text_map);
  auto parent_span = opentracing::Tracer::Global()->Extract(reader);
  auto span = opentracing::Tracer::Global()->StartSpan(
      "UploadMovieId",
      { opentracing::ChildOf(parent_span->get()) });
  opentracing::Tracer::Global()->Inject(span->context(), writer);

  memcached_return_t memcached_rc;
  std::string key_counter = std::to_string(req_id) + ":counter";
  memcached_st *memcached_client = memcached_pool_pop(
      _memcached_client_pool, true, &memcached_rc);

  // Initialize the counter to 0 if there it is not in the memcached
  memcached_rc = memcached_add(
      memcached_client,
      key_counter.c_str(),
      key_counter.size(),
      "0", 1, MMC_EXP_TIME, 0);

  // error if it cannot be stored
  if (memcached_rc != MEMCACHED_SUCCESS &&
      memcached_rc != MEMCACHED_DATA_EXISTS) {
    LOG(error) << "Failed to initilize the counter for request " << req_id
        << " Error code: "
        << memcached_strerror(memcached_client, memcached_rc);
    ServiceException se;
    se.errorCode = ErrorCode::SE_MEMCACHED_ERROR;
    se.message = memcached_strerror(memcached_client, memcached_rc);
    memcached_pool_push(_memcached_client_pool, memcached_client);
    throw se;
  }

  // Store movie_id to memcached
  uint64_t counter_value;
  std::string key_movie_id = std::to_string(req_id) + ":movie_id";
  memcached_rc = memcached_add(
      memcached_client,
      key_movie_id.c_str(),
      key_movie_id.size(),
      movie_id.c_str(),
      movie_id.size(),
      MMC_EXP_TIME, 0);
  if (memcached_rc == MEMCACHED_DATA_EXISTS) {
    // Another thread has uploaded movie_id, which is an unexpected behaviour.
    LOG(warning) << "movie_id of request " << req_id
                 << " has already been stored";
    size_t value_size;
    char *counter_value_str = memcached_get(
        memcached_client,
        key_counter.c_str(),
        key_counter.size(),
        &value_size,
        nullptr,
        &memcached_rc);
    if (memcached_rc != MEMCACHED_SUCCESS) {
      LOG(error) << "Cannot get the counter of request " << req_id;
      ServiceException se;
      se.errorCode = ErrorCode::SE_MEMCACHED_ERROR;
      se.message = memcached_strerror(memcached_client, memcached_rc);
      memcached_pool_push(_memcached_client_pool, memcached_client);
      throw se;
    }
    counter_value = std::stoul(counter_value_str);
    free(counter_value_str);
  } else if (memcached_rc != MEMCACHED_SUCCESS) {
    LOG(error) << "Cannot store movie_id of request " << req_id;
    ServiceException se;
    se.errorCode = ErrorCode::SE_MEMCACHED_ERROR;
    se.message = memcached_strerror(memcached_client, memcached_rc);
    memcached_pool_push(_memcached_client_pool, memcached_client);
    throw se;
  } else {
    // Atomically increment and get the counter value
    memcached_increment(
        memcached_client,
        key_counter.c_str(),
        key_counter.size(),
        1, &counter_value);
    if (memcached_rc != MEMCACHED_SUCCESS) {
      LOG(error) << "Cannot increment and get the counter of request "
          << req_id;
      ServiceException se;
      se.errorCode = ErrorCode::SE_MEMCACHED_ERROR;
      se.message = memcached_strerror(memcached_client, memcached_rc);
      memcached_pool_push(_memcached_client_pool, memcached_client);
      throw se;
    }
  }
  LOG(debug) << "req_id " << req_id
      << " caching movie_id to Memcached finished";
  memcached_pool_push(_memcached_client_pool, memcached_client);

  // If this thread is the last one uploading the review components,
  // it is in charge of compose the request and upload to the microservices in
  // the next tier.
  if (counter_value == NUM_COMPONENTS) {
    _ComposeAndUpload(req_id, writer_text_map);
  }
  span->Finish();
}

void ComposeReviewHandler::UploadUserId(
    int64_t req_id, int64_t user_id,
    const std::map<std::string, std::string> & carrier) {

  // Initialize a span
  TextMapReader reader(carrier);
  std::map<std::string, std::string> writer_text_map;
  TextMapWriter writer(writer_text_map);
  auto parent_span = opentracing::Tracer::Global()->Extract(reader);
  auto span = opentracing::Tracer::Global()->StartSpan(
      "UploadUserId",
      { opentracing::ChildOf(parent_span->get()) });
  opentracing::Tracer::Global()->Inject(span->context(), writer);

  memcached_return_t memcached_rc;
  std::string key_counter = std::to_string(req_id) + ":counter";
  memcached_st *memcached_client = memcached_pool_pop(
      _memcached_client_pool, true, &memcached_rc);

  // Initialize the counter to 0 if there it is not in the memcached
  memcached_rc = memcached_add(
      memcached_client,
      key_counter.c_str(),
      key_counter.size(),
      "0", 1, MMC_EXP_TIME, 0);

  // error if it cannot be stored
  if (memcached_rc != MEMCACHED_SUCCESS &&
      memcached_rc != MEMCACHED_DATA_EXISTS) {
    LOG(error) << "Failed to initilize the counter for request " << req_id
        << " Error code: "
        << memcached_strerror(memcached_client, memcached_rc);
    ServiceException se;
    se.errorCode = ErrorCode::SE_MEMCACHED_ERROR;
    se.message = memcached_strerror(memcached_client, memcached_rc);
    memcached_pool_push(_memcached_client_pool, memcached_client);
    throw se;
  }

  // Store user_id to memcached
  uint64_t counter_value;
  std::string key_user_id = std::to_string(req_id) + ":user_id";
  std::string user_id_str = std::to_string(user_id);
  memcached_rc = memcached_add(
      memcached_client,
      key_user_id.c_str(),
      key_user_id.size(),
      user_id_str.c_str(),
      user_id_str.size(),
      MMC_EXP_TIME, 0);
  if (memcached_rc == MEMCACHED_DATA_EXISTS) {
    // Another thread has uploaded user_id, which is an unexpected behaviour.
    LOG(warning) << "user_id of request " << req_id
                 << " has already been stored";
    size_t value_size;
    char *counter_value_str = memcached_get(
        memcached_client,
        key_counter.c_str(),
        key_counter.size(),
        &value_size,
        0,
        &memcached_rc);
    if (memcached_rc != MEMCACHED_SUCCESS) {
      LOG(error) << "Cannot get the counter of request " << req_id;
      ServiceException se;
      se.errorCode = ErrorCode::SE_MEMCACHED_ERROR;
      se.message = memcached_strerror(memcached_client, memcached_rc);
      memcached_pool_push(_memcached_client_pool, memcached_client);
      throw se;
    }
    counter_value = std::stoul(counter_value_str);
    free(counter_value_str);
  } else if (memcached_rc != MEMCACHED_SUCCESS) {
    LOG(error) << "Cannot store user_id of request " << req_id
               << " Error code: "
               << memcached_strerror(memcached_client, memcached_rc);
    ServiceException se;
    se.errorCode = ErrorCode::SE_MEMCACHED_ERROR;
    se.message = memcached_strerror(memcached_client, memcached_rc);
    memcached_pool_push(_memcached_client_pool, memcached_client);
    throw se;
  } else {
    // Atomically increment and get the counter value
    memcached_increment(
        memcached_client,
        key_counter.c_str(),
        key_counter.size(),
        1, &counter_value);
    if (memcached_rc != MEMCACHED_SUCCESS) {
      LOG(error) << "Cannot increment and get the counter of request "
                 << req_id;
      ServiceException se;
      se.errorCode = ErrorCode::SE_MEMCACHED_ERROR;
      se.message = memcached_strerror(memcached_client, memcached_rc);
      memcached_pool_push(_memcached_client_pool, memcached_client);
      throw se;
    }
  }
  LOG(debug) << "req_id " << req_id << "caching user to Memcached finished";
  memcached_pool_push(_memcached_client_pool, memcached_client);

  // If this thread is the last one uploading the review components,
  // it is in charge of compose the request and upload to the microservices in
  // the next tier.
  if (counter_value == NUM_COMPONENTS) {
    _ComposeAndUpload(req_id, writer_text_map);
  }
  span->Finish();
}

void ComposeReviewHandler::UploadUniqueId(
    int64_t req_id, int64_t review_id,
    const std::map<std::string, std::string> & carrier) {

  // Initialize a span
  TextMapReader reader(carrier);
  std::map<std::string, std::string> writer_text_map;
  TextMapWriter writer(writer_text_map);
  auto parent_span = opentracing::Tracer::Global()->Extract(reader);
  auto span = opentracing::Tracer::Global()->StartSpan(
      "UploadUniqueId",
      { opentracing::ChildOf(parent_span->get()) });
  opentracing::Tracer::Global()->Inject(span->context(), writer);

  memcached_return_t memcached_rc;
  std::string key_counter = std::to_string(req_id) + ":counter";
  memcached_st *memcached_client = memcached_pool_pop(
      _memcached_client_pool, true, &memcached_rc);

  // Initialize the counter to 0 if there it is not in the memcached
  memcached_rc = memcached_add(
      memcached_client,
      key_counter.c_str(),
      key_counter.size(),
      "0", 1, MMC_EXP_TIME, 0);

  // error if it cannot be stored
  if (memcached_rc != MEMCACHED_SUCCESS &&
      memcached_rc != MEMCACHED_DATA_EXISTS) {
    LOG(error) << "Failed to initilize the counter for request " << req_id
               << " Error code: "
               << memcached_strerror(memcached_client, memcached_rc);
    ServiceException se;
    se.errorCode = ErrorCode::SE_MEMCACHED_ERROR;
    se.message = memcached_strerror(memcached_client, memcached_rc);
    memcached_pool_push(_memcached_client_pool, memcached_client);
    throw se;
  }

  // Store review_id to memcached
  uint64_t counter_value;
  std::string key_unique_id = std::to_string(req_id) + ":review_id";
  std::string unique_id_str = std::to_string(review_id);
  memcached_rc = memcached_add(
      memcached_client,
      key_unique_id.c_str(),
      key_unique_id.size(),
      unique_id_str.c_str(),
      unique_id_str.size(),
      MMC_EXP_TIME, 0);
  if (memcached_rc == MEMCACHED_DATA_EXISTS) {
    // Another thread has uploaded review_id, which is an unexpected behaviour.
    LOG(warning) << "review_id of request " << req_id
                 << " has already been stored";
    size_t value_size;
    char *counter_value_str = memcached_get(
        memcached_client,
        key_counter.c_str(),
        key_counter.size(),
        &value_size,
        nullptr,
        &memcached_rc);

    if (memcached_rc != MEMCACHED_SUCCESS) {
      LOG(error) << "Cannot get the counter of request " << req_id;
      ServiceException se;
      se.errorCode = ErrorCode::SE_MEMCACHED_ERROR;
      se.message = memcached_strerror(memcached_client, memcached_rc);
      memcached_pool_push(_memcached_client_pool, memcached_client);
      throw se;
    }
    counter_value = std::stoul(counter_value_str);
    free(counter_value_str);
  } else if (memcached_rc != MEMCACHED_SUCCESS) {
    LOG(error) << "Cannot store review_id of request " << req_id;
    ServiceException se;
    se.errorCode = ErrorCode::SE_MEMCACHED_ERROR;
    se.message = memcached_strerror(memcached_client, memcached_rc);
    memcached_pool_push(_memcached_client_pool, memcached_client);
    throw se;
  } else {
    // Atomically increment and get the counter value
    memcached_increment(
        memcached_client,
        key_counter.c_str(),
        key_counter.size(),
        1, &counter_value);
    if (memcached_rc != MEMCACHED_SUCCESS) {
      LOG(error) << "Cannot increment and get the counter of request "
                 << req_id;
      ServiceException se;
      se.errorCode = ErrorCode::SE_MEMCACHED_ERROR;
      se.message = memcached_strerror(memcached_client, memcached_rc);
      memcached_pool_push(_memcached_client_pool, memcached_client);
      throw se;
    }
  }
  LOG(debug) << "req_id " << req_id
             << " caching review_id to Memcached finished";

  memcached_pool_push(_memcached_client_pool, memcached_client);

  // If this thread is the last one uploading the review components,
  // it is in charge of compose the request and upload to the microservices in
  // the next tier.
  if (counter_value == NUM_COMPONENTS) {
    _ComposeAndUpload(req_id, writer_text_map);
  }
  span->Finish();
}

void ComposeReviewHandler::UploadText(
    int64_t req_id,
    const std::string &text,
    const std::map<std::string, std::string> & carrier) {

  // Initialize a span
  TextMapReader reader(carrier);
  std::map<std::string, std::string> writer_text_map;
  TextMapWriter writer(writer_text_map);
  auto parent_span = opentracing::Tracer::Global()->Extract(reader);
  auto span = opentracing::Tracer::Global()->StartSpan(
      "UploadText",
      { opentracing::ChildOf(parent_span->get()) });
  opentracing::Tracer::Global()->Inject(span->context(), writer);

  memcached_return_t memcached_rc;
  std::string key_counter = std::to_string(req_id) + ":counter";
  memcached_st *memcached_client = memcached_pool_pop(
      _memcached_client_pool, true, &memcached_rc);

  // Initialize the counter to 0 if there it is not in the memcached
  memcached_rc = memcached_add(
      memcached_client,
      key_counter.c_str(),
      key_counter.size(),
      "0", 1, MMC_EXP_TIME, 0);

  // error if it cannot be stored
  if (memcached_rc != MEMCACHED_SUCCESS &&
      memcached_rc != MEMCACHED_DATA_EXISTS) {
    LOG(error) << "Failed to initilize the counter for request " << req_id
               << " Error code: "
               << memcached_strerror(memcached_client, memcached_rc);
    ServiceException se;
    se.errorCode = ErrorCode::SE_MEMCACHED_ERROR;
    se.message = memcached_strerror(memcached_client, memcached_rc);
    memcached_pool_push(_memcached_client_pool, memcached_client);
    throw se;
  }

  // Store text to memcached
  uint64_t counter_value;
  std::string key_text = std::to_string(req_id) + ":text";
  memcached_rc = memcached_add(
      memcached_client,
      key_text.c_str(),
      key_text.size(),
      text.c_str(),
      text.size(),
      MMC_EXP_TIME, 0);
  if (memcached_rc == MEMCACHED_DATA_EXISTS) {
    // Another thread has uploaded text, which is an unexpected behaviour.
    LOG(warning) << "text of request " << req_id
                 << " has already been stored";
    size_t value_size;
    char *counter_value_str = memcached_get(
        memcached_client,
        key_counter.c_str(),
        key_counter.size(),
        &value_size,
        nullptr,
        &memcached_rc);
    if (memcached_rc != MEMCACHED_SUCCESS) {
      LOG(error) << "Cannot get the counter of request " << req_id;
      ServiceException se;
      se.errorCode = ErrorCode::SE_MEMCACHED_ERROR;
      se.message = memcached_strerror(memcached_client, memcached_rc);
      memcached_pool_push(_memcached_client_pool, memcached_client);
      throw se;
    }
    counter_value = std::stoul(counter_value_str);
    free(counter_value_str);
  } else if (memcached_rc != MEMCACHED_SUCCESS) {
    LOG(error) << "Cannot store text of request " << req_id;
    ServiceException se;
    se.errorCode = ErrorCode::SE_MEMCACHED_ERROR;
    se.message = memcached_strerror(memcached_client, memcached_rc);
    memcached_pool_push(_memcached_client_pool, memcached_client);
    throw se;
  } else {
    // Atomically increment and get the counter value
    memcached_increment(
        memcached_client,
        key_counter.c_str(),
        key_counter.size(),
        1, &counter_value);
    if (memcached_rc != MEMCACHED_SUCCESS) {
      LOG(error) << "Cannot increment and get the counter of request "
                 << req_id;
      ServiceException se;
      se.errorCode = ErrorCode::SE_MEMCACHED_ERROR;
      se.message = memcached_strerror(memcached_client, memcached_rc);
      memcached_pool_push(_memcached_client_pool, memcached_client);
      throw se;
    }
  }
  LOG(debug) << "req_id " << req_id << "caching text to Memcached finished";
  memcached_pool_push(_memcached_client_pool, memcached_client);

  // If this thread is the last one uploading the review components,
  // it is in charge of compose the request and upload to the microservices in
  // the next tier.
  if (counter_value == NUM_COMPONENTS) {
    _ComposeAndUpload(req_id, writer_text_map);
  }
  span->Finish();
}

void ComposeReviewHandler::UploadRating(
    int64_t req_id, int32_t rating, const std::map<std::string, std::string> & carrier) {

  // Initialize a span
  TextMapReader reader(carrier);
  std::map<std::string, std::string> writer_text_map;
  TextMapWriter writer(writer_text_map);
  auto parent_span = opentracing::Tracer::Global()->Extract(reader);
  auto span = opentracing::Tracer::Global()->StartSpan(
      "UploadRating",
      { opentracing::ChildOf(parent_span->get()) });
  opentracing::Tracer::Global()->Inject(span->context(), writer);

  memcached_return_t memcached_rc;
  std::string key_counter = std::to_string(req_id) + ":counter";
  memcached_st *memcached_client = memcached_pool_pop(
      _memcached_client_pool, true, &memcached_rc);

  // Initialize the counter to 0 if there it is not in the memcached
  memcached_rc = memcached_add(
      memcached_client,
      key_counter.c_str(),
      key_counter.size(),
      "0", 1, MMC_EXP_TIME, 0);

  // error if it cannot be stored
  if (memcached_rc != MEMCACHED_SUCCESS &&
      memcached_rc != MEMCACHED_DATA_EXISTS) {
    LOG(error) << "Failed to initilize the counter for request " << req_id
               << " Error code: " << memcached_strerror(memcached_client, memcached_rc);
    ServiceException se;
    se.errorCode = ErrorCode::SE_MEMCACHED_ERROR;
    se.message = memcached_strerror(memcached_client, memcached_rc);
    memcached_pool_push(_memcached_client_pool, memcached_client);
    throw se;
  }

  // Store rating to memcached
  uint64_t counter_value;
  std::string key_rating = std::to_string(req_id) + ":rating";
  std::string rating_str = std::to_string(rating);
  memcached_rc = memcached_add(
      memcached_client,
      key_rating.c_str(),
      key_rating.size(),
      rating_str.c_str(),
      rating_str.size(),
      MMC_EXP_TIME, 0);
  if (memcached_rc == MEMCACHED_DATA_EXISTS) {
    // Another thread has uploaded rating, which is an unexpected behaviour.
    LOG(warning) << "rating of request " << req_id
                 << " has already been stored";
    size_t value_size;
    char *counter_value_str = memcached_get(
        memcached_client,
        key_counter.c_str(),
        key_counter.size(),
        &value_size,
        0,
        &memcached_rc);
    counter_value = std::stoul(counter_value_str);
    free(counter_value_str);
    if (memcached_rc != MEMCACHED_SUCCESS) {
      LOG(error) << "Cannot get the counter of request " << req_id;
      ServiceException se;
      se.errorCode = ErrorCode::SE_MEMCACHED_ERROR;
      se.message = memcached_strerror(memcached_client, memcached_rc);
      memcached_pool_push(_memcached_client_pool, memcached_client);
      throw se;
    }
  } else if (memcached_rc != MEMCACHED_SUCCESS) {
    LOG(error) << "Cannot store rating of request " << req_id;
    ServiceException se;
    se.errorCode = ErrorCode::SE_MEMCACHED_ERROR;
    se.message = memcached_strerror(memcached_client, memcached_rc);
    memcached_pool_push(_memcached_client_pool, memcached_client);
    throw se;
  } else {
    // Atomically increment and get the counter value
    memcached_increment(
        memcached_client,
        key_counter.c_str(),
        key_counter.size(),
        1, &counter_value);
    if (memcached_rc != MEMCACHED_SUCCESS) {
      LOG(error) << "Cannot increment and get the counter of request "
                 << req_id;
      ServiceException se;
      se.errorCode = ErrorCode::SE_MEMCACHED_ERROR;
      se.message = memcached_strerror(memcached_client, memcached_rc);
      memcached_pool_push(_memcached_client_pool, memcached_client);
      throw se;
    }
  }
  LOG(debug) << "req_id " << req_id << " caching rating to Memcached finished";
  memcached_pool_push(_memcached_client_pool, memcached_client);

  // If this thread is the last one uploading the review components,
  // it is in charge of compose the request and upload to the microservices in
  // the next tier.
  if (counter_value == NUM_COMPONENTS) {
    _ComposeAndUpload(req_id, writer_text_map);
  }
  span->Finish();
}

} // namespace media_service


#endif //MEDIA_MICROSERVICES_COMPOSEREVIEWHANDLER_H
