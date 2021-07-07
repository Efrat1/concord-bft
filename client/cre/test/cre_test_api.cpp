// Concord
//
// Copyright (c) 2020 VMware, Inc. All Rights Reserved.
//
// This product is licensed to you under the Apache 2.0 license (the 'License").
// You may not use this product except in compliance with the Apache 2.0 License.
//
// This product may include a number of subcomponents with separate copyright
// notices and license terms. Your use of these subcomponents is subject to the
// terms and conditions of the sub-component's license, as noted in the
// LICENSE file.

#include "gtest/gtest.h"
#include "assertUtils.hpp"
#include "client_reconfiguration_engine.hpp"
#include "cre_interfaces.hpp"
#include <chrono>

using namespace cre;
namespace {
std::string metrics_component = "client_reconfiguration_engine";
std::string invalids_counter = "invalid_handlers";
std::string errors_counter = "errored_handlers";
class TestStateClient : public IStateClient {
 public:
  State getNextState(uint64_t lastKnownBlockId) const override {
    std::string lastKnownBid = std::to_string(lastKnownBlockId + 1);
    return State{lastKnownBlockId + 1, std::vector<uint8_t>(lastKnownBid.begin(), lastKnownBid.end())};
  }
  State getLatestClientUpdate(uint16_t clientId) const override { return {0, {}}; }
  bool updateStateOnChain(const WriteState& state) override {
    blocks_.push_back(blocks_.size() + 1);
    return true;
  }
  void start(uint64_t lastKnownBlock) override {}
  void stop() override {}
  std::vector<uint64_t> blocks_;
};

class TestExecuteHandler : public IStateHandler {
 public:
  bool validate(const State&) const override { return true; }
  bool execute(const State& state, WriteState&) override {
    std::string newBid(state.data.begin(), state.data.end());
    lastKnownState = state.blockid;
    return true;
  }
  uint64_t lastKnownState{0};
};

class TestPersistOnChainHandler : public IStateHandler {
 public:
  bool validate(const State&) const override { return true; }
  bool execute(const State& state, WriteState& out) override {
    out = {state.data, nullptr};
    return true;
  }
};

class TestInvalidHandler : public IStateHandler {
 public:
  bool validate(const State&) const override { return false; }
  bool execute(const State& state, WriteState&) override { return true; }
};

class TestErroredHandler : public IStateHandler {
 public:
  bool validate(const State&) const override { return true; }
  bool execute(const State& state, WriteState&) override { return false; }
};
Config c{0, 10};

TEST(test_client_reconfiguration_engine, test_normal_start_and_shutdown) {
  ASSERT_NO_THROW(
      ClientReconfigurationEngine(c, new TestStateClient(), std::make_shared<concordMetrics::Aggregator>()));
  ClientReconfigurationEngine cre(c, new TestStateClient(), std::make_shared<concordMetrics::Aggregator>());
  cre.start();
  std::this_thread::sleep_for(std::chrono::seconds(1));
  cre.stop();
}

TEST(test_client_reconfiguration_engine, test_invalid_handler) {
  std::shared_ptr<concordMetrics::Aggregator> aggregator = std::make_shared<concordMetrics::Aggregator>();
  ClientReconfigurationEngine cre(c, new TestStateClient(), aggregator);
  cre.registerHandler(std::make_shared<TestInvalidHandler>());
  cre.start();
  std::this_thread::sleep_for(std::chrono::seconds(1));
  ASSERT_GT(aggregator->GetCounter(metrics_component, invalids_counter).Get(), 1);
  cre.stop();
}

TEST(test_client_reconfiguration_engine, test_errored_handler) {
  std::shared_ptr<concordMetrics::Aggregator> aggregator = std::make_shared<concordMetrics::Aggregator>();
  ClientReconfigurationEngine cre(c, new TestStateClient(), aggregator);
  cre.registerHandler(std::make_shared<TestErroredHandler>());
  cre.start();
  std::this_thread::sleep_for(std::chrono::seconds(1));
  ASSERT_GT(aggregator->GetCounter(metrics_component, errors_counter).Get(), 1);
  cre.stop();
}

TEST(test_client_reconfiguration_engine, test_cre) {
  std::shared_ptr<concordMetrics::Aggregator> aggregator = std::make_shared<concordMetrics::Aggregator>();
  TestStateClient* sc = new TestStateClient();
  TestStateClient& rsc = *sc;
  std::shared_ptr<ClientReconfigurationEngine> cre = std::make_shared<ClientReconfigurationEngine>(c, sc, aggregator);
  std::shared_ptr<TestExecuteHandler> handler = std::make_shared<TestExecuteHandler>();
  std::shared_ptr<TestPersistOnChainHandler> chainHandler = std::make_shared<TestPersistOnChainHandler>();
  cre->registerHandler(handler);
  cre->registerHandler(chainHandler);
  cre->start();
  std::this_thread::sleep_for(std::chrono::seconds(1));
  ASSERT_EQ(aggregator->GetCounter(metrics_component, errors_counter).Get(), 0);
  ASSERT_EQ(aggregator->GetCounter(metrics_component, invalids_counter).Get(), 0);
  ASSERT_GT(handler->lastKnownState, 0);
  ASSERT_GT(rsc.blocks_.size(), 0);
  cre->stop();
}
}  // namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}