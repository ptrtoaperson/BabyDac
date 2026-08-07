#include "net_config.h"

#include "flash.h"

static bool netcfg_is_valid_ip(const uint8_t ip[4])
{
    if (!ip) {
        return false;
    }

    // Reject invalid/reserved first-octet ranges for host unicast use.
    if ((ip[0] == 0u) || (ip[0] >= 224u)) {
        return false;
    }

    // Loopback is not a valid network interface address for the W5500.
    if (ip[0] == 127u) {
        return false;
    }

    if ((ip[0] == 0u) && (ip[1] == 0u) && (ip[2] == 0u) && (ip[3] == 0u)) {
        return false;
    }

    if ((ip[0] == 255u) && (ip[1] == 255u) && (ip[2] == 255u) && (ip[3] == 255u)) {
        return false;
    }

    return true;
}

bool netcfg_load(uint8_t ip[4], uint16_t *port)
{
    uint32_t words[3] = {0u, 0u, 0u};

    if (!ip || !port) {
        return false;
    }

    flash_read32(NETCFG_FLASH_ADDR, words, 3u);

    if (words[0] != NETCFG_MAGIC) {
        return false;
    }

    ip[0] = (uint8_t)((words[1] >> 0) & 0xFFu);
    ip[1] = (uint8_t)((words[1] >> 8) & 0xFFu);
    ip[2] = (uint8_t)((words[1] >> 16) & 0xFFu);
    ip[3] = (uint8_t)((words[1] >> 24) & 0xFFu);
    *port = (uint16_t)(words[2] & 0xFFFFu);

    if (!netcfg_is_valid_ip(ip) || (*port == 0u)) {
        return false;
    }

    return true;
}

int netcfg_save(const uint8_t ip[4], uint16_t port)
{
    uint32_t words[3];

    if (!netcfg_is_valid_ip(ip) || (port == 0u)) {
        return -1;
    }

    words[0] = NETCFG_MAGIC;
    words[1] = ((uint32_t)ip[0] << 0) |
               ((uint32_t)ip[1] << 8) |
               ((uint32_t)ip[2] << 16) |
               ((uint32_t)ip[3] << 24);
    words[2] = (uint32_t)port;

    return flash_write32(NETCFG_FLASH_ADDR, words, 3u);
}

void netcfg_clear(void)
{
    flash_erase_page(NETCFG_FLASH_ADDR);
}
