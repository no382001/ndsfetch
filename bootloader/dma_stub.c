/* Minimal dmaSetParams for the standalone ARM7 bootloader.
 * BlocksDS's dma.h inline functions call this, but we don't link libnds. */
#include <nds/ndstypes.h>
#include <nds/dma.h>

void dmaSetParams(uint8_t channel, const void *src, void *dest, uint32_t ctrl)
{
    REG_DMA_SRC(channel) = (uint32_t)src;
    REG_DMA_DEST(channel) = (uint32_t)dest;
    REG_DMA_CR(channel) = ctrl;
}
