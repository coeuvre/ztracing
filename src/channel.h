#pragma once

#include "memory.h"
#include "os.h"

struct Channel;

static Channel *ChannelCreate(usize item_size, usize cap);
// Returns true if the channel is destroyed after this call.
static bool ChannelCloseRx(Channel *channel);
// Returns true if the channel is destroyed after this call.
static bool ChannelCloseTx(Channel *channel);
// Returns false if the rx is closed.
static bool ChannelSend(Channel *channel, void *item);
// Returns false if no item in the buffer and tx is closed. out_item must has at
// least item_size space.
static bool ChannelRecv(Channel *channel, void *out_item);
