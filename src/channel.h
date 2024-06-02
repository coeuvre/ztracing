#pragma once

#include "core.h"

struct Channel;

Channel *CreateChannel(usize item_size, usize cap);
// Returns true if the channel is destroyed after this call.
bool CloseChannelRx(Channel *channel);
// Returns true if the channel is destroyed after this call.
bool CloseChannelTx(Channel *channel);
// Returns false if the rx is closed.
bool SendToChannel(Channel *channel, void *item);
// Returns false if no item in the buffer and tx is closed. out_item must has at
// least item_size space.
bool ReceiveFromChannel(Channel *channel, void *out_item);
