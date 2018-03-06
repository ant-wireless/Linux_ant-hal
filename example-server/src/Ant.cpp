/*
 * ANT Android Host Stack
 *
 * Copyright 2018 Dynastream Innovations
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "Ant Hidl Example"

#include <cassert>
#include <cstring>

#include <limits>
#include <sstream>
#include <stdexcept>

#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <poll.h>

#include "log/log.h"

#include "Ant.h"

// NOTE: Code here holds mutex locks while making callbacks. This is fine in this case since
// all of the callback methods are marked as oneway in the hal and therefore are non-blocking.

namespace com {
namespace dsi {
namespace ant {
namespace V1_0 {
namespace example {

// Convenience typedef, since we only use one type of mutex.
typedef std::lock_guard<std::mutex> lock;

// Header for framing ANT messages to go to the chip.
struct MsgHeader {
    uint8_t channel_id;
    uint8_t payload_len;

    static const size_t max_payload = std::numeric_limits<uint8_t>::max();
};

// Error classes. This implementation uses the upper 16 bits of a status code
// to indicate a class, and the lower 16 bits as an associated errno value.
// It assumes that all errno values fit, which is currently the case in the
// errno.h header used by android.
// see translateStatus to understand exactly what each error code means.
enum errors : uint16_t {
    GENERIC_ERR,
    SOCKET_ERR,
    CONNECT_ERR,
    EFD_ERR,
    NOFD_ERR,
    LARGEMSG_ERR,
};

// Convenience to not need the fully scoped name every time.
static const Status STATUS_OK = (Status)CommonStatusCodes::OK;

// Example implementation uses an UNIX domain socket, which allows for
// adb reverse-forwarding to be used.
static const sockaddr_un socket_addr = {
    AF_UNIX,
    "/dev/socket/ant",
    };

static const int poll_timeout_ms = 1000 * 5;

// Implementation properties are constant.
static const ImplProps props = {
    "ant.hidl.example.1.0",
    OptionFlags::USE_KEEPALIVE | OptionFlags::USE_ANT_FLOW_CONTROL,
};

// Bit manipulation for status codes.

static Status makeStatus(uint32_t cls, uint32_t err_code)
{ return ((cls << 16)|(err_code & 0xffff)); }

static uint16_t getErrClass(Status status)
{ return (uint16_t)((status >> 16) & 0xffff); }

static uint16_t getErrCode(Status status)
{ return (uint16_t)(status & 0xffff); }

// Convenience stuff for file descriptors.
static bool isValidFd(int fd) { return fd >= 0; }

static int invalid_fd = -1;

class MessageReader {
public:
    MessageReader(int fd) : fd(fd), read_idx(0) {}
    int checkForMessages(const sp<IAntCallbacks> &callbacks);

private:
    int fd;
    uint8_t read_buf[sizeof(MsgHeader) + MsgHeader::max_payload];
    size_t read_idx;
};

int MessageReader::checkForMessages(const sp<IAntCallbacks> &callbacks) {

    // First consume any new data that is available.
    ssize_t read_result = read(
        fd,
        &read_buf[read_idx],
        sizeof(read_buf) - read_idx);

    if (read_result < 0) {
        switch(errno) {
        case EAGAIN:
        case EINTR:
            // These errors are okay, act like no data read.
            read_result = 0;
            break;
        default:
            return errno;
        };
    }

    read_idx += read_result;

    // Now dispatch all read messages.

    // Alias the front of the buffer as a message header.
    MsgHeader *header = (MsgHeader*)read_buf;

    size_t full_size = header->payload_len + sizeof(MsgHeader);
    while (read_idx >= sizeof(MsgHeader) && read_idx >= full_size) {

        assert((header->channel_id == Ant::command_channel_id) ||
            (header->channel_id == Ant::data_channel_id));

        if (callbacks != NULL) {
            hidl_vec<uint8_t> msg;
            msg.setToExternal(read_buf + sizeof(MsgHeader), header->payload_len);
            callbacks->onMessageReceived(msg);
        }

        if (read_idx > full_size) {
            // There's (part of) another message, move it to the front of the buffer.
            std::memmove(read_buf, read_buf + full_size, read_idx - full_size);
        }
        read_idx -= full_size;
        full_size = header->payload_len + sizeof(MsgHeader);
    }

    return STATUS_OK;
}

Ant::Ant():
   transport_fd(invalid_fd),
   shutdown_fd(invalid_fd),
   stop_polling(true)
   {}

// Methods from IAnt follow.
Return<void> Ant::getProperties(getProperties_cb _hidl_cb) {
    _hidl_cb(props);
    return Void();
}

Return<void> Ant::setCallbacks(const sp<IAntCallbacks>& callbacks) {
    // Keep a reference to the old value around until we are outside of the
    // locked scope. See RefBase.h for why this is needed.
    sp<IAntCallbacks> old;
    {
        lock l(state_mtx);
        old = this->callbacks;
        this->callbacks = callbacks;
    }

    return Void();
}

Return<void> Ant::translateStatus(Status status, translateStatus_cb _hidl_cb) {
    std::ostringstream err;
    uint16_t err_class = getErrClass(status);
    uint16_t err_code = getErrCode(status);

    switch(err_class) {
    case GENERIC_ERR:
        break;
    case SOCKET_ERR:
        err << "Socket creation failed.";
        break;
    case CONNECT_ERR:
        err << "Socket connect failed.";
        break;
    case EFD_ERR:
        err << "Event fd not created.";
        break;
    case NOFD_ERR:
        err << "Transport not open.";
        break;
    case LARGEMSG_ERR:
        err << "Provided message too big.";
        break;
    default:
        err << "Unknown Error Class (" << err_class << ").";
        break;
    };

    if (err_code) {
        // Add a space between class and error string, only if a class string was
        // added (ie. length > 0)
        if (err.tellp() > 0) {
            err << " ";
        }
        err << strerror(err_code);
    }

    _hidl_cb(err.str());
    return Void();
}

Return<Status> Ant::enable() {
    // Early returns are used to bail out here, this is okay in this case though
    // since disable will always be used to recover, and we deal with any inconsistent state there.

    ALOGV("Enabling");

    lock l(state_mtx);

    stop_polling = false;

    // Open the local socket that is adb-forwarded somewhere. Actual implementations
    // would maybe open a character device here.
    // The socket is non-blocking since the poll loop waits for data anyways.
    transport_fd = socket(AF_UNIX, SOCK_STREAM|SOCK_NONBLOCK, 0);
    if (!isValidFd(transport_fd)) {
        return makeStatus(SOCKET_ERR, errno);
    }

    if (connect(transport_fd, (const sockaddr*)&socket_addr, sizeof(socket_addr)) < 0) {
        return makeStatus(CONNECT_ERR, errno);
    }

    // Setup the shutdown handle, which is an event-fd that can be used to interrupt
    // the poll thread when it is stuck in a poll().
    shutdown_fd = eventfd(0, EFD_NONBLOCK);
    if (!isValidFd(shutdown_fd)) {
        return makeStatus(EFD_ERR, errno);
    }

    // If all state was setup succesfully then the poll thread can be started.
    poll_thread = std::thread(&Ant::pollThreadEntry, this);

    return STATUS_OK;
}

Return<Status> Ant::disable() {
    ALOGV("Disabling");

    // Used to capture the value of the poll thread.
    std::thread old_poll;

    {
        // Use a scope block, since we need to be unlocked while joining the thread.
        lock l(state_mtx);

        std::swap(old_poll, poll_thread);
        if (isValidFd(shutdown_fd)) {
            uint64_t shutdown_evt = 1;
            if (write(shutdown_fd, &shutdown_evt, sizeof(shutdown_evt)) < 0) {
                ALOGW("Shutdown sending signal failed. %s", strerror(errno));
            }
        }

        stop_polling = true;
    }

    if (old_poll.get_id() != std::thread::id()) {
        // NOTE: if it's possible for the poll thread to get stuck it might
        // be better to do a timed wait on a future<void> set by the threads exit.
        // If a timeout occurs the thread should be detached instead of joined.
        old_poll.join();
    }

    // At this point the poll thread should be stopped, meaning it's safe to start
    // cleaning up files. The state lock is still held to make sure we don't close
    // the handles on any message writes in progress.

    lock l2(state_mtx);

    if (isValidFd(shutdown_fd)) {
        if (close(shutdown_fd) < 0) {
            ALOGW("Could not cleanly close event_fd. %s", strerror(errno));
        }
    }
    shutdown_fd = invalid_fd;

    if (isValidFd(transport_fd)) {
        if (close(transport_fd) < 0) {
            ALOGW("Could not cleanly close transport_fd. %s", strerror(errno));
        }
    }
    transport_fd = invalid_fd;

    return STATUS_OK;
}

void Ant::pollThreadEntry() {
    bool should_quit = false;
    MessageReader reader(transport_fd);
    // This remains empty as long as no error occured.
    std::string err_msg;

    struct poll_fds_t {
       pollfd transport;
       pollfd shutdown;
    } poll_fds;

    // Static setup for poll loop.
    // The only non-error event that matters is the data ready event.
    poll_fds.transport.fd = transport_fd;
    poll_fds.transport.events = POLLIN;
    poll_fds.shutdown.fd = shutdown_fd;
    poll_fds.shutdown.events = POLLIN;

    class poll_err : public std::runtime_error {};

    // Error cases just bail straight out of the thread
    while(!should_quit) {
        // Clear out previous events.
        poll_fds.transport.revents = 0;
        poll_fds.shutdown.revents = 0;

        int poll_result = poll((pollfd*)&poll_fds, sizeof(poll_fds)/sizeof(pollfd), poll_timeout_ms);

        // Now that poll is done grab the state lock, the rest should be quick, since all
        // operations are non-blocking.
        lock l(state_mtx);

        should_quit = stop_polling;

        // Only care to differentiate error case, and ignore EINTR errors.
        if (poll_result < 0 && errno != EINTR) {
            err_msg = std::string("Poll call failed. ") + strerror(errno);
            break;
        }

        if (poll_fds.transport.revents) {
            if (poll_fds.transport.revents != POLLIN) {
                std::ostringstream err_bld("Poll error flags on transport file: ");
                err_bld << (int)poll_fds.transport.revents;
                err_msg = err_bld.str();
                break;
            }

            int read_result = reader.checkForMessages(callbacks);
            if (read_result != STATUS_OK) {
                err_msg =  std::string("Could not read available data") + strerror(read_result);
                break;
            }
        }

        if (poll_fds.shutdown.revents) {
            if (poll_fds.shutdown.revents != POLLIN) {
                std::ostringstream err_bld("Poll error flags on shutdown file: ");
                err_bld << (int)poll_fds.shutdown.revents;
                err_msg = err_bld.str();
                break;
            }

            // No need to read, the eventfd is only signaled when shutting down.
            should_quit = true;
        }
    }

    if (!err_msg.empty()) {
        lock l(state_mtx);
        if (callbacks != NULL) {
            callbacks->onTransportDown(err_msg);
        }
    }

    return;
}

Status Ant::writeMsg(const hidl_vec<uint8_t> &msg, uint8_t channel_id) {
    if (msg.size() > MsgHeader::max_payload) {
        return makeStatus(LARGEMSG_ERR, 0);
    }

    // Lock is held for full function to make sure the fd isn't changed on us.
    // This should never take long, higher level flow control should ensure
    // we never block waiting for space in write buffers.
    lock l(state_mtx);
    if (!isValidFd(transport_fd)) {
        return makeStatus(NOFD_ERR, 0);
    }

    Status retval = STATUS_OK;

    MsgHeader header = {
        channel_id,
        (uint8_t)msg.size(),
    };

    iovec vecs[] = {
        { (uint8_t*)&header, sizeof(header) },
        // Cast away the constness of the data.
        // This is okay because we are only sourcing the data for a write,
        // but is required because the struct in the writev api is non-const
        // in order to be shared with readv calls.
        { const_cast<uint8_t*>(msg.data()), msg.size() },
    };
    size_t num_vecs = sizeof(vecs)/sizeof(vecs[0]);

    // Continue until the last chunk has been fully written.
    while(vecs[num_vecs - 1].iov_len > 0) {
        ssize_t written = writev(transport_fd, vecs, num_vecs);

        if (written < 0) {
            if (errno == EINTR) {
                // EINTR is okay, it means no data was written though.
                written = 0;
            } else {
                retval = makeStatus(GENERIC_ERR, errno);
                // Abort writing the remainder.
                break;
            }
        }

        // Adjust write to resume with unwritten portion.
        for (size_t i = 0; i < num_vecs; i++) {
            if ((size_t)written <= vecs[i].iov_len) {
                // Chunk was partially written, pointer adjustment needed.
                vecs[i].iov_len -= written;
                vecs[i].iov_base = ((uint8_t*)vecs[i].iov_base) + written;
                // Remaining chunks are unchanged.
                break;
            }

            // Chunk fully written, move onto next.
            vecs[i].iov_len = 0;
            written -= vecs[i].iov_len;
        }
    }

    return retval;
}

}  // namespace example
}  // namespace V1_0
}  // namespace ant
}  // namespace dsi
}  // namespace com
