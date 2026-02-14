#include "disk.h"

enum {
    ATA_REG_DATA      = 0x1F0,
    ATA_REG_SECCOUNT0 = 0x1F2,
    ATA_REG_LBA0      = 0x1F3,
    ATA_REG_LBA1      = 0x1F4,
    ATA_REG_LBA2      = 0x1F5,
    ATA_REG_HDDEVSEL  = 0x1F6,
    ATA_REG_STATUS    = 0x1F7,
    ATA_REG_COMMAND   = 0x1F7,
    ATA_REG_ALTSTATUS = 0x3F6,
};

enum {
    ATA_CMD_READ_PIO_EXT  = 0x24,
    ATA_CMD_WRITE_PIO_EXT = 0x34,
    ATA_CMD_CACHE_FLUSH   = 0xEA,
};

enum {
    ATA_STATUS_ERR = 0x01,
    ATA_STATUS_DRQ = 0x08,
    ATA_STATUS_DF  = 0x20,
    ATA_STATUS_BSY = 0x80,
};

#define ATA_SECTOR_SIZE 512U
#define ATA_POLL_LIMIT  1000000U



static void ata_delay_400ns(void)
{
    (void)in8(ATA_REG_ALTSTATUS);
    (void)in8(ATA_REG_ALTSTATUS);
    (void)in8(ATA_REG_ALTSTATUS);
    (void)in8(ATA_REG_ALTSTATUS);
}

static int ata_wait_ready_for_xfer(void)
{
    for (uint32_t i = 0; i < ATA_POLL_LIMIT; i++) {
        uint8_t status = in8(ATA_REG_STATUS);
        if (status & ATA_STATUS_BSY) {
            continue;
        }
        if (status & (ATA_STATUS_ERR | ATA_STATUS_DF)) {
            return 0;
        }
        if (status & ATA_STATUS_DRQ) {
            return 1;
        }
    }
    return 0;
}

static int ata_wait_not_busy(void)
{
    for (uint32_t i = 0; i < ATA_POLL_LIMIT; i++) {
        uint8_t status = in8(ATA_REG_STATUS);
        if ((status & ATA_STATUS_BSY) == 0) {
            if (status & (ATA_STATUS_ERR | ATA_STATUS_DF)) {
                return 0;
            }
            return 1;
        }
    }
    return 0;
}

static void ata_pio_read_sector(void *buf)
{
    uint8_t *dst = (uint8_t *)buf;
    for (uint32_t i = 0; i < ATA_SECTOR_SIZE / 2U; i++) {
        uint16_t word = in16(ATA_REG_DATA);
        dst[2U * i] = (uint8_t)(word & 0xFFU);
        dst[(2U * i) + 1U] = (uint8_t)(word >> 8U);
    }
}

static void ata_pio_write_sector(const void *buf)
{
    const uint8_t *src = (const uint8_t *)buf;
    for (uint32_t i = 0; i < ATA_SECTOR_SIZE / 2U; i++) {
        uint16_t word = (uint16_t)src[2U * i] | ((uint16_t)src[(2U * i) + 1U] << 8U);
        out16(ATA_REG_DATA, word);
    }
}

static int ata_issue_lba48(uint8_t drive, uint64_t lba, uint16_t sector_count, uint8_t command)
{
    if (!ata_wait_not_busy()) {
        return 0;
    }

    out8(ATA_REG_HDDEVSEL, (uint8_t)(0x40U | (drive ? 0x10U : 0x00U)));
    ata_delay_400ns();

    out8(ATA_REG_SECCOUNT0, (uint8_t)(sector_count >> 8U));
    out8(ATA_REG_LBA0, (uint8_t)((lba >> 24U) & 0xFFU));
    out8(ATA_REG_LBA1, (uint8_t)((lba >> 32U) & 0xFFU));
    out8(ATA_REG_LBA2, (uint8_t)((lba >> 40U) & 0xFFU));

    out8(ATA_REG_SECCOUNT0, (uint8_t)(sector_count & 0xFFU));
    out8(ATA_REG_LBA0, (uint8_t)(lba & 0xFFU));
    out8(ATA_REG_LBA1, (uint8_t)((lba >> 8U) & 0xFFU));
    out8(ATA_REG_LBA2, (uint8_t)((lba >> 16U) & 0xFFU));

    out8(ATA_REG_COMMAND, command);
    ata_delay_400ns();
    return 1;
}

int disk_read(void *user, uint64_t lba, uint32_t cnt, void *buf) {
    uint8_t drive = ((uintptr_t)user & 1U) ? 1U : 0U;
    uint8_t *dst = (uint8_t *)buf;

    if (cnt == 0U) {
        return 1;
    }
    if (buf == 0) {
        return 0;
    }
    if (lba > 0x0000FFFFFFFFFFFFULL) {
        return 0;
    }
    if ((uint64_t)cnt > (0x0001000000000000ULL - lba)) {
        return 0;
    }

    while (cnt > 0U) {
        uint16_t chunk = (cnt > 0xFFFFU) ? 0xFFFFU : (uint16_t)cnt;

        if (!ata_issue_lba48(drive, lba, chunk, ATA_CMD_READ_PIO_EXT)) {
            return 0;
        }

        for (uint32_t i = 0; i < (uint32_t)chunk; i++) {
            if (!ata_wait_ready_for_xfer()) {
                return 0;
            }
            ata_pio_read_sector(dst + ((size_t)i * ATA_SECTOR_SIZE));
        }

        lba += (uint64_t)chunk;
        dst += (size_t)chunk * ATA_SECTOR_SIZE;
        cnt -= (uint32_t)chunk;
    }

    return 1;
}
int disk_write(void *user, uint64_t lba, uint32_t cnt, const void *buf) {
    uint8_t drive = ((uintptr_t)user & 1U) ? 1U : 0U;
    const uint8_t *src = (const uint8_t *)buf;

    if (cnt == 0U) {
        return 1;
    }
    if (buf == 0) {
        return 0;
    }
    if (lba > 0x0000FFFFFFFFFFFFULL) {
        return 0;
    }
    if ((uint64_t)cnt > (0x0001000000000000ULL - lba)) {
        return 0;
    }

    while (cnt > 0U) {
        uint16_t chunk = (cnt > 0xFFFFU) ? 0xFFFFU : (uint16_t)cnt;

        if (!ata_issue_lba48(drive, lba, chunk, ATA_CMD_WRITE_PIO_EXT)) {
            return 0;
        }

        for (uint32_t i = 0; i < (uint32_t)chunk; i++) {
            if (!ata_wait_ready_for_xfer()) {
                return 0;
            }
            ata_pio_write_sector(src + ((size_t)i * ATA_SECTOR_SIZE));
        }

        if (!ata_wait_not_busy()) {
            return 0;
        }
        out8(ATA_REG_COMMAND, ATA_CMD_CACHE_FLUSH);
        if (!ata_wait_not_busy()) {
            return 0;
        }

        lba += (uint64_t)chunk;
        src += (size_t)chunk * ATA_SECTOR_SIZE;
        cnt -= (uint32_t)chunk;
    }

    return 1;
}