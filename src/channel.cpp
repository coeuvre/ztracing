#include "channel.h"

#include <memory.h>

#include "core.h"
#include "memory.h"
#include "os.h"

struct ItemNode {
    ItemNode *next;
    u8 payload[0];
};

struct Channel {
    Arena arena;
    OsMutex *mutex;
    OsCond *cond;
    usize item_size;
    usize cap;
    usize len;
    ItemNode *first;
    ItemNode *last;
    ItemNode *free;
    bool rx_closed;
    bool tx_closed;
};

Channel *
CreateChannel(usize item_size, usize cap) {
    Channel *channel = BootstrapPushStruct(Channel, arena);
    channel->mutex = OsCreateMutex();
    channel->cond = OsCreateCond();
    channel->item_size = item_size;
    channel->cap = cap;
    return channel;
}

static void
DestroyChannel(Channel *channel) {
    ASSERT(channel->rx_closed && channel->tx_closed);
    OsDestroyCond(channel->cond);
    OsDestroyMutex(channel->mutex);
    ClearArena(&channel->arena);
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
        DestroyChannel(channel);
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
        DestroyChannel(channel);
    }

    return all_closed;
}

bool
SendToChannel(Channel *channel, void *item) {
    bool sent = false;

    OsLockMutex(channel->mutex);

    while (!(!channel->cap || channel->len < channel->cap || channel->rx_closed)
    ) {
        OsWaitCond(channel->cond, channel->mutex);
    }

    if (!channel->rx_closed) {
        ASSERT(!channel->cap || channel->len < channel->cap);
        sent = true;

        ItemNode *item_node = 0;
        if (channel->free) {
            item_node = channel->free;
            channel->free = item_node->next;
        } else {
            item_node = (ItemNode *)PushSize(
                &channel->arena,
                sizeof(ItemNode) + channel->item_size
            );
        }

        item_node->next = 0;
        CopyMemory(item_node->payload, item, channel->item_size);

        if (channel->first) {
            channel->last->next = item_node;
        } else {
            channel->first = item_node;
        }
        channel->last = item_node;
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

        ASSERT(channel->first);
        ItemNode *item_node = channel->first;
        channel->first = channel->first->next;
        if (!channel->first) {
            channel->last = 0;
        }
        channel->len -= 1;

        CopyMemory(out_item, item_node->payload, channel->item_size);

        item_node->next = channel->free;
        channel->free = item_node;

        OsBroadcast(channel->cond);
    }

    OsUnlockMutex(channel->mutex);

    return received;
}
