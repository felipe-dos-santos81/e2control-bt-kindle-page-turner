#include "kc_update_cmd.h"

static const char cmd[] = "{\"o\":\"JUMPBOOT\"}";
#define CMD_LEN (sizeof cmd - 1)

/* Whitespace may follow these many matched bytes: after '{', after "o", after ':', after "JUMPBOOT". */
static bool ws_allowed(uint8_t pos)
{
    return pos == 1 || pos == 4 || pos == 5 || pos == 15;
}

bool kc_update_cmd_feed(kc_update_cmd_t *st, uint8_t byte)
{
    if (byte == (uint8_t)cmd[st->pos]) {
        if (++st->pos == CMD_LEN) {
            st->pos = 0;
            return true;
        }
        return false;
    }
    if ((byte == ' ' || byte == '\t' || byte == '\r' || byte == '\n') && ws_allowed(st->pos))
        return false;
    /* '{' occurs only at the start of the command, so after a mismatch the only possible new
     * start is this byte itself. */
    st->pos = (byte == '{');
    return false;
}
