// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mojo/core/message_pipe_dispatcher.h"

#include <limits>
#include <memory>

#include "base/logging.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "mojo/core/core.h"
#include "mojo/core/node_controller.h"
#include "mojo/core/ports/event.h"
#include "mojo/core/ports/message_filter.h"
#include "mojo/core/request_context.h"
#include "mojo/core/user_message_impl.h"

namespace mojo {
namespace core {

namespace {

#pragma pack(push, 1)

struct SerializedState {
  uint64_t pipe_id;
  int8_t endpoint;
  char padding[7];
};

static_assert(sizeof(SerializedState) % 8 == 0,
              "Invalid SerializedState size.");

#pragma pack(pop)

}  // namespace

// A SlotObserver which forwards to a MessagePipeDispatcher. This owns a
// reference to the MPD to ensure it lives as long as the observed port.
class MessagePipeDispatcher::SlotObserverThunk
    : public NodeController::SlotObserver {
 public:
  explicit SlotObserverThunk(scoped_refptr<MessagePipeDispatcher> dispatcher)
      : dispatcher_(dispatcher) {}

 private:
  ~SlotObserverThunk() override {}

  // NodeController::SlotObserver:
  void OnSlotStatusChanged() override { dispatcher_->OnSlotStatusChanged(); }

  scoped_refptr<MessagePipeDispatcher> dispatcher_;

  DISALLOW_COPY_AND_ASSIGN(SlotObserverThunk);
};

#if DCHECK_IS_ON()

// A MessageFilter which never matches a message. Used to peek at the size of
// the next available message on a port, for debug logging only.
class PeekSizeMessageFilter : public ports::MessageFilter {
 public:
  PeekSizeMessageFilter() {}
  ~PeekSizeMessageFilter() override {}

  // ports::MessageFilter:
  bool Match(const ports::UserMessageEvent& message_event) override {
    const auto* message = message_event.GetMessage<UserMessageImpl>();
    if (message->IsSerialized())
      message_size_ = message->user_payload_size();
    return false;
  }

  size_t message_size() const { return message_size_; }

 private:
  size_t message_size_ = 0;

  DISALLOW_COPY_AND_ASSIGN(PeekSizeMessageFilter);
};

#endif  // DCHECK_IS_ON()

MessagePipeDispatcher::MessagePipeDispatcher(NodeController* node_controller,
                                             const ports::PortRef& port,
                                             uint64_t pipe_id,
                                             int endpoint)
    : MessagePipeDispatcher(node_controller,
                            ports::SlotRef(port, ports::kDefaultSlotId),
                            pipe_id,
                            endpoint) {}

MessagePipeDispatcher::MessagePipeDispatcher(NodeController* node_controller,
                                             const ports::SlotRef& slot,
                                             uint64_t pipe_id,
                                             int endpoint)
    : node_controller_(node_controller),
      pipe_id_(pipe_id),
      endpoint_(endpoint),
      slot_(slot),
      watchers_(this) {
  DVLOG(2) << "Creating new MessagePipeDispatcher for slot "
           << slot.port().name() << "/" << slot.slot_id()
           << " [pipe_id=" << pipe_id << "; endpoint=" << endpoint << "]";

  node_controller_->SetSlotObserver(
      slot_, base::MakeRefCounted<SlotObserverThunk>(this));
}

bool MessagePipeDispatcher::Fuse(MessagePipeDispatcher* other) {
  ports::SlotRef slot0;
  {
    base::AutoLock lock(signal_lock_);
    node_controller_->SetSlotObserver(slot_, nullptr);
    slot0 = slot_;
    port_closed_.Set(true);
    watchers_.NotifyClosed();
  }

  ports::SlotRef slot1;
  {
    base::AutoLock lock(other->signal_lock_);
    node_controller_->SetSlotObserver(other->slot_, nullptr);
    slot1 = other->slot_;
    other->port_closed_.Set(true);
    other->watchers_.NotifyClosed();
  }

  if (slot0.slot_id() != ports::kDefaultSlotId ||
      slot1.slot_id() != ports::kDefaultSlotId) {
    return false;
  }

  // Both ports are always closed by this call.
  int rv = node_controller_->MergeLocalPorts(slot0.port(), slot1.port());
  return rv == ports::OK;
}

Dispatcher::Type MessagePipeDispatcher::GetType() const {
  return Type::MESSAGE_PIPE;
}

MojoResult MessagePipeDispatcher::Close() {
  base::AutoLock lock(signal_lock_);
  DVLOG(2) << "Closing message pipe " << pipe_id_ << " endpoint " << endpoint_
           << " [port=" << slot_.port().name() << "]";
  return CloseNoLock();
}

MojoResult MessagePipeDispatcher::WriteMessage(
    std::unique_ptr<ports::UserMessageEvent> message) {
  if (port_closed_ || in_transit_)
    return MOJO_RESULT_INVALID_ARGUMENT;

  ports::SlotRef slot;
  {
    base::AutoLock lock(signal_lock_);
    slot = slot_;
  }

  auto* user_message_impl = message->GetMessage<UserMessageImpl>();
  user_message_impl->PrepareSplicedHandles(slot.port());
  int rv = node_controller_->SendUserMessage(slot, std::move(message));
  DVLOG(4) << "Sent message on pipe " << pipe_id_ << " endpoint " << endpoint_
           << " [port=" << slot.port().name() << "/" << slot.slot_id()
           << "; rv=" << rv << "]";

  if (rv != ports::OK) {
    if (rv == ports::ERROR_PORT_UNKNOWN ||
        rv == ports::ERROR_PORT_STATE_UNEXPECTED ||
        rv == ports::ERROR_PORT_CANNOT_SEND_PEER) {
      return MOJO_RESULT_INVALID_ARGUMENT;
    } else if (rv == ports::ERROR_PORT_PEER_CLOSED) {
      return MOJO_RESULT_FAILED_PRECONDITION;
    }

    NOTREACHED();
    return MOJO_RESULT_UNKNOWN;
  }

  return MOJO_RESULT_OK;
}

MojoResult MessagePipeDispatcher::ReadMessage(
    std::unique_ptr<ports::UserMessageEvent>* message) {
  // We can't read from a port that's closed or in transit!
  if (port_closed_ || in_transit_)
    return MOJO_RESULT_INVALID_ARGUMENT;

  ports::SlotRef slot;
  {
    base::AutoLock lock(signal_lock_);
    slot = slot_;
  }

  int rv = node_controller_->node()->GetMessage(slot, message, nullptr);
  if (rv != ports::OK && rv != ports::ERROR_PORT_PEER_CLOSED) {
    if (rv == ports::ERROR_PORT_UNKNOWN ||
        rv == ports::ERROR_PORT_STATE_UNEXPECTED)
      return MOJO_RESULT_INVALID_ARGUMENT;

    NOTREACHED();
    return MOJO_RESULT_UNKNOWN;
  }

  if (!*message) {
    // No message was available in queue.
    if (rv == ports::OK)
      return MOJO_RESULT_SHOULD_WAIT;
    // Peer is closed and there are no more messages to read.
    DCHECK_EQ(rv, ports::ERROR_PORT_PEER_CLOSED);
    return MOJO_RESULT_FAILED_PRECONDITION;
  }

  // We may need to update anyone watching our signals in case we just read the
  // last available message.
  base::AutoLock lock(signal_lock_);
  watchers_.NotifyState(GetHandleSignalsStateNoLock());
  return MOJO_RESULT_OK;
}

MojoResult MessagePipeDispatcher::SetQuota(MojoQuotaType type, uint64_t limit) {
  switch (type) {
    case MOJO_QUOTA_TYPE_RECEIVE_QUEUE_LENGTH:
      if (limit == MOJO_QUOTA_LIMIT_NONE)
        receive_queue_length_limit_.reset();
      else
        receive_queue_length_limit_ = limit;
      break;

    case MOJO_QUOTA_TYPE_RECEIVE_QUEUE_MEMORY_SIZE:
      if (limit == MOJO_QUOTA_LIMIT_NONE)
        receive_queue_memory_size_limit_.reset();
      else
        receive_queue_memory_size_limit_ = limit;
      break;

    default:
      return MOJO_RESULT_INVALID_ARGUMENT;
  }

  return MOJO_RESULT_OK;
}

MojoResult MessagePipeDispatcher::QueryQuota(MojoQuotaType type,
                                             uint64_t* limit,
                                             uint64_t* usage) {
  base::AutoLock lock(signal_lock_);
  ports::PortStatus port_status;
  if (node_controller_->node()->GetStatus(slot_, &port_status) != ports::OK) {
    CHECK(in_transit_ || port_transferred_ || port_closed_);
    return MOJO_RESULT_INVALID_ARGUMENT;
  }

  switch (type) {
    case MOJO_QUOTA_TYPE_RECEIVE_QUEUE_LENGTH:
      *limit = receive_queue_length_limit_.value_or(MOJO_QUOTA_LIMIT_NONE);
      *usage = port_status.queued_message_count;
      break;

    case MOJO_QUOTA_TYPE_RECEIVE_QUEUE_MEMORY_SIZE:
      *limit = receive_queue_memory_size_limit_.value_or(MOJO_QUOTA_LIMIT_NONE);
      *usage = port_status.queued_num_bytes;
      break;

    default:
      return MOJO_RESULT_INVALID_ARGUMENT;
  }

  return MOJO_RESULT_OK;
}

HandleSignalsState MessagePipeDispatcher::GetHandleSignalsState() const {
  base::AutoLock lock(signal_lock_);
  return GetHandleSignalsStateNoLock();
}

MojoResult MessagePipeDispatcher::AddWatcherRef(
    const scoped_refptr<WatcherDispatcher>& watcher,
    uintptr_t context) {
  base::AutoLock lock(signal_lock_);
  if (port_closed_ || in_transit_)
    return MOJO_RESULT_INVALID_ARGUMENT;
  return watchers_.Add(watcher, context, GetHandleSignalsStateNoLock());
}

MojoResult MessagePipeDispatcher::RemoveWatcherRef(WatcherDispatcher* watcher,
                                                   uintptr_t context) {
  base::AutoLock lock(signal_lock_);
  if (port_closed_ || in_transit_)
    return MOJO_RESULT_INVALID_ARGUMENT;
  return watchers_.Remove(watcher, context);
}

void MessagePipeDispatcher::StartSerialize(uint32_t* num_bytes,
                                           uint32_t* num_ports,
                                           uint32_t* num_handles) {
  *num_bytes = static_cast<uint32_t>(sizeof(SerializedState));
  *num_ports = 1;
  *num_handles = 0;
}

bool MessagePipeDispatcher::EndSerialize(
    void* destination,
    ports::UserMessageEvent::PortAttachment* ports,
    PlatformHandle* handles) {
  base::AutoLock lock(signal_lock_);
  if (slot_.slot_id() != ports::kDefaultSlotId)
    return false;

  SerializedState* state = static_cast<SerializedState*>(destination);
  state->pipe_id = pipe_id_;
  state->endpoint = static_cast<int8_t>(endpoint_);
  memset(state->padding, 0, sizeof(state->padding));
  ports[0].name = slot_.port().name();
  ports[0].slot_id = ports::kDefaultSlotId;
  return true;
}

bool MessagePipeDispatcher::BeginTransit() {
  base::AutoLock lock(signal_lock_);
  if (in_transit_ || port_closed_)
    return false;
  in_transit_.Set(true);
  return in_transit_;
}

void MessagePipeDispatcher::CompleteTransitAndClose() {
  base::AutoLock lock(signal_lock_);
  node_controller_->SetSlotObserver(slot_, nullptr);
  port_transferred_ = true;
  in_transit_.Set(false);
  CloseNoLock();
}

void MessagePipeDispatcher::CancelTransit() {
  base::AutoLock lock(signal_lock_);
  in_transit_.Set(false);

  // Something may have happened while we were waiting for potential transit.
  watchers_.NotifyState(GetHandleSignalsStateNoLock());
}

// static
scoped_refptr<Dispatcher> MessagePipeDispatcher::Deserialize(
    const void* data,
    size_t num_bytes,
    const ports::UserMessageEvent::PortAttachment* ports,
    size_t num_ports,
    PlatformHandle* handles,
    size_t num_handles) {
  if (num_ports != 1 || num_handles || num_bytes != sizeof(SerializedState))
    return nullptr;

  const SerializedState* state = static_cast<const SerializedState*>(data);

  ports::Node* node = Core::Get()->GetNodeController()->node();
  ports::PortRef port;
  if (node->GetPort(ports[0].name, &port))
    return nullptr;

  ports::SlotRef slot(port, ports[0].slot_id.value_or(ports::kDefaultSlotId));
  ports::SlotStatus status;
  if (node->GetStatus(slot, &status) != ports::OK)
    return nullptr;

  return new MessagePipeDispatcher(Core::Get()->GetNodeController(), slot,
                                   state->pipe_id, state->endpoint);
}

scoped_refptr<MessagePipeDispatcher> MessagePipeDispatcher::GetLocalPeer() {
  base::AutoLock lock(signal_lock_);
  return local_peer_;
}

void MessagePipeDispatcher::SetLocalPeer(
    scoped_refptr<MessagePipeDispatcher> peer) {
  base::AutoLock lock(signal_lock_);
  local_peer_ = std::move(peer);
}

void MessagePipeDispatcher::BindToSlot(const ports::SlotRef& slot_ref) {
  base::AutoLock lock(signal_lock_);
  node_controller_->SetSlotObserver(slot_, nullptr);
  slot_ = slot_ref;
  watchers_.NotifyState(GetHandleSignalsStateNoLock());
  node_controller_->SetSlotObserver(
      slot_, base::MakeRefCounted<SlotObserverThunk>(this));
}

MessagePipeDispatcher::~MessagePipeDispatcher() = default;

MojoResult MessagePipeDispatcher::CloseNoLock() {
  signal_lock_.AssertAcquired();
  if (port_closed_ || in_transit_)
    return MOJO_RESULT_INVALID_ARGUMENT;

  port_closed_.Set(true);
  watchers_.NotifyClosed();

  if (!port_transferred_) {
    ports::SlotRef slot = slot_;
    base::AutoUnlock unlock(signal_lock_);
    node_controller_->ClosePortSlot(slot);
  }

  return MOJO_RESULT_OK;
}

HandleSignalsState MessagePipeDispatcher::GetHandleSignalsStateNoLock() const {
  HandleSignalsState rv;

  ports::PortStatus port_status;
  if (node_controller_->node()->GetStatus(slot_, &port_status) != ports::OK) {
    CHECK(in_transit_ || port_transferred_ || port_closed_);
    return HandleSignalsState();
  }

  if (port_status.has_messages) {
    rv.satisfied_signals |= MOJO_HANDLE_SIGNAL_READABLE;
    rv.satisfiable_signals |= MOJO_HANDLE_SIGNAL_READABLE;
  }
  if (port_status.receiving_messages)
    rv.satisfiable_signals |= MOJO_HANDLE_SIGNAL_READABLE;
  if (!port_status.peer_closed) {
    rv.satisfied_signals |= MOJO_HANDLE_SIGNAL_WRITABLE;
    rv.satisfiable_signals |= MOJO_HANDLE_SIGNAL_WRITABLE;
    rv.satisfiable_signals |= MOJO_HANDLE_SIGNAL_READABLE;
    rv.satisfiable_signals |= MOJO_HANDLE_SIGNAL_PEER_REMOTE;
    if (port_status.peer_remote)
      rv.satisfied_signals |= MOJO_HANDLE_SIGNAL_PEER_REMOTE;
  } else {
    rv.satisfied_signals |= MOJO_HANDLE_SIGNAL_PEER_CLOSED;
  }
  if (receive_queue_length_limit_ &&
      port_status.queued_message_count > *receive_queue_length_limit_) {
    rv.satisfied_signals |= MOJO_HANDLE_SIGNAL_QUOTA_EXCEEDED;
  } else if (receive_queue_memory_size_limit_ &&
             port_status.queued_num_bytes > *receive_queue_memory_size_limit_) {
    rv.satisfied_signals |= MOJO_HANDLE_SIGNAL_QUOTA_EXCEEDED;
  }
  rv.satisfiable_signals |=
      MOJO_HANDLE_SIGNAL_PEER_CLOSED | MOJO_HANDLE_SIGNAL_QUOTA_EXCEEDED;
  return rv;
}

void MessagePipeDispatcher::OnSlotStatusChanged() {
  DCHECK(RequestContext::current());

  base::AutoLock lock(signal_lock_);

  // We stop observing our port as soon as it's transferred, but this can race
  // with events which are raised right before that happens. This is fine to
  // ignore.
  if (port_transferred_)
    return;

#if DCHECK_IS_ON()
  ports::PortStatus port_status;
  if (node_controller_->node()->GetStatus(slot_, &port_status) == ports::OK) {
    if (port_status.has_messages) {
      std::unique_ptr<ports::UserMessageEvent> unused;
      PeekSizeMessageFilter filter;
      node_controller_->node()->GetMessage(slot_, &unused, &filter);
      DVLOG(4) << "New message detected on message pipe " << pipe_id_
               << " endpoint " << endpoint_ << " [slot=" << slot_.port().name()
               << "/" << slot_.slot_id() << "; size=" << filter.message_size()
               << "]";
    }
    if (port_status.peer_closed) {
      DVLOG(2) << "Peer closure detected on message pipe " << pipe_id_
               << " endpoint " << endpoint_ << " [slot=" << slot_.port().name()
               << "/" << slot_.slot_id() << "]";
    }
  }
#endif

  watchers_.NotifyState(GetHandleSignalsStateNoLock());
}

}  // namespace core
}  // namespace mojo
