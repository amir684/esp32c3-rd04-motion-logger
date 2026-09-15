#include "eventlog.h"
#include <esp_partition.h>
#include <esp_spi_flash.h>

static const uint32_t REC_MAGIC   = 0x52443034; // "RD04"
static const uint32_t CACHE_MAGIC = 0xE7106A11;
static const uint32_t REC_SIZE    = sizeof(EventRec);
static const uint32_t SECTOR_SIZE = 4096;
static const uint32_t PER_SECTOR  = SECTOR_SIZE / REC_SIZE;

// Survives deep sleep, lost on reset/power loss (then rebuilt by scanning flash)
RTC_DATA_ATTR static struct {
    uint32_t magic;
    uint32_t head;     // index where the next record goes
    uint32_t lastSeq;
} s_cache;

static const esp_partition_t *s_part = nullptr;
static uint32_t s_capacity = 0;

static bool isValid(const EventRec &r) {
    return r.magic == REC_MAGIC && r.seq != 0xFFFFFFFF;
}

// Memory-maps the whole partition for fast reads; the mapping is released before any write.
class MappedLog {
public:
    MappedLog() {
        if (esp_partition_mmap(s_part, 0, s_part->size, SPI_FLASH_MMAP_DATA, &m_ptr, &m_handle) != ESP_OK)
            m_ptr = nullptr;
    }
    ~MappedLog() {
        if (m_ptr) spi_flash_munmap(m_handle);
    }
    const EventRec *at(uint32_t idx) const {
        return m_ptr ? (const EventRec *)((const uint8_t *)m_ptr + idx * REC_SIZE) : nullptr;
    }
private:
    const void *m_ptr = nullptr;
    spi_flash_mmap_handle_t m_handle = 0;
};

static void rebuildCache() {
    s_cache.head = 0;
    s_cache.lastSeq = 0;
    MappedLog map;
    for (uint32_t i = 0; i < s_capacity; i++) {
        const EventRec *r = map.at(i);
        if (!r) break;
        if (isValid(*r) && r->seq > s_cache.lastSeq) {
            s_cache.lastSeq = r->seq;
            s_cache.head = (i + 1) % s_capacity;
        }
    }
    s_cache.magic = CACHE_MAGIC;
}

namespace EventLog {

bool begin(bool trustRtcCache) {
    s_part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, (esp_partition_subtype_t)0x40, "evlog");
    if (!s_part) return false;
    s_capacity = (s_part->size / SECTOR_SIZE) * PER_SECTOR;

    if (!trustRtcCache || s_cache.magic != CACHE_MAGIC || s_cache.head >= s_capacity)
        rebuildCache();
    return true;
}

bool append(uint32_t ts, uint16_t boot, uint16_t flags) {
    if (!s_part) return false;
    uint32_t idx = s_cache.head;

    // Entering a new sector: erase it (it is either blank or holds the oldest events)
    if (idx % PER_SECTOR == 0) {
        if (esp_partition_erase_range(s_part, idx * REC_SIZE, SECTOR_SIZE) != ESP_OK) return false;
    }

    EventRec rec = { s_cache.lastSeq + 1, ts, boot, flags, REC_MAGIC };
    if (esp_partition_write(s_part, idx * REC_SIZE, &rec, REC_SIZE) != ESP_OK) return false;

    s_cache.lastSeq = rec.seq;
    s_cache.head = (idx + 1) % s_capacity;
    return true;
}

void forEachNewestFirst(uint32_t limit, const std::function<bool(const EventRec &)> &cb) {
    if (!s_part) return;
    MappedLog map;
    uint32_t sent = 0;
    for (uint32_t n = 1; n <= s_capacity; n++) {
        const EventRec *r = map.at((s_cache.head + s_capacity - n) % s_capacity);
        if (!r) return;
        if (!isValid(*r)) continue;
        if (!cb(*r)) return;
        if (limit && ++sent >= limit) return;
    }
}

uint32_t count() {
    uint32_t c = 0;
    forEachNewestFirst(0, [&](const EventRec &) { c++; return true; });
    return c;
}

uint32_t capacity() {
    return s_capacity;
}

bool clear() {
    if (!s_part) return false;
    if (esp_partition_erase_range(s_part, 0, s_part->size) != ESP_OK) return false;
    s_cache.head = 0;
    s_cache.lastSeq = 0;
    s_cache.magic = CACHE_MAGIC;
    return true;
}

}
