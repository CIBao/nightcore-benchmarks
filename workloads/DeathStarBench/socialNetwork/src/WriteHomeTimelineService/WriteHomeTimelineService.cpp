
#include <csignal>
#include <hiredis/hiredis.h>
#include <mutex>
#include <thread>
#include <sstream>
#include <set>

#include "../AmqpLibeventHandler.h"
#include "../ClientPool.h"
#include "../RedisClient.h"
#include "../ThriftClient.h"
#include "../logger.h"
#include "../tracing.h"
#include "../utils.h"
#include "../../gen-cpp/social_network_types.h"
#include "../../gen-cpp/SocialGraphService.h"

#define NUM_WORKERS 4

using namespace social_network;

static std::exception_ptr _teptr;
static ClientPool<RedisClient> *_redis_client_pool;
static ClientPool<ThriftClient<SocialGraphServiceClient>>
    *_social_graph_client_pool;
static int max_followers_to_update = -1;

void sigintHandler(int sig) {
  exit(EXIT_SUCCESS);
}

void OnReceivedWorker(const AMQP::Message &msg) {
  try {
    json msg_json = json::parse(std::string(msg.body(), msg.bodySize()));

    std::map<std::string, std::string> carrier;
    for (auto it = msg_json["carrier"].begin();
        it != msg_json["carrier"].end(); ++it) {
      carrier.emplace(std::make_pair(it.key(), it.value()));
    }

    // Jaeger tracing
    TextMapReader span_reader(carrier);
    auto parent_span = opentracing::Tracer::Global()->Extract(span_reader);
    auto span = opentracing::Tracer::Global()->StartSpan(
        "FanoutHomeTimelines",
        {opentracing::ChildOf(parent_span->get())});
    std::map<std::string, std::string> writer_text_map;
    TextMapWriter writer(writer_text_map);
    opentracing::Tracer::Global()->Inject(span->context(), writer);

    // Extract information from rabbitmq messages
    int64_t user_id = msg_json["user_id"];
    int64_t req_id = msg_json["req_id"];
    int64_t post_id = msg_json["post_id"];
    int64_t timestamp = msg_json["timestamp"];
    std::vector<int64_t> user_mentions_id = msg_json["user_mentions_id"];

    // Find followers of the user
    auto social_graph_client_wrapper = _social_graph_client_pool->Pop();
    if (!social_graph_client_wrapper) {
      ServiceException se;
      se.errorCode = ErrorCode::SE_THRIFT_CONN_ERROR;
      se.message = "Failed to connect to social-graph-service";
      throw se;
    }
    auto social_graph_client = social_graph_client_wrapper->GetClient();
    std::vector<int64_t> followers_id;
    {
      auto rpc_trace_guard = _social_graph_client_pool->StartRpcTrace("GetFollowers", social_graph_client_wrapper);
      try {
        social_graph_client->GetFollowers(followers_id, req_id, user_id,
                                          writer_text_map);
      } catch (...) {
        rpc_trace_guard->set_status(1);
        LOG(error) << "Failed to get followers from social-network-service";
        _social_graph_client_pool->Remove(social_graph_client_wrapper);
        throw;
      }
    }
    _social_graph_client_pool->Push(social_graph_client_wrapper);

    std::set<int64_t> followers_id_set(followers_id.begin(),
        followers_id.end());
    followers_id_set.insert(user_mentions_id.begin(), user_mentions_id.end());

    if (max_followers_to_update != -1 && followers_id_set.size() > max_followers_to_update) {
      std::vector<int64_t> tmp(followers_id_set.begin(), followers_id_set.end());
      followers_id_set.clear();
      std::random_shuffle(tmp.begin(), tmp.end());
      for (int i = 0; i < max_followers_to_update; i++) {
        followers_id_set.insert(tmp[i]);
      }
    }

    // Update Redis ZSet
    auto redis_span = opentracing::Tracer::Global()->StartSpan(
        "RedisUpdate", {opentracing::ChildOf(&span->context())});
    auto redis_client_wrapper = _redis_client_pool->Pop();
    if (!redis_client_wrapper) {
      ServiceException se;
      se.errorCode = ErrorCode::SE_REDIS_ERROR;
      se.message = "Cannot connect to Redis server";
      throw se;
    }
    auto redis_client = redis_client_wrapper->GetClient();

    for (auto &follower_id : followers_id_set) {
      redis_client.AppendCommand("ZADD %" PRId64 " NX %" PRId64 " %" PRId64, follower_id, timestamp, post_id);
    }
    for (size_t i = 0; i < followers_id_set.size(); i++) {
      auto reply = redis_client.GetReply();
      reply->check_ok();
    }
    redis_span->Finish();
    _redis_client_pool->Push(redis_client_wrapper);
  } catch (...) {
    LOG(error) << "OnReveived worker error";
    throw;
  }
}

void HeartbeatSend(AmqpLibeventHandler &handler,
    AMQP::TcpConnection &connection, int interval){
  while(handler.GetIsRunning()){
    LOG(debug) << "Heartbeat sent";
    connection.heartbeat();
    sleep(interval);
  }
}

void WorkerThread(std::string &addr, int port) {
  AmqpLibeventHandler handler;
  AMQP::TcpConnection connection(handler, AMQP::Address(
      addr, port, AMQP::Login("guest", "guest"), "/"));
  AMQP::TcpChannel channel(&connection);
  channel.onError(
      [&handler](const char *message) {
        LOG(error) << "Channel error: " << message;
        handler.Stop();
      });
  channel.declareQueue("write-home-timeline", AMQP::durable).onSuccess(
      [&connection](const std::string &name, uint32_t messagecount,
                    uint32_t consumercount) {
        LOG(debug) << "Created queue: " << name;
      });
  channel.consume("write-home-timeline", AMQP::noack).onReceived(
      [](const AMQP::Message &msg, uint64_t tag, bool redelivered) {
        LOG(debug) << "Received: " << std::string(msg.body(), msg.bodySize());
        OnReceivedWorker(msg);
      });


  std::thread heartbeat_thread(HeartbeatSend, std::ref(handler),
      std::ref(connection), 30);
  heartbeat_thread.detach();
  handler.Start();
  LOG(debug) << "Closing connection.";
  connection.close();
}

int main(int argc, char *argv[]) {
  signal(SIGINT, sigintHandler);
  init_logger();

  const char* max_followers_to_update_str = getenv("MAX_FOLLOWERS_TO_UPDATE");
  if (max_followers_to_update_str != nullptr) {
    max_followers_to_update = atoi(max_followers_to_update_str);
    LOG(info) << "MAX_FOLLOWERS_TO_UPDATE=" << max_followers_to_update;
  }

  SetUpTracer("config/jaeger-config.yml", "write-home-timeline-service");

  json config_json;
  if (load_config(&config_json) != 0) {
    exit(EXIT_FAILURE);
  }

  int port = 9090;

  std::string rabbitmq_addr =
      config_json["write-home-timeline-rabbitmq"]["addr"];
  int rabbitmq_port = config_json["write-home-timeline-rabbitmq"]["port"];

  std::string redis_addr =
      config_json["home-timeline-redis"]["addr"];
  int redis_port = config_json["home-timeline-redis"]["port"];

  std::string social_graph_service_addr =
      config_json["social-graph-service"]["addr"];
  int social_graph_service_port = config_json["social-graph-service"]["port"];
  std::string social_graph_service_http_path = config_json["social-graph-service"]["http_path"];

  ClientPool<RedisClient> redis_client_pool("redis", redis_addr, redis_port,
                                            0, config_json["write-home-timeline-service"]["redis_client_pool_size"], 1000);

  ClientPool<ThriftClient<SocialGraphServiceClient>>
      social_graph_client_pool(
          "social-graph-service", social_graph_service_addr,
          social_graph_service_port, 0, config_json["write-home-timeline-service"]["social_graph_client_pool_size"], 1000,
          social_graph_service_http_path, nullptr, "WriteHomeTimelineService", "SocialGraphService");

  _redis_client_pool = &redis_client_pool;
  _social_graph_client_pool = &social_graph_client_pool;

  std::unique_ptr<std::thread> threads_ptr[NUM_WORKERS];
  for (auto & thread_ptr : threads_ptr) {
    thread_ptr = std::make_unique<std::thread>(
        WorkerThread, std::ref(rabbitmq_addr), rabbitmq_port);
  }
  for (auto &thread_ptr : threads_ptr) {
    thread_ptr->join();
    if (_teptr) {
      try{
        std::rethrow_exception(_teptr);
      }
      catch(const std::exception &ex)
      {
        LOG(error) << "Thread exited with exception: " << ex.what();
      }
    }
  }



  return 0;
}