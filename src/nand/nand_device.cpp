#include "nand_device.h"

namespace nand {

NandDevice::NandDevice(const Geometry& g) : geo_(g) {
    blocks_.resize(geo_.blocks);
    for (auto& b : blocks_) {
        b.pages.resize(geo_.pagesPerBlock);
        for (auto& p : b.pages)
            p.data.assign(geo_.pageSize, 0xFF);   // erased NAND reads 0xFF
    }
}

void NandDevice::checkBlock(uint32_t block) const {
    if (block >= geo_.blocks)
        throw NandError("block index out of range: " + std::to_string(block));
}

void NandDevice::checkPage(uint32_t block, uint32_t page) const {
    checkBlock(block);
    if (page >= geo_.pagesPerBlock)
        throw NandError("page index out of range: " + std::to_string(page));
}

void NandDevice::programPage(uint32_t block, uint32_t page,
                             const std::vector<uint8_t>& data, const Oob& oob) {
    checkPage(block, page);
    Block& b = blocks_[block];

    if (b.bad)
        throw BadBlockError("program on bad block " + std::to_string(block));
    if (data.size() != geo_.pageSize)
        throw ProgramError("data size must equal page size");
    if (b.pages[page].state != PageState::Erased)
        throw ProgramError("page overwrite without erase: block " +
                           std::to_string(block) + " page " + std::to_string(page));
    if (page != b.programmedPages)
        throw ProgramError("pages must be programmed sequentially in a block");

    /* fault injection: power dies BEFORE the cells are charged,
     * so this write simply never happens */
    if (powerLossIn_ >= 0 && powerLossIn_-- == 0) {
        powerLossIn_ = -1;
        throw PowerLoss();
    }

    b.pages[page].data  = data;
    b.pages[page].oob   = oob;
    b.pages[page].state = PageState::Programmed;
    b.programmedPages++;
    stats_.programs++;
}

void NandDevice::readPage(uint32_t block, uint32_t page,
                          std::vector<uint8_t>& data, Oob& oob) const {
    checkPage(block, page);
    const Page& p = blocks_[block].pages[page];
    data = p.data;
    oob  = p.oob;
    stats_.reads++;
}

void NandDevice::eraseBlock(uint32_t block) {
    checkBlock(block);
    Block& b = blocks_[block];

    if (b.bad)
        throw BadBlockError("erase on bad block " + std::to_string(block));

    if (b.failNextErase || b.eraseCount >= geo_.eraseLimit) {
        b.bad = true;   // the block has "grown" bad
        throw EraseError("erase failed, block " + std::to_string(block) +
                         " marked bad (erase count " +
                         std::to_string(b.eraseCount) + ")");
    }

    for (auto& p : b.pages) {
        p.data.assign(geo_.pageSize, 0xFF);
        p.oob   = Oob{};
        p.state = PageState::Erased;
    }
    b.programmedPages = 0;
    b.eraseCount++;
    stats_.erases++;
}

PageState NandDevice::pageState(uint32_t block, uint32_t page) const {
    checkPage(block, page);
    return blocks_[block].pages[page].state;
}

uint32_t NandDevice::programmedPages(uint32_t block) const {
    checkBlock(block);
    return blocks_[block].programmedPages;
}

bool NandDevice::isBad(uint32_t block) const {
    checkBlock(block);
    return blocks_[block].bad;
}

uint32_t NandDevice::eraseCount(uint32_t block) const {
    checkBlock(block);
    return blocks_[block].eraseCount;
}

uint32_t NandDevice::badBlockCount() const {
    uint32_t n = 0;
    for (const auto& b : blocks_) if (b.bad) n++;
    return n;
}

uint32_t NandDevice::minEraseCount() const {
    uint32_t m = UINT32_MAX;
    for (const auto& b : blocks_)
        if (!b.bad && b.eraseCount < m) m = b.eraseCount;
    return m == UINT32_MAX ? 0 : m;
}

uint32_t NandDevice::maxEraseCount() const {
    uint32_t m = 0;
    for (const auto& b : blocks_)
        if (!b.bad && b.eraseCount > m) m = b.eraseCount;
    return m;
}

void NandDevice::injectBitFlip(uint32_t block, uint32_t page, uint32_t bitIndex) {
    checkPage(block, page);
    Page& p = blocks_[block].pages[page];
    if (p.state != PageState::Programmed)
        throw NandError("bit flip injection needs a programmed page");
    uint32_t byte = bitIndex / 8;
    if (byte >= geo_.pageSize)
        throw NandError("bit index out of range");
    p.data[byte] ^= (uint8_t)(1u << (bitIndex % 8));
}

void NandDevice::injectEraseFailure(uint32_t block) {
    checkBlock(block);
    blocks_[block].failNextErase = true;
}

void NandDevice::markFactoryBad(uint32_t block) {
    checkBlock(block);
    blocks_[block].bad = true;
}

} // namespace nand
