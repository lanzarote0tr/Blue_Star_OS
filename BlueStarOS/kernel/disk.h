#include <stdint.h>
#include <stddef.h>
#include "ioport.h"

int disk_read(void *user, uint64_t lba, uint32_t cnt, void *buf);
int disk_write(void *user, uint64_t lba, uint32_t cnt, const void *buf);