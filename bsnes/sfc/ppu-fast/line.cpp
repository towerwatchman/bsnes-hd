uint PPU::Line::start = 0;
uint PPU::Line::count = 0;

auto PPU::Line::flush() -> void {
  if(Line::count) {
    if(ppu.hdScale() > 1) cacheMode7HD();
    #pragma omp parallel for if(Line::count >= 8)
    for(uint y = 0; y < Line::count; y++) {
      if(ppu.deinterlace()) {
        if(!ppu.interlace()) {
          //some games enable interlacing in 240p mode, just force these to even fields
          ppu.lines[Line::start + y].render(0);
        } else {
          //for actual interlaced frames, render both fields every farme for 480i -> 480p
          ppu.lines[Line::start + y].render(0);
          ppu.lines[Line::start + y].render(1);
        }
      } else {
        //standard 240p (progressive) and 480i (interlaced) rendering
        ppu.lines[Line::start + y].render(ppu.field());
      }
    }
    Line::start = 0;
    Line::count = 0;
  }
}

auto PPU::Line::cache() -> void {
  cacheBackground(ppu.io.bg1);
  cacheBackground(ppu.io.bg2);
  cacheBackground(ppu.io.bg3);
  cacheBackground(ppu.io.bg4);

  uint y = ppu.vcounter();
  if(ppu.io.displayDisable || y >= ppu.vdisp()) {
    io.displayDisable = true;
  } else {
    memcpy(&io, &ppu.io, sizeof(io));
    memcpy(&cgram, &ppu.cgram, sizeof(cgram));
  }
  if(!Line::count) Line::start = y;
  Line::count++;
}

auto PPU::Line::avgBgC(uint dist, uint offset) const -> uint32 {
  auto luma = ppu.lightTable[io.displayBrightness];
  uint32 t = luma[io.col.fixedColor];
  if(dist < 1) return t;
  uint32 a = (t >> 16) & 255;
  uint32 b = (t >>  8) & 255;
  uint32 c = (t >>  0) & 255;
  int scale = ppufast.hd() ? ppufast.hdScale() : 1;
  int hdY = y * scale + offset;
  int count = 1;
  for (int i = 1; i <= dist*scale; i++) {
    int uY = (hdY-i)/scale;
    int dY = (hdY+i)/scale;
    if(uY < 0 || dY >= 224) break; /////////////////////////////
    auto uL = ppufast.lines[uY];
    auto dL = ppufast.lines[dY];
    if( io.col.halve     != dL.io.col.halve     || io.col.halve     != uL.io.col.halve     ||
        io.col.mathMode  != dL.io.col.mathMode  || io.col.mathMode  != uL.io.col.mathMode  ||
        io.col.blendMode != dL.io.col.blendMode || io.col.blendMode != uL.io.col.blendMode ||
        io.col.enable[0] != dL.io.col.enable[0] || io.col.enable[0] != uL.io.col.enable[0] ||
        io.col.enable[1] != dL.io.col.enable[1] || io.col.enable[1] != uL.io.col.enable[1] ||
        io.col.enable[2] != dL.io.col.enable[2] || io.col.enable[2] != uL.io.col.enable[2] ||
        io.col.enable[3] != dL.io.col.enable[3] || io.col.enable[3] != uL.io.col.enable[3] ||
        io.col.enable[4] != dL.io.col.enable[4] || io.col.enable[4] != uL.io.col.enable[4] ||
        io.col.enable[5] != dL.io.col.enable[5] || io.col.enable[5] != uL.io.col.enable[5] ||
        io.col.enable[6] != dL.io.col.enable[6] || io.col.enable[6] != uL.io.col.enable[6] ||
        io.bg1.tileMode  != dL.io.bg1.tileMode  || io.bg1.tileMode  != uL.io.bg1.tileMode  ||
        io.bg2.tileMode  != dL.io.bg2.tileMode  || io.bg2.tileMode  != uL.io.bg2.tileMode  ||
        io.bg3.tileMode  != dL.io.bg3.tileMode  || io.bg3.tileMode  != uL.io.bg3.tileMode  ||
        io.bg4.tileMode  != dL.io.bg4.tileMode  || io.bg4.tileMode  != uL.io.bg4.tileMode) break; 
    t = luma[uL.io.col.fixedColor];
    a += (t >> 16) & 255;
    b += (t >>  8) & 255;
    c += (t >>  0) & 255;
    t = luma[dL.io.col.fixedColor];
    a += (t >> 16) & 255;
    b += (t >>  8) & 255;
    c += (t >>  0) & 255;
    count += 2;
  }
  a /= count;
  b /= count;
  c /= count;
  return (a << 16) + (b << 8) + (c << 0);
}

auto PPU::Line::render(bool fieldID) -> void {
  this->fieldID = fieldID;
  uint y = this->y + (!ppu.latch.overscan ? 7 : 0);

  auto hd = ppu.hd();
  auto ss = ppu.ss();
  auto scale = ppufast.hd() ? ppufast.hdScale() : 1;
  auto output = ppu.output + (!hd
  ? (y * 1024 + (ppu.interlace() && field() ? 512 : 0))
  : (y * (256+2*ppu.widescreen()) * scale * scale)
  );
  auto width = (!hd
  ? (!ppu.hires() ? 256 : 512)
  : ((256+2*ppu.widescreen()) * scale * scale));

  if(io.displayDisable) {
    memory::fill<uint32>(output, width);
    return;
  }

  bool hires = io.pseudoHires || io.bgMode == 5 || io.bgMode == 6;
  
  auto luma = ppu.lightTable[io.displayBrightness];
  auto aboveColor = luma[cgram[0]];
  uint32 *bgFixedColors = new uint32[10];
  uint32 *belowColors = new uint32[10];
  for (int i = 0; i < scale; i++) {
    bgFixedColors[i] = avgBgC(ppufast.bgGrad(), i);
    belowColors[i]  = hires ? aboveColor : bgFixedColors[i];
  }

  uint xa =  (hd || ss) && ppu.interlace() && field() ? (256+2*ppu.widescreen())  * scale * scale / 2 : 0;
  uint xb = !(hd || ss) ? 256 : ppu.interlace() && !ppu.field() ? (256+2*ppu.widescreen()) * scale * scale / 2 : (256+2*ppu.widescreen()) * scale * scale;
  if (hd && ppu.wsBgCol() && ppu.widescreen() > 0) {
    for(uint x = xa; x < xb; x++) {
      int cx = (x % ((256+2*ppu.widescreen()) * scale)) - (ppu.widescreen() * scale);
      if (cx >= 0 && cx <= (256 * scale)) {
        above[x] = {Source::COL, 0, aboveColor};
        below[x] = {Source::COL, 0, belowColors[x / ((256+2*ppufast.widescreen()) * scale)]};
      } else {
        above[x] = {Source::COL, 0, 0};
        below[x] = {Source::COL, 0, 0};
      }
    }
  } else {
    for(uint x = xa; x < xb; x++) {
      above[x] = {Source::COL, 0, aboveColor};
      below[x] = {Source::COL, 0, belowColors[x / ((256+2*ppufast.widescreen()) * scale)]};
    }
  }

  //hack: generally, renderBackground/renderObject ordering do not matter.
  //but for HD mode 7, a larger grid of pixels are generated, and so ordering ends up mattering.
  //as a hack for Mohawk & Headphone Jack, we reorder things for BG2 to render properly.
  //longer-term, we need to devise a better solution that can work for every game.
  renderBackground(io.bg1, Source::BG1);
  if(io.extbg == 0) renderBackground(io.bg2, Source::BG2);
  renderBackground(io.bg3, Source::BG3);
  renderBackground(io.bg4, Source::BG4);
  renderObject(io.obj);
  if(io.extbg == 1) renderBackground(io.bg2, Source::BG2);

  //TODO: move to own method
  uint windRad = ppufast.windRad();
  for (int offset = 0; offset < scale; offset++) {
    uint oneLeft  = io.window.oneLeft;
    uint oneRight = io.window.oneRight;
    uint twoLeft  = io.window.twoLeft;
    uint twoRight = io.window.twoRight;

    int hdY = y * scale + offset;
    int count = 1;
    for (int i = 1; i <= windRad*scale; i++) {
      int uY = (hdY-i)/scale;
      int dY = (hdY+i)/scale;
      if(uY <= 0 || dY >= 224) break;
      auto uL = ppufast.lines[uY];
      auto dL = ppufast.lines[dY];

      if( io.col.halve     != dL.io.col.halve     || io.col.halve     != uL.io.col.halve     ||
          io.col.mathMode  != dL.io.col.mathMode  || io.col.mathMode  != uL.io.col.mathMode  ||
          io.col.blendMode != dL.io.col.blendMode || io.col.blendMode != uL.io.col.blendMode ||
          (io.window.oneLeft >= io.window.oneRight) != (dL.io.window.oneLeft >= dL.io.window.oneRight) ||
          (io.window.oneLeft >= io.window.oneRight) != (uL.io.window.oneLeft >= uL.io.window.oneRight) ||
          (io.window.twoLeft >= io.window.twoRight) != (dL.io.window.twoLeft >= dL.io.window.twoRight) ||
          (io.window.twoLeft >= io.window.twoRight) != (uL.io.window.twoLeft >= uL.io.window.twoRight) ||
          io.col.window.oneEnable != dL.io.col.window.oneEnable ||
          io.col.window.oneEnable != uL.io.col.window.oneEnable ||
          io.col.window.oneInvert != dL.io.col.window.oneInvert ||
          io.col.window.oneInvert != uL.io.col.window.oneInvert ||
          io.col.window.twoEnable != dL.io.col.window.twoEnable ||
          io.col.window.twoEnable != uL.io.col.window.twoEnable ||
          io.col.window.twoInvert != dL.io.col.window.twoInvert ||
          io.col.window.twoInvert != uL.io.col.window.twoInvert ||
          io.col.window.mask != dL.io.col.window.mask ||
          io.col.window.mask != uL.io.col.window.mask ||
          io.col.window.aboveMask != dL.io.col.window.aboveMask ||
          io.col.window.aboveMask != uL.io.col.window.aboveMask ||
          io.col.window.belowMask != dL.io.col.window.belowMask ||
          io.col.window.belowMask != uL.io.col.window.belowMask
          ) break; 

      oneLeft  += dL.io.window.oneLeft  + uL.io.window.oneLeft;
      oneRight += dL.io.window.oneRight + uL.io.window.oneRight;
      twoLeft  += dL.io.window.twoLeft  + uL.io.window.twoLeft;
      twoRight += dL.io.window.twoRight + uL.io.window.twoRight;

      count += 2;
    }
    oneLeft  = oneLeft  * scale / count;
    oneRight = oneRight * scale / count + scale - 1;
    twoLeft  = twoLeft  * scale / count;
    twoRight = twoRight * scale / count + scale - 1;

    renderWindow(io.col.window, io.col.window.aboveMask, windowAbove,
                oneLeft, oneRight, twoLeft, twoRight, scale, 256*scale*offset);
    renderWindow(io.col.window, io.col.window.belowMask, windowBelow,
                oneLeft, oneRight, twoLeft, twoRight, scale, 256*scale*offset);
  }

  uint wsm = (ppu.widescreen() == 0 || ppu.wsOverride()) ? 0 : ppu.wsMarker();
  uint wsma = ppu.wsMarkerAlpha();

  uint curr = 0, prev = 0;
  if(hd) {
    int x = 0;
    int xWindow = 0;
    for(uint ySub : range(scale)) {
      for(uint i : range(ppufast.widescreen() * scale)) {
        *output++ = pixel(xWindow, above[x], below[x], wsm, wsma, bgFixedColors[ySub]);
        x++;
      }
      for(uint i : range(256 * scale)) {
        *output++ = pixel(xWindow, above[x], below[x], wsm, wsma, bgFixedColors[ySub]);
        x++;
        xWindow++;
      }
      xWindow--;
      for(uint i : range(ppufast.widescreen() * scale)) {
        *output++ = pixel(xWindow, above[x], below[x], wsm, wsma, bgFixedColors[ySub]);
        x++;
      }
      xWindow++;
    }

  } else if(width == 256) for(uint x : range(256)) {
    *output++ = pixel(x, above[x], below[x], wsm, wsma, bgFixedColors[0]);
  } else if(!hires) for(uint x : range(256)) {
    auto color = pixel(x, above[x], below[x], wsm, wsma, bgFixedColors[0]);
    *output++ = color;
    *output++ = color;
  } else if(!configuration.video.blurEmulation) for(uint x : range(256)) {
    *output++ = pixel(x, below[x], above[x], wsm, wsma, bgFixedColors[0]);
    *output++ = pixel(x, above[x], below[x], wsm, wsma, bgFixedColors[0]);
  } else for(uint x : range(256)) {
    curr = pixel(x, below[x], above[x], wsm, wsm, bgFixedColors[0]);
    *output++ = (prev + curr - ((prev ^ curr) & 0x00010101)) >> 1;
    prev = curr;
    curr = pixel(x, above[x], below[x], wsm, wsma, bgFixedColors[0]);
    *output++ = (prev + curr - ((prev ^ curr) & 0x00010101)) >> 1;
    prev = curr;
  }
}

auto PPU::Line::pixel(uint x, Pixel above, Pixel below, uint wsm, uint wsma, uint32 bgFixedColor) const -> uint32 { 
  uint32 r = 0;
  if(!windowAbove[x]) above.color = 0x0000;
  if(!windowBelow[x]) r = above.color;
  else if(!io.col.enable[above.source]) r = above.color;
  else if(!io.col.blendMode) r = blend(above.color, bgFixedColor, io.col.halve && windowAbove[x]);
  else r = blend(above.color, below.color, io.col.halve && windowAbove[x] && below.source != Source::COL);
  if(wsm > 0) {
    x = (x / ppufast.hdScale()) % 256;
    if(wsm == 1 && (x == 1 || x == 254)
        || wsm == 2 && (x == 0 || x == 255)) {
      int b = wsm == 2 ? 0 : ((y / 4) % 2 == 0) ? 0 : 255;
      r = ((((((r >> 16) & 255) * wsma) + b) / (wsma + 1)) << 16)
        + ((((((r >>  8) & 255) * wsma) + b) / (wsma + 1)) <<  8)
        + ((((((r >>  0) & 255) * wsma) + b) / (wsma + 1)) <<  0);
    }
  }
  return r;
}

auto PPU::Line::blend(uint x, uint y, bool halve) const -> uint32 {
  if(!io.col.mathMode) {  //add
    if(!halve) {
      uint sum = x + y;
      uint carry = (sum - ((x ^ y) & 0x00010101)) & 0x01010100;
      return (sum - carry) | (carry - (carry >> 8));
    } else {
      return (x + y - ((x ^ y) & 0x00010101)) >> 1;
    }
  } else {  //sub
    uint diff = x - y + 0x01010100;
    uint borrow = (diff - ((x ^ y) & 0x01010100)) & 0x01010100;
    if(!halve) {
      return   (diff - borrow) & (borrow - (borrow >> 8));
    } else {
      return (((diff - borrow) & (borrow - (borrow >> 8))) & 0x00fefefe) >> 1;
    }
  }
}

auto PPU::Line::directColor(uint paletteIndex, uint paletteColor) const -> uint32 {
  //paletteIndex = bgr
  //paletteColor = BBGGGRRR
  //output       = 0 BBb00 GGGg0 RRRr0
  return (paletteColor << 2 & 0x001c) + (paletteIndex <<  1 & 0x0002)   //R
       + (paletteColor << 4 & 0x0380) + (paletteIndex <<  5 & 0x0040)   //G
       + (paletteColor << 7 & 0x6000) + (paletteIndex << 10 & 0x1000);  //B
}

auto PPU::Line::plotAbove(int x, uint8 source, uint8 priority, uint32 color) -> void {
  if(ppu.hd() || ppu.ss()) return plotHD(above, x, source, priority, color, false, false);
  if(priority > above[x].priority) above[x] = {source, priority, color};
}

auto PPU::Line::plotBelow(int x, uint8 source, uint8 priority, uint32 color) -> void {
  if(ppu.hd() || ppu.ss()) return plotHD(below, x, source, priority, color, false, false);
  if(priority > below[x].priority) below[x] = {source, priority, color};
}

//todo: name these variables more clearly ...
auto PPU::Line::plotHD(Pixel* pixel, int x, uint8 source, uint8 priority, uint32 color, bool hires, bool subpixel) -> void {
  int scale = ppu.hdScale();
  int wss = ppu.widescreen() * scale;
  int xss = hires && subpixel ? scale / 2 : 0;
  int ys = ppu.interlace() && ppu.field() ? scale / 2 : 0;
  if(priority > pixel[x * scale + xss + ys * 256 * scale + wss].priority) {
    Pixel p = {source, priority, color};
    int xsm = hires && !subpixel ? scale / 2 : scale;
    int ysm = ppu.interlace() && !ppu.field() ? scale / 2 : scale;
    for(int xs = xss; xs < xsm; xs++) {
      pixel[x * scale + xs + ys * 256 * scale + wss] = p;
    }
    int size = sizeof(Pixel) * (xsm - xss);
    Pixel* source = &pixel[x * scale + xss + ys * 256 * scale + wss];
    for(int yst = ys + 1; yst < ysm; yst++) {
      memcpy(&pixel[x * scale + xss + yst * (256+2*ppu.widescreen()) * scale + wss], source, size);
    }
  }
}
