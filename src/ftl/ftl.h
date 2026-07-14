/*
 * Ftl — a Flash Translation Layer, the core algorithm of SSD firmware.
 *
 * The host (your computer) thinks a drive is a simple array of
 * logical pages it can overwrite at will. NAND flash offers nothing
 * of the sort (see nand_device.h). The FTL bridges the two worlds:
 *
 *   - MAPPING: a table from logical page number (LPN) to the physical
 *     (block, page) that currently holds its data. An "overwrite" is
 *     really a write to a fresh page plus a map update; the old copy
 *     just becomes stale ("invalid").
 *
 *   - GARBAGE COLLECTION: stale pages pile up. When free blocks run
 *     low, GC picks the block with the fewest still-valid pages,
 *     relocates those pages elsewhere, and erases the block.
 *
 *   - WEAR LEVELING: every new write block is chosen as the free
 *     block with the LOWEST erase count, so wear spreads evenly and
 *     no single block dies early.
 *
 *   - BAD BLOCK MANAGEMENT: factory-bad blocks are never used; blocks
 *     that fail an erase are retired on the spot (their data has
 *     already been relocated by then).
 *
 *   - POWER-LOSS RECOVERY: every programmed page carries its LPN and
 *     a global sequence number in the OOB area. If power dies, all
 *     RAM state is lost — recover() rescans every OOB and rebuilds
 *     the mapping, letting the newest sequence number win for each
 *     LPN. Old mappings are only invalidated AFTER the new copy is
 *     safely on flash, so committed writes always survive.
 *
 * Overprovisioning: `spareBlocks` physical blocks are held back from
 * the logical capacity. That spare space is what guarantees GC always
 * has somewhere to move data — every real SSD does the same (it is
 * why a "256 GB" SSD has more than 256 GB of actual flash inside).
 */
#pragma once

#include "../nand/nand_device.h"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace ftl {

struct FtlError : std::runtime_error {
    using std::runtime_error::runtime_error;
};
struct NotWritten     : FtlError { using FtlError::FtlError; };
struct DataCorruption : FtlError { using FtlError::FtlError; };
struct DriveFull      : FtlError { using FtlError::FtlError; };

class Ftl {
public:
    explicit Ftl(nand::NandDevice& dev, uint32_t spareBlocks = 8);

    uint32_t logicalPages() const { return logicalPages_; }
    uint32_t pageSize() const { return dev_.geometry().pageSize; }

    /* Host interface — what a filesystem would call. */
    void write(uint32_t lpn, const std::vector<uint8_t>& data);
    std::vector<uint8_t> read(uint32_t lpn);
    void trim(uint32_t lpn);   // "this data is deleted, stop preserving it"

    /* Rebuild all RAM state from the flash itself (after power loss). */
    void recover();

    struct Stats {
        uint64_t hostWrites = 0;      // writes the host asked for
        uint64_t nandWrites = 0;      // pages actually programmed (incl. GC)
        uint64_t gcRuns = 0;
        uint64_t pagesRelocated = 0;
        /* write amplification factor: nandWrites / hostWrites.
         * 1.0 is perfect; GC pushes it above 1. A famous SSD metric. */
        double waf() const {
            return hostWrites ? (double)nandWrites / (double)hostWrites : 0.0;
        }
    };
    Stats stats() const { return stats_; }
    uint32_t freeBlockCount() const;

    /* Debug/inspection interface (real firmware exposes these too). */
    std::pair<uint32_t, uint32_t> debugPhysical(uint32_t lpn) const;
    char blockStateChar(uint32_t block) const;   // F/A/U/B for display

private:
    enum class BlockState : uint8_t { Free, Active, Full, Bad };
    struct BlockInfo {
        BlockState state = BlockState::Free;
        uint32_t validPages = 0;
    };

    static uint32_t checksum(const std::vector<uint8_t>& d);

    int64_t  packAddr(uint32_t block, uint32_t page) const;
    uint32_t addrBlock(int64_t addr) const;
    uint32_t addrPage(int64_t addr) const;

    uint32_t pickFreeBlock();                     // lowest erase count wins
    void     ensureActiveHasSpace();              // may trigger GC
    void     gc();                                // collect until a block is reclaimed
    void     relocatePage(uint32_t lpn, const std::vector<uint8_t>& data,
                          uint32_t csum);         // GC's internal write path
    void     programToActive(uint32_t lpn, const std::vector<uint8_t>& data,
                             bool hostWrite);
    void     invalidateOld(uint32_t lpn);
    void     safeErase(uint32_t block);           // erase or retire as bad

    nand::NandDevice& dev_;
    uint32_t spareBlocks_;
    uint32_t logicalPages_ = 0;

    std::vector<int64_t>   map_;      // lpn -> packed phys addr, -1 unmapped
    std::vector<BlockInfo> blocks_;
    int64_t  active_ = -1;            // block currently receiving writes
    uint32_t activeNextPage_ = 0;
    uint32_t seq_ = 1;                // global write sequence number
    Stats stats_;
};

} // namespace ftl
