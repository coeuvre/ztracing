#include "channel.h"

#include <memory.h>

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

static Channel *ChannelCreate(usize item_size, usize cap) {
    Channel *channel = (Channel *)AllocateMemory(sizeof(Channel));
    channel->mutex = OsMutexCreate();
    channel->cond = OsCondCreate();
    channel->buffer = (u8 *)AllocateMemory(item_size * cap);
    channel->item_size = item_size;
    channel->cap = cap;
    return channel;
}

static void ChannelDestroy(Channel *channel) {
    ASSERT(channel->rx_closed && channel->tx_closed);
    OsCondDestroy(channel->cond);
    OsMutexDestroy(channel->mutex);
    DeallocateMemory(channel->buffer);
    DeallocateMemory(channel);
}

static bool ChannelCloseRx(Channel *channel) {
    OsMutexLock(channel->mutex);

    ASSERT(!channel->rx_closed);

    channel->rx_closed = true;
    OsCondBroadcast(channel->cond);

    bool all_closed = channel->rx_closed && channel->tx_closed;

    OsMutexUnlock(channel->mutex);

    if (all_closed) {
        ChannelDestroy(channel);
    }

    return all_closed;
}

static bool ChannelCloseTx(Channel *channel) {
    OsMutexLock(channel->mutex);

    ASSERT(!channel->tx_closed);

    channel->tx_closed = true;
    OsCondBroadcast(channel->cond);

    bool all_closed = channel->rx_closed && channel->tx_closed;

    OsMutexUnlock(channel->mutex);

    if (all_closed) {
        ChannelDestroy(channel);
    }

    return all_closed;
}

static bool ChannelSend(Channel *channel, void *item) {
    bool sent = false;

    OsMutexLock(channel->mutex);

    while (!(channel->len < channel->cap || channel->rx_closed)) {
        OsCondWait(channel->cond, channel->mutex);
    }

    if (!channel->rx_closed) {
        ASSERT(channel->len < channel->cap);
        sent = true;

        usize index = (channel->cursor + channel->len) % channel->cap;
        u8 *dst = channel->buffer + (index * channel->item_size);
        memcpy(dst, item, channel->item_size);
        channel->len += 1;

        OsCondBroadcast(channel->cond);
    }

    OsMutexUnlock(channel->mutex);

    return sent;
}

static bool ChannelRecv(Channel *channel, void *out_item) {
    bool received = false;

    OsMutexLock(channel->mutex);

    while (!(channel->len != 0 || channel->tx_closed)) {
        OsCondWait(channel->cond, channel->mutex);
    }

    if (channel->len != 0) {
        received = true;

        usize index = channel->cursor;
        u8 *src = channel->buffer + (index * channel->item_size);
        memcpy(out_item, src, channel->item_size);

        channel->cursor = (channel->cursor + 1) % channel->cap;
        channel->len -= 1;

        OsCondBroadcast(channel->cond);
    }

    OsMutexUnlock(channel->mutex);

    return received;
}
