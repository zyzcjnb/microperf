#ifndef MEDIA_MICROSERVICES_SRC_MOVIEINFOSERVICE_MOVIEINFOHANDLER_H_
#define MEDIA_MICROSERVICES_SRC_MOVIEINFOSERVICE_MOVIEINFOHANDLER_H_

#include <iostream>
#include <string>

#include <libmemcached/memcached.h>
#include <libmemcached/util.h>
#include <mongoc.h>
#include <bson/bson.h>
#include <nlohmann/json.hpp>

#include "../../gen-cpp/MovieInfoService.h"
#include "../logger.h"
#include "../tracing.h"

namespace media_service {
using json = nlohmann::json;

class MovieInfoHandler : public MovieInfoServiceIf {
 public:
  MovieInfoHandler(
      memcached_pool_st *,
      mongoc_client_pool_t *);
  ~MovieInfoHandler() override = default;
  void ReadMovieInfo(MovieInfo& _return, int64_t req_id,
      const std::string& movie_id,
      const std::map<std::string, std::string> & carrier) override;
  void WriteMovieInfo(int64_t req_id, const std::string& movie_id, 
      const std::string& title, const std::vector<Cast> & casts,
      int64_t plot_id, const std::vector<std::string> & thumbnail_ids,
      const std::vector<std::string> & photo_ids,
      const std::vector<std::string> & video_ids,
      const std::string &avg_rating, int32_t num_rating,
      const std::map<std::string, std::string> & carrier) override;
  void UpdateRating(int64_t req_id, const std::string& movie_id,
      int32_t sum_uncommitted_rating, int32_t num_uncommitted_rating,
      const std::map<std::string, std::string> & carrier) override;


 private:
  memcached_pool_st *_memcached_client_pool;
  mongoc_client_pool_t *_mongodb_client_pool;
};

MovieInfoHandler::MovieInfoHandler(
    memcached_pool_st *memcached_client_pool,
    mongoc_client_pool_t *mongodb_client_pool) {
  _memcached_client_pool = memcached_client_pool;
  _mongodb_client_pool = mongodb_client_pool;
}

void MovieInfoHandler::WriteMovieInfo(
    int64_t req_id,
    const std::string &movie_id,
    const std::string &title,
    const std::vector<Cast> &casts,
    int64_t plot_id,
    const std::vector<std::string> &thumbnail_ids,
    const std::vector<std::string> &photo_ids,
    const std::vector<std::string> &video_ids,
    const std::string & avg_rating,
    int32_t num_rating,
    const std::map<std::string, std::string> &carrier) {
  // Initialize a span
  TextMapReader reader(carrier);
  std::map<std::string, std::string> writer_text_map;
  TextMapWriter writer(writer_text_map);
  auto parent_span = opentracing::Tracer::Global()->Extract(reader);
  auto span = opentracing::Tracer::Global()->StartSpan(
      "WriteMovieInfo",
      { opentracing::ChildOf(parent_span->get()) });
  opentracing::Tracer::Global()->Inject(span->context(), writer);

  bson_t *new_doc = bson_new();
  BSON_APPEND_UTF8(new_doc, "movie_id", movie_id.c_str());
  BSON_APPEND_UTF8(new_doc, "title", title.c_str());
  BSON_APPEND_INT64(new_doc, "plot_id", plot_id);
  BSON_APPEND_DOUBLE(new_doc, "avg_rating", std::stod(avg_rating));
  BSON_APPEND_INT64(new_doc, "num_rating", num_rating);
  const char *key;
  int idx = 0;
  char buf[16];
  
  bson_t cast_list;
  BSON_APPEND_ARRAY_BEGIN(new_doc, "casts", &cast_list);
  for (auto &cast : casts) {
    bson_uint32_to_string(idx, &key, buf, sizeof buf);
    bson_t cast_doc;
    BSON_APPEND_DOCUMENT_BEGIN(&cast_list, key, &cast_doc);
    BSON_APPEND_INT64(&cast_doc, "cast_id", cast.cast_id);
    BSON_APPEND_INT64(&cast_doc, "cast_info_id", cast.cast_info_id);
    BSON_APPEND_UTF8(&cast_doc, "character", cast.character.c_str());
    bson_append_document_end(&cast_list, &cast_doc);
    idx++;
  }
  bson_append_array_end(new_doc, &cast_list);
  
  idx = 0;
  bson_t thumbnail_id_list;
  BSON_APPEND_ARRAY_BEGIN(new_doc, "thumbnail_ids", &thumbnail_id_list);
  for (auto &thumbnail_id : thumbnail_ids) {
    bson_uint32_to_string(idx, &key, buf, sizeof buf);
    BSON_APPEND_UTF8(&thumbnail_id_list, key, thumbnail_id.c_str());
    idx++;
  }
  bson_append_array_end(new_doc, &thumbnail_id_list);

  idx = 0;
  bson_t photo_id_list;
  BSON_APPEND_ARRAY_BEGIN(new_doc, "photo_ids", &photo_id_list);
  for (auto &photo_id : photo_ids) {
    bson_uint32_to_string(idx, &key, buf, sizeof buf);
    BSON_APPEND_UTF8(&photo_id_list, key, photo_id.c_str());
    idx++;
  }
  bson_append_array_end(new_doc, &photo_id_list);

  idx = 0;
  bson_t video_id_list;
  BSON_APPEND_ARRAY_BEGIN(new_doc, "video_ids", &video_id_list);
  for (auto &video_id : video_ids) {
    bson_uint32_to_string(idx, &key, buf, sizeof buf);
    BSON_APPEND_UTF8(&video_id_list, key, video_id.c_str());
    idx++;
  }
  bson_append_array_end(new_doc, &video_id_list);

  mongoc_client_t *mongodb_client = mongoc_client_pool_pop(
      _mongodb_client_pool);
  if (!mongodb_client) {
    ServiceException se;
    se.errorCode = ErrorCode::SE_MONGODB_ERROR;
    se.message = "Failed to pop a client from MongoDB pool";
    throw se;
  }
  auto collection = mongoc_client_get_collection(
      mongodb_client, "movie-info", "movie-info");
  if (!collection) {
    ServiceException se;
    se.errorCode = ErrorCode::SE_MONGODB_ERROR;
    se.message = "Failed to create collection movie-info from DB movie-info";
    mongoc_client_pool_push(_mongodb_client_pool, mongodb_client);
    throw se;
  }
  bson_error_t error;
  auto insert_span = opentracing::Tracer::Global()->StartSpan(
      "MongoInsertMovieInfo", { opentracing::ChildOf(&span->context()) });
  bool plotinsert = mongoc_collection_insert_one (
      collection, new_doc, nullptr, nullptr, &error);
  insert_span->Finish();
  if (!plotinsert) {
    LOG(error) << "Error: Failed to insert movie-info to MongoDB: "
               << error.message;
    ServiceException se;
    se.errorCode = ErrorCode::SE_MONGODB_ERROR;
    se.message = error.message;
    bson_destroy(new_doc);
    mongoc_collection_destroy(collection);
    mongoc_client_pool_push(_mongodb_client_pool, mongodb_client);
    throw se;
  }

  bson_destroy(new_doc);
  mongoc_collection_destroy(collection);
  mongoc_client_pool_push(_mongodb_client_pool, mongodb_client);

  span->Finish();
}

void MovieInfoHandler::ReadMovieInfo(
    MovieInfo &_return,
    int64_t req_id,
    const std::string &movie_id,
    const std::map<std::string, std::string> &carrier) {

  // Initialize a span
  TextMapReader reader(carrier);
  std::map<std::string, std::string> writer_text_map;
  TextMapWriter writer(writer_text_map);
  auto parent_span = opentracing::Tracer::Global()->Extract(reader);
  auto span = opentracing::Tracer::Global()->StartSpan(
      "ReadMovieInfo",
      { opentracing::ChildOf(parent_span->get()) });
  opentracing::Tracer::Global()->Inject(span->context(), writer);
  
  memcached_return_t memcached_rc;
  memcached_st *memcached_client = memcached_pool_pop(
      _memcached_client_pool, true, &memcached_rc);
  if (!memcached_client) {
    ServiceException se;
    se.errorCode = ErrorCode::SE_MEMCACHED_ERROR;
    se.message = "Failed to pop a client from memcached pool";
    throw se;
  }

  size_t movie_info_mmc_size;
  uint32_t memcached_flags;
  auto get_span = opentracing::Tracer::Global()->StartSpan(
      "MmcGetMovieInfo", { opentracing::ChildOf(&span->context()) });
  char *movie_info_mmc = memcached_get(
      memcached_client,
      movie_id.c_str(),
      movie_id.length(),
      &movie_info_mmc_size,
      &memcached_flags,
      &memcached_rc);
  if (!movie_info_mmc && memcached_rc != MEMCACHED_NOTFOUND) {
    ServiceException se;
    se.errorCode = ErrorCode::SE_MEMCACHED_ERROR;
    se.message = memcached_strerror(memcached_client, memcached_rc);
    memcached_pool_push(_memcached_client_pool, memcached_client);
    throw se;
  }
  memcached_pool_push(_memcached_client_pool, memcached_client);
  get_span->Finish();

  if (movie_info_mmc) {
    LOG(debug) << "Get movie-info " << movie_id << " cache hit from Memcached";
    json movie_info_json = json::parse(std::string(
        movie_info_mmc, movie_info_mmc + movie_info_mmc_size));
    _return.movie_id = movie_info_json["movie_id"];
    _return.title = movie_info_json["title"];
    _return.avg_rating = movie_info_json["avg_rating"];
    _return.num_rating = movie_info_json["num_rating"];
    _return.plot_id = movie_info_json["plot_id"];
    for (auto &item : movie_info_json["photo_ids"]) {
      _return.photo_ids.emplace_back(item);
    }
    for (auto &item : movie_info_json["video_ids"]) {
      _return.video_ids.emplace_back(item);
    }
    for (auto &item : movie_info_json["thumbnail_ids"]) {
      _return.thumbnail_ids.emplace_back(item);
    }
    for (auto &item : movie_info_json["casts"]) {
      Cast new_cast;
      new_cast.cast_id = item["cast_id"];
      new_cast.cast_info_id = item["cast_info_id"];
      new_cast.character = item["character"];
      _return.casts.emplace_back(new_cast);
    }
    free(movie_info_mmc);
  } else {
    // If not cached in memcached
    mongoc_client_t *mongodb_client = mongoc_client_pool_pop(
        _mongodb_client_pool);
    if (!mongodb_client) {
      ServiceException se;
      se.errorCode = ErrorCode::SE_MONGODB_ERROR;
      se.message = "Failed to pop a client from MongoDB pool";
      throw se;
    }

    auto collection = mongoc_client_get_collection(
        mongodb_client, "movie-info", "movie-info");
    if (!collection) {
      ServiceException se;
      se.errorCode = ErrorCode::SE_MONGODB_ERROR;
      se.message = "Failed to create collection user from DB user";
      mongoc_client_pool_push(_mongodb_client_pool, mongodb_client);
      throw se;
    }
    bson_t *query = bson_new();
    BSON_APPEND_UTF8(query, "movie_id", movie_id.c_str());
    auto find_span = opentracing::Tracer::Global()->StartSpan(
        "MongoFindMovieInfo", { opentracing::ChildOf(&span->context()) });
    mongoc_cursor_t *cursor = mongoc_collection_find_with_opts(
        collection, query, nullptr, nullptr);
    const bson_t *doc;
    bool found = mongoc_cursor_next(cursor, &doc);
    find_span->Finish();
    if (!found) {
      bson_error_t error;
      if (mongoc_cursor_error (cursor, &error)) {
        LOG(warning) << error.message;
        bson_destroy(query);
        mongoc_cursor_destroy(cursor);
        mongoc_collection_destroy(collection);
        mongoc_client_pool_push(_mongodb_client_pool, mongodb_client);
        ServiceException se;
        se.errorCode = ErrorCode::SE_MONGODB_ERROR;
        se.message = error.message;
        throw se;
      } else {
        LOG(warning) << "Movie_id: " << movie_id << " doesn't exist in MongoDB";
        bson_destroy(query);
        mongoc_cursor_destroy(cursor);
        mongoc_collection_destroy(collection);
        mongoc_client_pool_push(_mongodb_client_pool, mongodb_client);
        ServiceException se;
        se.errorCode = ErrorCode::SE_THRIFT_HANDLER_ERROR;
        se.message = "Movie_id: " + movie_id + " doesn't exist in MongoDB";
        throw se;
      }
    } else {
      LOG(debug) << "Movie_id: " << movie_id << " found in MongoDB";
      auto movie_info_json_char = bson_as_json(doc, nullptr);
      json movie_info_json = json::parse(movie_info_json_char);
      _return.movie_id = movie_info_json["movie_id"];
      _return.title = movie_info_json["title"];
      _return.avg_rating = movie_info_json["avg_rating"];
      _return.num_rating = movie_info_json["num_rating"];
      _return.plot_id = movie_info_json["plot_id"];
      for (auto &item : movie_info_json["photo_ids"]) {
        _return.photo_ids.emplace_back(item);
      }
      for (auto &item : movie_info_json["video_ids"]) {
        _return.video_ids.emplace_back(item);
      }
      for (auto &item : movie_info_json["thumbnail_ids"]) {
        _return.thumbnail_ids.emplace_back(item);
      }
      for (auto &item : movie_info_json["casts"]) {
        Cast new_cast;
        new_cast.cast_id = item["cast_id"];
        new_cast.cast_info_id = item["cast_info_id"];
        new_cast.character = item["character"];
        _return.casts.emplace_back(new_cast);
      }
      bson_destroy(query);
      mongoc_cursor_destroy(cursor);
      mongoc_collection_destroy(collection);
      mongoc_client_pool_push(_mongodb_client_pool, mongodb_client);

      // upload movie-info to memcached
      memcached_client = memcached_pool_pop(
          _memcached_client_pool, true, &memcached_rc);
      if (!memcached_client) {
        ServiceException se;
        se.errorCode = ErrorCode::SE_MEMCACHED_ERROR;
        se.message = "Failed to pop a client from memcached pool";
        throw se;
      }
      auto set_span = opentracing::Tracer::Global()->StartSpan(
          "MmcSetMovieInfo", { opentracing::ChildOf(&span->context()) });

      memcached_rc = memcached_set(
          memcached_client,
          movie_id.c_str(),
          movie_id.length(),
          movie_info_json_char,
          std::strlen(movie_info_json_char),
          static_cast<time_t>(0),
          static_cast<uint32_t>(0));
      if (memcached_rc != MEMCACHED_SUCCESS) {
        LOG(warning) << "Failed to set movie_info to Memcached: "
                     << memcached_strerror(memcached_client, memcached_rc);
      }
      set_span->Finish();
      bson_free(movie_info_json_char);
      memcached_pool_push(_memcached_client_pool, memcached_client);
    }
  }
  span->Finish();
}

void MovieInfoHandler::UpdateRating(
    int64_t req_id, const std::string& movie_id,
    int32_t sum_uncommitted_rating, int32_t num_uncommitted_rating,
    const std::map<std::string, std::string> & carrier) {
  // Initialize a span
  TextMapReader reader(carrier);
  std::map<std::string, std::string> writer_text_map;
  TextMapWriter writer(writer_text_map);
  auto parent_span = opentracing::Tracer::Global()->Extract(reader);
  auto span = opentracing::Tracer::Global()->StartSpan(
      "UpdateRating",
      { opentracing::ChildOf(parent_span->get()) });
  opentracing::Tracer::Global()->Inject(span->context(), writer);

  bson_t *query = bson_new();
  BSON_APPEND_UTF8(query, "movie_id", movie_id.c_str());

  mongoc_client_t *mongodb_client = mongoc_client_pool_pop(
      _mongodb_client_pool);
  if (!mongodb_client) {
    ServiceException se;
    se.errorCode = ErrorCode::SE_MONGODB_ERROR;
    se.message = "Failed to pop a client from MongoDB pool";
    throw se;
  }
  auto collection = mongoc_client_get_collection(
      mongodb_client, "movie-info", "movie-info");
  if (!collection) {
    ServiceException se;
    se.errorCode = ErrorCode::SE_MONGODB_ERROR;
    se.message = "Failed to create collection movie-info from MongoDB";
    mongoc_client_pool_push(_mongodb_client_pool, mongodb_client);
    throw se;
  }
  auto update_span = opentracing::Tracer::Global()->StartSpan(
      "MongoUpdateRating", {opentracing::ChildOf(&span->context())});

  bson_t *pipeline_update = bson_new();
  bson_t stage_0;
  BSON_APPEND_DOCUMENT_BEGIN(pipeline_update, "0", &stage_0);
  bson_t set_doc;
  BSON_APPEND_DOCUMENT_BEGIN(&stage_0, "$set", &set_doc);

  bson_t avg_expr;
  BSON_APPEND_DOCUMENT_BEGIN(&set_doc, "avg_rating", &avg_expr);
  bson_t divide_arr;
  BSON_APPEND_ARRAY_BEGIN(&avg_expr, "$divide", &divide_arr);
  bson_t numerator;
  BSON_APPEND_DOCUMENT_BEGIN(&divide_arr, "0", &numerator);
  bson_t add_numerator;
  BSON_APPEND_ARRAY_BEGIN(&numerator, "$add", &add_numerator);
  bson_t mul_inner;
  BSON_APPEND_DOCUMENT_BEGIN(&add_numerator, "0", &mul_inner);
  bson_t multiply_arr;
  BSON_APPEND_ARRAY_BEGIN(&mul_inner, "$multiply", &multiply_arr);
  BSON_APPEND_UTF8(&multiply_arr, "0", "$avg_rating");
  BSON_APPEND_UTF8(&multiply_arr, "1", "$num_rating");
  bson_append_array_end(&mul_inner, &multiply_arr);
  bson_append_document_end(&add_numerator, &mul_inner);
  BSON_APPEND_INT32(&add_numerator, "1", sum_uncommitted_rating);
  bson_append_array_end(&numerator, &add_numerator);
  bson_append_document_end(&divide_arr, &numerator);
  bson_t denominator;
  BSON_APPEND_DOCUMENT_BEGIN(&divide_arr, "1", &denominator);
  bson_t add_denom;
  BSON_APPEND_ARRAY_BEGIN(&denominator, "$add", &add_denom);
  BSON_APPEND_UTF8(&add_denom, "0", "$num_rating");
  BSON_APPEND_INT32(&add_denom, "1", num_uncommitted_rating);
  bson_append_array_end(&denominator, &add_denom);
  bson_append_document_end(&divide_arr, &denominator);
  bson_append_array_end(&avg_expr, &divide_arr);
  bson_append_document_end(&set_doc, &avg_expr);

  bson_t num_expr;
  BSON_APPEND_DOCUMENT_BEGIN(&set_doc, "num_rating", &num_expr);
  bson_t add_num;
  BSON_APPEND_ARRAY_BEGIN(&num_expr, "$add", &add_num);
  BSON_APPEND_UTF8(&add_num, "0", "$num_rating");
  BSON_APPEND_INT32(&add_num, "1", num_uncommitted_rating);
  bson_append_array_end(&num_expr, &add_num);
  bson_append_document_end(&set_doc, &num_expr);

  bson_append_document_end(&stage_0, &set_doc);
  bson_append_document_end(pipeline_update, &stage_0);

  bson_error_t error;
  bson_t reply;
  bool updated = mongoc_collection_update_one(
      collection, query, pipeline_update, nullptr, &reply, &error);
  if (!updated) {
    LOG(error) << "Failed to update rating for movie " << movie_id
               << " to MongoDB: " << error.message;
    ServiceException se;
    se.errorCode = ErrorCode::SE_MONGODB_ERROR;
    se.message = "Failed to update rating for movie " + movie_id +
        " to MongoDB: " + error.message;
    bson_destroy(&reply);
    bson_destroy(pipeline_update);
    bson_destroy(query);
    mongoc_collection_destroy(collection);
    mongoc_client_pool_push(_mongodb_client_pool, mongodb_client);
    throw se;
  }
  update_span->Finish();
  bson_destroy(&reply);
  bson_destroy(pipeline_update);
  bson_destroy(query);
  mongoc_collection_destroy(collection);
  mongoc_client_pool_push(_mongodb_client_pool, mongodb_client);

  auto delete_span = opentracing::Tracer::Global()->StartSpan(
      "MmcDelete", {opentracing::ChildOf(&span->context())});
  memcached_return_t memcached_rc;
  memcached_st *memcached_client = memcached_pool_pop(
      _memcached_client_pool, true, &memcached_rc);
  if (!memcached_client) {
    ServiceException se;
    se.errorCode = ErrorCode::SE_MEMCACHED_ERROR;
    se.message = "Failed to pop a client from memcached pool";
    throw se;
  }
  memcached_delete(memcached_client, movie_id.c_str(), movie_id.length(), 0);
  memcached_pool_push(_memcached_client_pool, memcached_client);
  delete_span->Finish();

  span->Finish();
}

} // namespace media_service

#endif //MEDIA_MICROSERVICES_SRC_MOVIEINFOSERVICE_MOVIEINFOHANDLER_H_
