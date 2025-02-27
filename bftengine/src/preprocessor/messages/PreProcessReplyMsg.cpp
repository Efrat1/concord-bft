// Concord
//
// Copyright (c) 2019-2021 VMware, Inc. All Rights Reserved.
//
// This product is licensed to you under the Apache 2.0 license (the "License"). You may not use this product except in
// compliance with the Apache 2.0 License.
//
// This product may include a number of subcomponents with separate copyright notices and license terms. Your use of
// these subcomponents is subject to the terms and conditions of the sub-component's license, as noted in the LICENSE
// file.

#include "PreProcessReplyMsg.hpp"
#include "ReplicaConfig.hpp"
#include "assertUtils.hpp"
#include "SigManager.hpp"

namespace preprocessor {

using namespace std;
using namespace concord::util;
using namespace bftEngine;
using namespace bftEngine::impl;

// maxReplyMsgSize_ = sizeof(Header) + sizeof(signature) + reqCid.size(), i.e 58 + 256 + up to 710 bytes of reqCid
uint16_t PreProcessReplyMsg::maxReplyMsgSize_ = 1024;

PreProcessReplyMsg::PreProcessReplyMsg(NodeIdType senderId,
                                       uint16_t clientId,
                                       uint16_t reqOffsetInBatch,
                                       uint64_t reqSeqNum,
                                       uint64_t reqRetryId,
                                       const char* preProcessResultBuf,
                                       uint32_t preProcessResultBufLen,
                                       const std::string& reqCid,
                                       ReplyStatus status,
                                       OperationResult preProcessResult,
                                       ViewNum viewNum)
    : MessageBase(senderId, MsgCode::PreProcessReply, 0, maxReplyMsgSize_) {
  setParams(senderId, clientId, reqOffsetInBatch, reqSeqNum, reqRetryId, status, preProcessResult, viewNum);
  setupMsgBody(preProcessResultBuf, preProcessResultBufLen, reqCid);
}

// Used by PreProcessBatchReplyMsg while retrieving PreProcessReplyMsgs from the batch
PreProcessReplyMsg::PreProcessReplyMsg(NodeIdType senderId,
                                       uint16_t clientId,
                                       uint16_t reqOffsetInBatch,
                                       uint64_t reqSeqNum,
                                       uint64_t reqRetryId,
                                       const uint8_t* resultsHash,
                                       const char* signature,
                                       const std::string& reqCid,
                                       ReplyStatus status,
                                       OperationResult preProcessResult,
                                       ViewNum viewNum)
    : MessageBase(senderId, MsgCode::PreProcessReply, 0, maxReplyMsgSize_) {
  setParams(senderId, clientId, reqOffsetInBatch, reqSeqNum, reqRetryId, status, preProcessResult, viewNum);
  setupMsgBody(resultsHash, signature, reqCid);
}

void PreProcessReplyMsg::validate(const ReplicasInfo& repInfo) const {
  const uint64_t headerSize = sizeof(Header);
  if (size() < headerSize || size() < headerSize + msgBody()->replyLength) throw runtime_error(__PRETTY_FUNCTION__);

  if (type() != MsgCode::PreProcessReply) {
    LOG_WARN(logger(), "Message type is incorrect" << KVLOG(type()));
    throw std::runtime_error(__PRETTY_FUNCTION__);
  }

  auto& msgHeader = *msgBody();
  if (msgHeader.senderId == repInfo.myId()) {
    LOG_WARN(logger(), "Message sender is invalid" << KVLOG(senderId()));
    throw std::runtime_error(__PRETTY_FUNCTION__);
  }

  auto sigManager = SigManager::instance();
  uint16_t sigLen = sigManager->getSigLength(msgHeader.senderId);
  if (msgHeader.status == STATUS_GOOD) {
    if (size() < (sizeof(Header) + sigLen)) {
      LOG_WARN(logger(),
               "Message size is too small" << KVLOG(
                   msgHeader.senderId, msgHeader.clientId, msgHeader.reqSeqNum, size(), sizeof(Header) + sigLen));
      throw runtime_error(__PRETTY_FUNCTION__ + string(": Message size is too small"));
    }
    concord::diagnostics::TimeRecorder scoped_timer(*preProcessorHistograms_->verifyPreProcessReplySig);
    if (!sigManager->verifySig(msgHeader.senderId,
                               msgBody()->resultsHash,
                               concord::crypto::openssl::SHA3_256::SIZE_IN_BYTES,
                               reinterpret_cast<concord::Byte*>(msgBody()) + headerSize,
                               sigLen))
      throw runtime_error(__PRETTY_FUNCTION__ + string(": verifySig failed"));
  }
}  // namespace preprocessor

std::vector<uint8_t> PreProcessReplyMsg::getResultHashSignature() const {
  const uint64_t headerSize = sizeof(Header);
  const auto& msgHeader = *msgBody();
  auto sigManager = SigManager::instance();
  uint16_t sigLen = sigManager->getSigLength(msgHeader.senderId);

  return std::vector<uint8_t>((uint8_t*)msgBody() + headerSize, (uint8_t*)msgBody() + headerSize + sigLen);
}

void PreProcessReplyMsg::setParams(NodeIdType senderId,
                                   uint16_t clientId,
                                   uint16_t reqOffsetInBatch,
                                   ReqId reqSeqNum,
                                   uint64_t reqRetryId,
                                   ReplyStatus status,
                                   OperationResult preProcessResult,
                                   ViewNum viewNum) {
  msgBody()->senderId = senderId;
  msgBody()->reqSeqNum = reqSeqNum;
  msgBody()->clientId = clientId;
  msgBody()->reqOffsetInBatch = reqOffsetInBatch;
  msgBody()->reqRetryId = reqRetryId;
  msgBody()->status = status;
  msgBody()->preProcessResult = preProcessResult;
  msgBody()->viewNum = viewNum;
  LOG_DEBUG(logger(),
            KVLOG(senderId, clientId, reqSeqNum, reqRetryId, status, static_cast<uint32_t>(preProcessResult)));
}

void PreProcessReplyMsg::setLeftMsgParams(const string& reqCid, uint16_t sigSize) {
  const uint16_t headerSize = sizeof(Header);
  msgBody()->cidLength = reqCid.size();
  memcpy(body() + headerSize + sigSize, reqCid.c_str(), reqCid.size());
  msgBody()->replyLength = sigSize;
  msgSize_ = headerSize + sigSize + msgBody()->cidLength;
  LOG_DEBUG(logger(),
            KVLOG(msgBody()->senderId,
                  msgBody()->clientId,
                  msgBody()->reqSeqNum,
                  reqCid,
                  msgBody()->reqOffsetInBatch,
                  msgBody()->reqRetryId,
                  msgBody()->status,
                  static_cast<uint32_t>(msgBody()->preProcessResult),
                  sigSize,
                  msgSize_));
}

void PreProcessReplyMsg::setupMsgBody(const char* preProcessResultBuf,
                                      uint32_t preProcessResultBufLen,
                                      const string& reqCid) {
  uint16_t sigSize = 0;
  auto sigManager = SigManager::instance();
  sigSize = sigManager->getMySigLength();
  // Calculate pre-process result hash
  auto hash = PreProcessResultHashCreator::create(preProcessResultBuf,
                                                  preProcessResultBufLen,
                                                  msgBody()->preProcessResult,
                                                  msgBody()->clientId,
                                                  msgBody()->reqSeqNum);
  memcpy(msgBody()->resultsHash, hash.data(), concord::crypto::openssl::SHA3_256::SIZE_IN_BYTES);
  {
    concord::diagnostics::TimeRecorder scoped_timer(*preProcessorHistograms_->signPreProcessReplyHash);
    sigManager->sign(hash.data(),
                     concord::crypto::openssl::SHA3_256::SIZE_IN_BYTES,
                     reinterpret_cast<concord::Byte*>(body() + sizeof(Header)));
  }
  setLeftMsgParams(reqCid, sigSize);
}

// Used by PreProcessBatchReplyMsg while retrieving PreProcessReplyMsgs from the batch
void PreProcessReplyMsg::setupMsgBody(const uint8_t* resultsHash, const char* signature, const string& reqCid) {
  memcpy(msgBody()->resultsHash, resultsHash, concord::crypto::openssl::SHA3_256::SIZE_IN_BYTES);
  const uint16_t sigLen = SigManager::instance()->getMySigLength();
  memcpy(body() + sizeof(Header), signature, sigLen);
  setLeftMsgParams(reqCid, sigLen);
}

std::string PreProcessReplyMsg::getCid() const {
  return std::string(body() + msgSize_ - msgBody()->cidLength, msgBody()->cidLength);
}

}  // namespace preprocessor
