#include "channel.h"

#include <memory.h>

#include "memory.h"
#include "os.h"

struct Channel {
    OsMutex *mutex;
    OsCond *cond;
    u8 *buffer;
    usize item_size;
    usize cap;
    usize len;
    usize cursor;
    bool rx_closed;
    bool tx_closed;
};

Channel *
CreateChannel(usize item_size, usize cap) {
    Channel *channel = (Channel *)AllocateMemory(sizeof(Channel));
    channel->mutex = OsCreateMutex();
    channel->cond = OsCreateCond();
    channel->buffer = (u8 *)AllocateMemory(item_size * cap);
    channel->item_size = item_size;
    channel->cap = cap;
    return channel;
}

void
ChannelDestroy(Channel *channel) {
    ASSERT(channel->rx_closed && channel->tx_closed);
    OsDestroyCond(channel->cond);
    OsDestroyMutex(channel->mutex);
    DeallocateMemory(channel->buffer);
    DeallocateMemory(channel);
}

bool
CloseChannelRx(Channel *channel) {
    OsLockMutex(channel->mutex);

    ASSERT(!channel->rx_closed);

    channel->rx_closed = true;
    OsBroadcast(channel->cond);

    bool all_closed = channel->rx_closed && channel->tx_closed;

    OsUnlockMutex(channel->mutex);

    if (all_closed) {
        ChannelDestroy(channel);
    }

    return all_closed;
}

bool
CloseChannelTx(Channel *channel) {
    OsLockMutex(channel->mutex);

    ASSERT(!channel->tx_closed);

    channel->tx_closed = true;
    OsBroadcast(channel->cond);

    bool all_closed = channel->rx_closed && channel->tx_closed;

    OsUnlockMutex(channel->mutex);

    if (all_closed) {
        ChannelDestroy(channel);
    }

    return all_closed;
}

bool
SendToChannel(Channel *channel, void *item) {
    bool sent = false;

    OsLockMutex(channel->mutex);

    while (!(channel->len < channel->cap || channel->rx_closed)) {
        OsWaitCond(channel->cond, channel->mutex);
    }

    if (!channel->rx_closed) {
        ASSERT(channel->len < channel->cap);
        sent = true;

        usize index = (channel->cursor + channel->len) % channel->cap;
        u8 *dst = channel->buffer + (index * channel->item_size);
        CopyMemory(dst, item, channel->item_size);
        channel->len += 1;

        OsBroadcast(channel->cond);
    }

    OsUnlockMutex(channel->mutex);

    return sent;
}

bool
ReceiveFromChannel(Channel *channel, void *out_item) {
    bool received = false;

    OsLockMutex(channel->mutex);

    while (!(channel->len != 0 || channel->tx_closed)) {
        OsWaitCond(channel->cond, channel->mutex);
    }

    if (channel->len != 0) {
        received = true;

        usize index = channel->cursor;
        u8 *src = channel->buffer + (index * channel->item_size);
        CopyMemory(out_item, src, channel->item_size);

        channel->cursor = (channel->cursor + 1) % channel->cap;
        channel->len -= 1;

        OsBroadcast(channel->cond);
    }

    OsUnlockMutex(channel->mutex);

    return received;
}
