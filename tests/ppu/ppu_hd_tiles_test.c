/* Tile provenance follows the actual raster, including flips and world edges. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "snes/ppu.h"
#include "snes/snes.h"
Snes *g_snes;
int snes_frame_counter;
unsigned char g_snesrecomp_last_hdmaen;
uint16_t WsShadowTile(int l,int x,uint32_t y,uint16_t s,uint16_t a,uint16_t t) { return t; }
bool WsShadowLayerActive(int l) { return false; }
uint32_t WsShadowWorldX(int l) { return 0; }
uint32_t WsShadowWorldY(int l) { return 0; }
uint32_t WsShadowScrollX(int l) { return 0; }
uint32_t WsShadowScrollY(int l) { return 0; }
uint32_t WsShadowPresentWorldY(int l,int x) { return 0; }
void WsShadowOnVramWrite(uint16_t a,uint16_t v) {}
static Ppu p;
static uint32_t frame[342 * 2];
static void setup(void) {
  memset(&p, 0, sizeof(p)); ppu_reset(&p);
  p.inidisp=15; p.bgmode=1; p.screenEnabled[0]=1; p.bgXsc[0]=0x10;
  p.hdTracking=true;
  for (int i=0;i<128;i++) p.oam[i*2]=0xf000;
  for (int chr=1;chr<64;chr++)
    for (int row=0;row<8;row++) p.vram[chr*16+row]=0xff;
  PpuBeginDrawing(&p,(uint8_t*)frame,342*4,kPpuRenderFlags_NewRenderer);
  PpuSetExtraSpace(&p,43); ppu_runLine(&p,0);
}
static PpuTileSource sample(int x) {
  PpuTileSource s={0};
  assert(PpuGetTileSource(&p,x,1,p.bgBuffers[0].data[x+kPpuExtraLeftRight],&s));
  return s;
}
int main(void) {
  /* Layer candidates expose transparent texels and the actual windows without
   * changing the native PPU. This permits HD alpha before the priority test. */
  static PpuLayerSample candidates[kPpuBufWidth];
  setup(); p.screenEnabled[1]=1; p.vram[0x1000]=1;
  p.windowsel=2; p.window1left=8;p.window1right=15;p.screenWindowed[0]=1;
  ppu_runLine(&p,1);
  Ppu *before=malloc(sizeof(p));memcpy(before,&p,sizeof(p));
  assert(PpuGetLayerScanline(&p,1,0,candidates,kPpuBufWidth));
  assert(!memcmp(before,&p,sizeof(p)));free(before);
  assert(candidates[kPpuExtraLeftRight+3].screens==3);
  assert(candidates[kPpuExtraLeftRight+10].screens==2);
  assert(candidates[kPpuExtraLeftRight+3].pixel==0x8001);
  assert(candidates[kPpuExtraLeftRight+10].source.valid);
  assert(!(candidates[kPpuExtraLeftRight+10].pixel&15));
  assert(!PpuGetLayerScanline(&p,1,3,candidates,kPpuBufWidth));
  assert(!PpuGetLayerScanline(&p,1,0,candidates,kPpuBufWidth-1));
  setup();p.bgmode=7;p.m7matrix[0]=256;p.m7matrix[3]=256;p.vram[0]=1;
  for(int i=0;i<64;++i)p.vram[64+i]=0x0300;
  ppu_runLine(&p,1);
  assert(PpuGetLayerScanline(&p,1,0,candidates,kPpuBufWidth));
  assert(candidates[kPpuExtraLeftRight+3].pixel==0x5003);
  assert((p.bgBuffers[0].data[kPpuExtraLeftRight+3]&255)==3);
  assert(PpuGetLayerScanline(&p,1,1,candidates,kPpuBufWidth));
  assert(!candidates[kPpuExtraLeftRight+3].screens);
  /* Background identity survives transparent texels and another layer covering
   * it. It is only a context query, never a visibility assertion. */
  setup(); p.vram[0x1000]=7; p.vram[7*16+1]=0;
  ppu_runLine(&p,1); PpuTileSource hidden={0};
  assert(PpuGetBackgroundTileSource(&p,3,1,0,&hidden));
  assert(hidden.address==7*16 && hidden.px==3 && hidden.py==1);
  assert(!PpuGetTileSource(&p,3,1,p.bgBuffers[0].data[275],&hidden));
  assert(!PpuGetBackgroundTileSource(&p,3,1,3,&hidden));
  p.bgmode=7;
  assert(!PpuGetBackgroundTileSource(&p,3,1,0,&hidden));
  for (unsigned flip=0;flip<4;flip++) {
    setup(); p.vram[0x1000]=1|(2<<10)|(flip<<14); ppu_runLine(&p,1);
    PpuTileSource s=sample(3);
    assert(s.address==16 && s.palette==32 && s.bpp==4 && s.flip==flip);
    assert(s.px==(flip&1?4:3) && s.py==(flip&2?6:1));
    /* 16px tiles choose the correct 8px character after applying flips. */
    p.bgmode|=0x10; ppu_runLine(&p,1); s=sample(11);
    unsigned px=flip&1?4:11, py=flip&2?14:1;
    assert(s.address==(1+(px>>3)+((py>>3)<<4))*16 && s.px==(px&7) && s.py==(py&7));
  }
  setup(); p.vram[0x1000]=1;
  PpuSetWidescreenLayerWorldMirrorBand(&p,0,0,224,0,0xffff);
  ppu_runLine(&p,1); PpuTileSource s=sample(-3);
  assert(s.px==2 && s.flip==1);
  /* Mosaic and unsupported modes deliberately keep native graphics. */
  p.mosaic=0x11; ppu_runLine(&p,0); ppu_runLine(&p,1);
  assert(!PpuGetTileSource(&p,3,1,p.bgBuffers[0].data[275],&s));
  for (unsigned flip=0;flip<4;flip++) {
    setup(); p.screenEnabled[0]=16; p.oam[0]=20;
    p.oam[1]=32|0x3400|(flip<<14); ppu_runLine(&p,1); s=sample(23);
    assert(s.address==32*16 && s.palette==0xa0 && s.bpp==4 && s.flip==flip);
    assert(s.px==(flip&1?4:3) && s.py==(flip&2?7:0));
    p.windowsel=2u<<16; p.window1left=20; p.window1right=27; p.screenWindowed[0]=16;
    ppu_runLine(&p,1);
    assert(!PpuGetTileSource(&p,23,1,p.bgBuffers[0].data[295],&s));
  }
  puts("HD provenance: BG/OBJ flips, 16px tiles, world reflection, mosaic and windows passed");
  return 0;
}
