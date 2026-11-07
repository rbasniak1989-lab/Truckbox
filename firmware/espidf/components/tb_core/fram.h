#pragma once

#include <stdbool.h>
#include <stdint.h>

/* SPI2 must already have been initialized by the storage component. */
bool tb_fram_start(void);
bool tb_fram_get_metadata(uint64_t *reserved_end, uint64_t *ack);
void tb_fram_checkpoint(uint64_t reserved_end, uint64_t ack);
const char *tb_fram_status(void);

