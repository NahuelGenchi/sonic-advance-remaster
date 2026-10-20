/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Stage 1 GLSL 330 core shaders for the GPU layer path.
 *
 * The shaders are compiled at start-up by the renderer. They are kept in this
 * repository as source, embedded as string literals so the host stays a single
 * self-contained binary and no shader file has to be found at run time.
 *
 * Conventions:
 *   - The scene target is the native 426x240 frame. gl_FragCoord.y is the GBA
 *     scanline, so scanline 0 is the bottom row of the target and the present
 *     pass flips it back to the top of the window.
 *   - Layer targets hold the 5-bit GBA colour components (0..31) in an RGBA8
 *     texture: the byte carries the 5-bit value itself, so the composite pass
 *     recovers it exactly with a multiply by 255. Alpha is 255 where the layer
 *     covers the pixel and 0 where it does not.
 *   - The per-scanline snapshot is a RGBA32UI texture with nine texels per
 *     line; see renderer.c for the layout.
 *   - The tile walk of the background shader reproduces the walk of the
 *     upstream software PPU, including the detail that the last partial tile
 *     of a line is read past the end of the map row.
 */

#ifndef SAR_SHADERS_STAGE1_H
#define SAR_SHADERS_STAGE1_H

#define SAR_GLSL_FULLSCREEN_VS                                                                                                             \
    "#version 330 core\n"                                                                                                                  \
    "out vec2 vUV;\n"                                                                                                                      \
    "void main(void) {\n"                                                                                                                  \
    "    float cx = float(gl_VertexID & 1);\n"                                                                                              \
    "    float cy = float((gl_VertexID >> 1) & 1);\n"                                                                                       \
    "    vec2 pos = vec2(cx * 2.0 - 1.0, cy * 2.0 - 1.0);\n"                                                                                \
    "    vUV = pos * 0.5 + 0.5;\n"                                                                                                          \
    "    gl_Position = vec4(pos, 0.0, 1.0);\n"                                                                                              \
    "}\n"

#define SAR_GLSL_BG_FS                                                                                                                     \
    "#version 330 core\n"                                                                                                                  \
    "uniform usampler2D uVram;\n"                                                                                                           \
    "uniform usampler2D uPal;\n"                                                                                                            \
    "uniform usampler2D uLine;\n"                                                                                                           \
    "uniform int uLayer;\n"                                                                                                                 \
    "in vec2 vUV;\n"                                                                                                                        \
    "out vec4 fragColor;\n"                                                                                                                 \
    "int S16(uint v) {\n"                                                                                                                   \
    "    int x = int(v & 0xFFFFu);\n"                                                                                                       \
    "    if (x >= 0x8000) { x -= 0x10000; }\n"                                                                                              \
    "    return x;\n"                                                                                                                       \
    "}\n"                                                                                                                                  \
    "uint VramByte(uint off) {\n"                                                                                                           \
    "    return texelFetch(uVram, ivec2(int(off & 1023u), int(off >> 10)), 0).r;\n"                                                        \
    "}\n"                                                                                                                                  \
    "// Reproduces the tile walk of the software PPU for one scanline. The PPU\n"                                                              \
    "// walks the map linearly and restarts at the first map entry when the map\n"                                                            \
    "// width is used up, but the last partial tile of the line is read past\n"                                                             \
    "// the end of the map row.\n"                                                                                                          \
    "uint PpuTextTile(uint hofs, uint mapWidth, int x, uint screenWidth, out uint pixelInTile) {\n"                                    \
    "    uint h = hofs % 512u;\n"                                                                                                            \
    "    if ((mapWidth & 256u) == 0u) { h = h % 256u; }\n"                                                                                    \
    "    uint ph = h % 8u;\n"                                                                                                                \
    "    uint head = (ph != 0u) ? (8u - ph) : 0u;\n"                                                                                          \
    "    uint run = mapWidth - h;\n"                                                                                                         \
    "    uint tile;\n"                                                                                                                       \
    "    if (uint(x) < head) {\n"                                                                                                            \
    "        tile = h / 8u;\n"                                                                                                                \
    "    } else {\n"                                                                                                                           \
    "        uint rest = uint(x) - head;\n"                                                                                                   \
    "        uint avail = (screenWidth > head) ? (screenWidth - head) : 0u;\n"                                                                 \
    "        uint runLeft = (run > head) ? (run - head) : 0u;\n"                                                                               \
    "        uint full = ((avail < runLeft) ? avail : runLeft) / 8u;\n"                                                                        \
    "        if (rest < full * 8u) {\n"                                                                                                       \
    "            tile = h / 8u + (head > 0u ? 1u : 0u) + rest / 8u;\n"                                                                          \
    "        } else {\n"                                                                                                                       \
    "            uint tail = rest - full * 8u;\n"                                                                                              \
    "            uint runRest = (runLeft > full * 8u) ? (runLeft - full * 8u) : 0u;\n"                                                        \
    "            uint base = (runRest == 0u) ? 0u : (h / 8u + (head > 0u ? 1u : 0u) + full);\n"                                             \
    "            uint left = (avail > full * 8u) ? (avail - full * 8u) : 0u;\n"                                                              \
    "            uint whole = left / 8u;\n"                                                                                                   \
    "            tile = (tail < whole * 8u) ? (base + tail / 8u) : (base + whole);\n"                                                          \
    "        }\n"                                                                                                                            \
    "    }\n"                                                                                                                                \
    "    pixelInTile = (uint(x) + h) % 8u;\n"                                                                                                 \
    "    return tile;\n"                                                                                                                     \
    "}\n"                                                                                                                                  \
    "void main(void) {\n"                                                                                                                  \
    "    int x = int(gl_FragCoord.x);\n"                                                                                                     \
    "    int y = int(gl_FragCoord.y);\n"                                                                                                     \
    "    uvec4 hofsV = texelFetch(uLine, ivec2(0, y), 0);\n"                                                                                  \
    "    uvec4 vofsV = texelFetch(uLine, ivec2(1, y), 0);\n"                                                                                  \
    "    uvec4 cnts = texelFetch(uLine, ivec2(2, y), 0);\n"                                                                                   \
    "    uvec4 aff2 = texelFetch(uLine, ivec2(3, y), 0);\n"                                                                                   \
    "    uvec4 aff3 = texelFetch(uLine, ivec2(4, y), 0);\n"                                                                                   \
    "    uvec4 arefV = texelFetch(uLine, ivec2(5, y), 0);\n"                                                                                  \
    "    uvec4 ctl = texelFetch(uLine, ivec2(6, y), 0);\n"                                                                                    \
    "    uvec4 misc = texelFetch(uLine, ivec2(7, y), 0);\n"                                                                                   \
    "    uint dispcnt = ctl.x;\n"                                                                                                            \
    "    uint videoMode = dispcnt & 7u;\n"                                                                                                    \
    "    uint hofs[4] = uint[4](hofsV.x, hofsV.y, hofsV.z, hofsV.w);\n"                                                                       \
    "    uint vofs[4] = uint[4](vofsV.x, vofsV.y, vofsV.z, vofsV.w);\n"                                                                       \
    "    uint cntsA[4] = uint[4](cnts.x, cnts.y, cnts.z, cnts.w);\n"                                                                          \
    "    uint cnt = cntsA[uLayer];\n"                                                                                                        \
    "    bool affine = (uLayer >= 2) && ((videoMode == 2u) || (videoMode == 1u && uLayer == 2));\n"                                          \
    "    uint mosaicReg = misc.x;\n"                                                                                                          \
    "    uint mosh = (mosaicReg & 0xFu) + 1u;\n"                                                                                             \
    "    uint mosv = ((mosaicReg >> 4) & 0xFu) + 1u;\n"                                                                                      \
    "    bool bgMosaic = (cnt & 0x40u) != 0u && (mosaicReg & 0xFFu) != 0u;\n"                                                                  \
    "    uint screenBase = (cnt >> 8) & 0x3Fu;\n"                                                                                             \
    "    uint affineScreenBase = (cnt >> 8) & 0x1Fu;\n"                                                                                       \
    "    uint charBase = (cnt >> 2) & 3u;\n"                                                                                                  \
    "    if (affine) {\n"                                                                                                                    \
    "        uvec4 aff = (uLayer == 2) ? aff2 : aff3;\n"                                                                                      \
    "        int pa = S16(aff.x);\n"                                                                                                         \
    "        int pc = S16(aff.z);\n"                                                                                                         \
    "        int arefX = int((uLayer == 2) ? arefV.x : arefV.z);\n"                                                                          \
    "        int arefY = int((uLayer == 2) ? arefV.y : arefV.w);\n"                                                                          \
    "        int sx = x;\n"                                                                                                                  \
    "        int sy = y;\n"                                                                                                                  \
    "        if (bgMosaic) {\n"                                                                                                              \
    "            sx = x - (x % int(mosh));\n"                                                                                                 \
    "            sy = y - (y % int(mosv));\n"                                                                                                 \
    "        }\n"                                                                                                                            \
    "        int sourceX = arefX + sx * pa;\n"                                                                                               \
    "        int sourceY = arefY + sx * pc;\n"                                                                                               \
    "        uint mapSize = (cnt >> 14) & 3u;\n"                                                                                             \
    "        uint wh = 128u << mapSize;\n"                                                                                                   \
    "        int px = sourceX >> 8;\n"                                                                                                        \
    "        int py = sourceY >> 8;\n"                                                                                                        \
    "        bool wrap = ((cnt >> 13) & 1u) != 0u;\n"                                                                                         \
    "        if (wrap) {\n"                                                                                                                   \
    "            px = int(uint(px) & (wh - 1u));\n"                                                                                          \
    "            py = int(uint(py) & (wh - 1u));\n"                                                                                          \
    "        } else if (px < 0 || py < 0 || px >= int(wh) || py >= int(wh)) {\n"                                                                \
    "            discard;\n"                                                                                                                  \
    "        }\n"                                                                                                                            \
    "        uint mapOff = affineScreenBase * 0x800u + (uint(py) >> 3u) * (wh >> 3u) + (uint(px) >> 3u);\n"      \
    "        uint tile = VramByte(mapOff);\n"                                                                                                 \
    "        uint tOff = charBase * 0x4000u + tile * 64u + (uint(py) & 7u) * 8u + (uint(px) & 7u);\n"        \
    "        uint idx = VramByte(tOff);\n"                                                                                                    \
    "        if (idx == 0u) { discard; }\n"                                                                                                    \
    "        uint pal = texelFetch(uPal, ivec2(int(idx), 0), 0).r;\n"                                                                          \
    "        fragColor = vec4(float(pal & 31u) / 255.0, float((pal >> 5) & 31u) / 255.0,\n"                                                \
    "            float((pal >> 10) & 31u) / 255.0, 1.0);\n"                                                                                 \
    "        return;\n"                                                                                                                       \
    "    }\n"                                                                                                                                \
    "    uint mapSize = (cnt >> 14) & 3u;\n"                                                                                                 \
    "    uint mapW = ((mapSize & 1u) != 0u) ? 512u : 256u;\n"                                                                                  \
    "    uint mapH = ((mapSize & 2u) != 0u) ? 512u : 256u;\n"                                                                                  \
    "    bool is8bpp = (cnt & 0x80u) != 0u;\n"                                                                                               \
    "    int sx = x;\n"                                                                                                                       \
    "    int sy = y;\n"                                                                                                                       \
    "    if (bgMosaic) {\n"                                                                                                                   \
    "        sx = x - (x % int(mosh));\n"                                                                                                      \
    "        sy = y - (y % int(mosv));\n"                                                                                                      \
    "    }\n"                                                                                                                                 \
    "    uint my = (uint(sy) + vofs[uLayer]) % mapH;\n"                                                                                        \
    "    uint mapStride = mapW >> 3u;\n"                                                                                                       \
    "    uint blockOffset = (mapH == 512u && my >= 256u) ? (mapStride * 32u) : 0u;\n"                                                          \
    "    uint rowOffset = blockOffset + ((my % 256u) >> 3u) * mapStride;\n"                                                                      \
    "    uint pixelInTile;\n"                                                                                                                \
    "    uint tileIndex = PpuTextTile(hofs[uLayer], mapW, x, 426u, pixelInTile);\n"                                                            \
    "    uint mapOff = screenBase * 0x800u + (rowOffset + tileIndex) * 2u;\n"                                                                  \
    "    uint entry = VramByte(mapOff) | (VramByte(mapOff + 1u) << 8u);\n"                                                                      \
    "    uint tile = entry & 0x3FFu;\n"                                                                                                        \
    "    bool hflip = (entry & 0x400u) != 0u;\n"                                                                                               \
    "    bool vflip = (entry & 0x800u) != 0u;\n"                                                                                               \
    "    uint palBank = (entry >> 12) & 0xFu;\n"                                                                                              \
    "    uint px = pixelInTile;\n"                                                                                                            \
    "    uint py = my & 7u;\n"                                                                                                                \
    "    if (hflip) { px = 7u - px; }\n"                                                                                                      \
    "    if (vflip) { py = 7u - py; }\n"                                                                                                      \
    "    uint tOff = charBase * 0x4000u + tile * (is8bpp ? 64u : 32u)\n"                                                                      \
    "        + py * (is8bpp ? 8u : 4u) + (is8bpp ? px : (px >> 1u));\n"                                                                       \
    "    uint idx;\n"                                                                                                                        \
    "    if (is8bpp) {\n"                                                                                                                     \
    "        idx = VramByte(tOff);\n"                                                                                                        \
    "    } else {\n"                                                                                                                           \
    "        uint b = VramByte(tOff);\n"                                                                                                     \
    "        idx = ((px & 1u) == 0u) ? (b & 0xFu) : (b >> 4u);\n"                                                                              \
    "    }\n"                                                                                                                                \
    "    if (idx == 0u) { discard; }\n"                                                                                                       \
    "    uint palIdx = is8bpp ? idx : (palBank * 16u + idx);\n"                                                                               \
    "    uint pal = texelFetch(uPal, ivec2(int(palIdx), 0), 0).r;\n"                                                                          \
    "    fragColor = vec4(float(pal & 31u) / 255.0, float((pal >> 5) & 31u) / 255.0,\n"                                                \
    "        float((pal >> 10) & 31u) / 255.0, 1.0);\n"                                                                                 \
    "}\n"

#define SAR_GLSL_OBJ_VS                                                                                                                     \
    "#version 330 core\n"                                                                                                                  \
    "uniform usampler2D uObj;\n"                                                                                                            \
    "uniform vec2 uScreen;\n"                                                                                                               \
    "layout(location = 0) in int aIndex;\n"                                                                                                  \
    "flat out int vIndex;\n"                                                                                                                \
    "int S16(uint v) {\n"                                                                                                                   \
    "    int x = int(v & 0xFFFFu);\n"                                                                                                       \
    "    if (x >= 0x8000) { x -= 0x10000; }\n"                                                                                              \
    "    return x;\n"                                                                                                                       \
    "}\n"                                                                                                                                  \
    "void main(void) {\n"                                                                                                                  \
    "    uvec4 t0 = texelFetch(uObj, ivec2(aIndex, 0), 0);\n"                                                                                 \
    "    uvec4 t1 = texelFetch(uObj, ivec2(aIndex, 1), 0);\n"                                                                                 \
    "    int x = S16(t0.x);\n"                                                                                                              \
    "    int y = S16(t0.y);\n"                                                                                                              \
    "    uint flags = t0.z;\n"                                                                                                              \
    "    bool isDouble = (flags & 2u) != 0u;\n"                                                                                             \
    "    int w = int(t1.x);\n"                                                                                                              \
    "    int h = int(t1.y);\n"                                                                                                              \
    "    int rw = isDouble ? w * 2 : w;\n"                                                                                                  \
    "    int rh = isDouble ? h * 2 : h;\n"                                                                                                  \
    "    float cx = float(gl_VertexID & 1);\n"                                                                                              \
    "    float cy = float((gl_VertexID >> 1) & 1);\n"                                                                                       \
    "    int px = x + int(cx * float(rw));\n"                                                                                                \
    "    int py = y + int(cy * float(rh));\n"                                                                                                \
    "    gl_Position = vec4(float(px) / uScreen.x * 2.0 - 1.0, float(py) / uScreen.y * 2.0 - 1.0, 0.0, 1.0);\n" \
    "    vIndex = aIndex;\n"                                                                                                                \
    "}\n"

#define SAR_GLSL_OBJ_FS                                                                                                                     \
    "#version 330 core\n"                                                                                                                  \
    "uniform usampler2D uObj;\n"                                                                                                            \
    "uniform usampler2D uVram;\n"                                                                                                           \
    "uniform usampler2D uPal;\n"                                                                                                            \
    "uniform usampler2D uLine;\n"                                                                                                           \
    "uniform int uObjPass;\n"                                                                                                               \
    "flat in int vIndex;\n"                                                                                                                 \
    "out vec4 fragColor;\n"                                                                                                                 \
    "int S16(uint v) {\n"                                                                                                                   \
    "    int x = int(v & 0xFFFFu);\n"                                                                                                       \
    "    if (x >= 0x8000) { x -= 0x10000; }\n"                                                                                              \
    "    return x;\n"                                                                                                                       \
    "}\n"                                                                                                                                  \
    "uint VramByte(uint off) {\n"                                                                                                           \
    "    return texelFetch(uVram, ivec2(int(off & 1023u), int(off >> 10)), 0).r;\n"                                                        \
    "}\n"                                                                                                                                  \
    "void main(void) {\n"                                                                                                                  \
    "    int px = int(gl_FragCoord.x);\n"                                                                                                     \
    "    int py = int(gl_FragCoord.y);\n"                                                                                                     \
    "    uvec4 t0 = texelFetch(uObj, ivec2(vIndex, 0), 0);\n"                                                                                \
    "    uvec4 t1 = texelFetch(uObj, ivec2(vIndex, 1), 0);\n"                                                                                \
    "    uvec4 t2 = texelFetch(uObj, ivec2(vIndex, 2), 0);\n"                                                                                \
    "    int ox = S16(t0.x);\n"                                                                                                              \
    "    int oy = S16(t0.y);\n"                                                                                                              \
    "    uint flags = t0.z;\n"                                                                                                              \
    "    uint tile = t0.w & 0x3FFu;\n"                                                                                                       \
    "    uint prio = (t0.w >> 10) & 3u;\n"                                                                                                    \
    "    uint palBank = (t0.w >> 12) & 0xFu;\n"                                                                                              \
    "    bool isAffine = (flags & 1u) != 0u;\n"                                                                                             \
    "    bool isDouble = (flags & 2u) != 0u;\n"                                                                                             \
    "    bool is8bpp = (flags & 4u) != 0u;\n"                                                                                               \
    "    bool hflip = (flags & 8u) != 0u;\n"                                                                                                \
    "    bool vflip = (flags & 16u) != 0u;\n"                                                                                               \
    "    bool mosaic = (flags & 32u) != 0u;\n"                                                                                              \
    "    uint objMode = (flags >> 8) & 3u;\n"                                                                                                \
    "    int w = int(t1.x);\n"                                                                                                               \
    "    int h = int(t1.y);\n"                                                                                                               \
    "    int pa = S16(t1.z);\n"                                                                                                              \
    "    int pb = S16(t1.w);\n"                                                                                                              \
    "    int pc = S16(t2.x);\n"                                                                                                              \
    "    int pd = S16(t2.y);\n"                                                                                                              \
    "    uvec4 ctl = texelFetch(uLine, ivec2(6, py), 0);\n"                                                                                  \
    "    uvec4 misc = texelFetch(uLine, ivec2(7, py), 0);\n"                                                                                 \
    "    uint dispcnt = ctl.x;\n"                                                                                                            \
    "    bool obj1d = (dispcnt & 0x40u) != 0u;\n"                                                                                           \
    "    uint mosaicReg = misc.x;\n"                                                                                                          \
    "    bool mosaicActive = mosaic && (mosaicReg & 0xFF00u) != 0u;\n"                                                                         \
    "    uint mosv = ((mosaicReg >> 12) & 0xFu) + 1u;\n"                                                                                      \
    "    uint mosh = ((mosaicReg >> 8) & 0xFu) + 1u;\n"                                                                                      \
    "    uint tileSize = is8bpp ? 64u : 32u;\n"                                                                                              \
    "    uint tileWidth = is8bpp ? 8u : 4u;\n"                                                                                              \
    "    uint pitch = obj1d ? (uint(w) / 8u) * tileSize : 1024u;\n"                                                                           \
    "    uint idx;\n"                                                                                                                        \
    "    if (!isAffine) {\n"                                                                                                                 \
    "        int lx = px - ox;\n"                                                                                                            \
    "        int ly = py - oy;\n"                                                                                                            \
    "        if (lx < 0 || ly < 0 || lx >= w || ly >= h) { discard; }\n"                                                                      \
    "        int rowCoord = vflip ? (h - 1 - ly) : ly;\n"                                                                                     \
    "        int colCoord = lx;\n"                                                                                                           \
    "        if (mosaicActive) {\n"                                                                                                          \
    "            rowCoord -= rowCoord % int(mosv);\n"                                                                                        \
    "            colCoord -= colCoord % int(mosh);\n"                                                                                        \
    "        }\n"                                                                                                                            \
    "        int sx = hflip ? (w - 1 - colCoord) : colCoord;\n"                                                                                \
    "        uint tileOff = tile * 32u + (uint(rowCoord) >> 3u) * pitch\n"                                                                      \
    "            + (uint(rowCoord) & 7u) * tileWidth + (uint(sx) >> 3u) * tileSize;\n"                                                          \
    "        uint addr = 0x10000u + ((tileOff\n"                                                                                            \
    "            + (is8bpp ? (uint(sx) & 7u) : ((uint(sx) & 7u) >> 1u))) & 0x7FFFu);\n"                                                         \
    "        uint b = VramByte(addr);\n"                                                                                                      \
    "        idx = is8bpp ? b : (((uint(sx) & 1u) == 0u) ? (b & 0xFu) : (b >> 4u));\n"                                                         \
    "    } else {\n"                                                                                                                           \
    "        int mx = isDouble ? w : (w / 2);\n"                                                                                             \
    "        int my = isDouble ? h : (h / 2);\n"                                                                                             \
    "        int yq = mosaicActive ? (py - (py % int(mosv))) : py;\n"                                                                           \
    "        int xq = mosaicActive ? (px - (px % int(mosh))) : px;\n"                                                                           \
    "        int sourceX = (w << 7) + (yq - oy - my) * pb + (xq - ox - mx) * pa;\n"                                                             \
    "        int sourceY = (h << 7) + (yq - oy - my) * pd + (xq - ox - mx) * pc;\n"                                                             \
    "        int tx = sourceX >> 8;\n"                                                                                                        \
    "        int ty = sourceY >> 8;\n"                                                                                                        \
    "        if (tx < 0 || ty < 0 || tx >= w || ty >= h) { discard; }\n"                                                                      \
    "        uint tileOff = tile * 32u + (uint(ty) >> 3u) * pitch + (uint(tx) >> 3u) * tileSize\n"                 \
    "            + (uint(ty) & 7u) * tileWidth\n"                                                                                             \
    "            + (is8bpp ? (uint(tx) & 7u) : ((uint(tx) & 7u) >> 1u));\n"                                                                    \
    "        uint addr = 0x10000u + (tileOff & 0x7FFFu);\n"                                                                                  \
    "        uint b = VramByte(addr);\n"                                                                                                      \
    "        idx = is8bpp ? b : (((uint(tx) & 1u) == 0u) ? (b & 0xFu) : (b >> 4u));\n"                   \
    "    }\n"                                                                                                                                \
    "    if (idx == 0u) { discard; }\n"                                                                                                       \
    "    uint palIdx = 256u + (is8bpp ? idx : (palBank * 16u + idx));\n"                                                                      \
    "    uint pal = texelFetch(uPal, ivec2(int(palIdx), 0), 0).r;\n"                                                                          \
    "    if (uObjPass == 1) {\n"                                                                                                              \
    "        fragColor = vec4(1.0, 0.0, 0.0, 1.0);\n"                                                                                    \
    "        return;\n"                                                                                                                       \
    "    }\n"                                                                                                                                \
    "    // The alpha byte carries the object priority and the object mode: the\n"                                                                  \
    "    // composite pass needs both to place the object in the layer order.\n"                                                        \
    "    float alpha = 255.0 - float(prio * 4u) - (objMode == 1u ? 1.0 : 0.0);\n"                                                          \
    "    fragColor = vec4(float(pal & 31u) / 255.0, float((pal >> 5) & 31u) / 255.0,\n"                                                \
    "        float((pal >> 10) & 31u) / 255.0, alpha / 255.0);\n"                                                                         \
    "}\n"

#define SAR_GLSL_COMPOSITE_FS                                                                                                               \
    "#version 330 core\n"                                                                                                                  \
    "uniform sampler2D uBg0;\n"                                                                                                             \
    "uniform sampler2D uBg1;\n"                                                                                                             \
    "uniform sampler2D uBg2;\n"                                                                                                             \
    "uniform sampler2D uBg3;\n"                                                                                                             \
    "uniform sampler2D uObj;\n"                                                                                                             \
    "uniform sampler2D uObjWin;\n"                                                                                                           \
    "uniform usampler2D uLine;\n"                                                                                                           \
    "uniform vec3 uBackdrop;\n"                                                                                                              \
    "in vec2 vUV;\n"                                                                                                                        \
    "out vec4 fragColor;\n"                                                                                                                 \
    "bool ModeAllowsBg(uint mode, int layer) {\n"                                                                                            \
    "    if (mode == 0u) { return true; }\n"                                                                                                \
    "    if (mode == 1u) { return layer < 3; }\n"                                                                                           \
    "    if (mode == 2u) { return layer >= 2; }\n"                                                                                           \
    "    return false;\n"                                                                                                                    \
    "}\n"                                                                                                                                  \
    "bool InWindow(uint hreg, uint vreg, int x, int y) {\n"                                                                                   \
    "    int l = int(hreg >> 16);\n"                                                                                                        \
    "    int r = int(hreg & 0xFFFFu);\n"                                                                                                    \
    "    int t = int(vreg >> 16);\n"                                                                                                        \
    "    int b = int(vreg & 0xFFFFu);\n"                                                                                                    \
    "    if (l == r) { return false; }\n"                                                                                                    \
    "    bool inY;\n"                                                                                                                        \
    "    if (t > 307) { inY = false; }\n"                                                                                                  \
    "    else if (b > 307) { inY = true; }\n"                                                                                               \
    "    else if (t > b) { inY = (y > t) || (y <= b); }\n"                                                                                    \
    "    else { inY = (y >= t) && (y < b); }\n"                                                                                              \
    "    if (!inY) { return false; }\n"                                                                                                      \
    "    if (l < r) { return (x >= l) && (x < r); }\n"                                                                                         \
    "    return (x < r) || (x >= l);\n"                                                                                                      \
    "}\n"                                                                                                                                  \
    "bool Target1(uint bldcnt, int id) {\n"                                                                                                 \
    "    uint bit = (id == 4) ? 4u : ((id == 5) ? 5u : uint(id));\n"                                                                           \
    "    return ((bldcnt >> bit) & 1u) != 0u;\n"                                                                                            \
    "}\n"                                                                                                                                  \
    "bool Target2(uint bldcnt, int id) {\n"                                                                                                 \
    "    uint bit = (id == 4) ? 11u : ((id == 5) ? 12u : (uint(id) + 7u));\n"                                                                 \
    "    return ((bldcnt >> bit) & 1u) != 0u;\n"                                                                                            \
    "}\n"                                                                                                                                  \
    "float SrgbToLinear(float c) {\n"                                                                                                       \
    "    if (c <= 0.04045) { return c / 12.92; }\n"                                                                                       \
    "    return pow((c + 0.055) / 1.055, 2.4);\n"                                                                                         \
    "}\n"                                                                                                                                  \
    "void main(void) {\n"                                                                                                                  \
    "    int x = int(gl_FragCoord.x);\n"                                                                                                     \
    "    int y = int(gl_FragCoord.y);\n"                                                                                                     \
    "    uvec4 ctl = texelFetch(uLine, ivec2(6, y), 0);\n"                                                                                  \
    "    uvec4 misc = texelFetch(uLine, ivec2(7, y), 0);\n"                                                                                 \
    "    uvec4 win = texelFetch(uLine, ivec2(8, y), 0);\n"                                                                                  \
    "    uvec4 cnts = texelFetch(uLine, ivec2(2, y), 0);\n"                                                                                   \
    "    uint dispcnt = ctl.x;\n"                                                                                                            \
    "    uint bldcnt = ctl.y;\n"                                                                                                             \
    "    uint winin = ctl.z;\n"                                                                                                             \
    "    uint winout = ctl.w;\n"                                                                                                            \
    "    uint bldalpha = misc.y;\n"                                                                                                          \
    "    uint bldy = misc.z;\n"                                                                                                              \
    "    uint videoMode = dispcnt & 7u;\n"                                                                                                    \
    "    uint prio[4] = uint[4](cnts.x & 3u, cnts.y & 3u, cnts.z & 3u, cnts.w & 3u);\n"                                                       \
    "    bool forcedBlank = (dispcnt & 0x80u) != 0u;\n"                                                                                      \
    "    uint winCtrl = (dispcnt >> 13) & 7u;\n"                                                                                             \
    "    uint enable;\n"                                                                                                                     \
    "    if (winCtrl == 0u) {\n"                                                                                                              \
    "        enable = 0x3Fu;\n"                                                                                                             \
    "    } else {\n"                                                                                                                           \
    "        enable = winout & 0x3Fu;\n"                                                                                                     \
    "        if ((dispcnt & 0x2000u) != 0u && InWindow(win.x, win.y, x, y)) {\n"                                                              \
    "            enable = winin & 0x3Fu;\n"                                                                                                  \
    "        } else if ((dispcnt & 0x4000u) != 0u && InWindow(win.z, win.w, x, y)) {\n"                                                       \
    "            enable = (winin >> 8) & 0x3Fu;\n"                                                                                        \
    "        } else if ((dispcnt & 0x8000u) != 0u && texelFetch(uObjWin, ivec2(x, y), 0).r > 0.5) {\n"  \
    "            enable = (winout >> 8) & 0x3Fu;\n"                                                                                        \
    "        }\n"                                                                                                                            \
    "    }\n"                                                                                                                                \
    "    vec3 cols[5];\n"                                                                                                                    \
    "    float keys[5];\n"                                                                                                                   \
    "    bool valids[5];\n"                                                                                                                  \
    "    int ids[5];\n"                                                                                                                      \
    "    for (int i = 0; i < 5; i++) {\n"                                                                                                \
    "        cols[i] = vec3(0.0);\n"                                                                                                          \
    "        keys[i] = 1.0e9;\n"                                                                                                           \
    "        valids[i] = false;\n"                                                                                                            \
    "        ids[i] = 5;\n"                                                                                                                 \
    "    }\n"                                                                                                                                \
    "    vec4 s0 = texelFetch(uBg0, ivec2(x, y), 0);\n"                                                                                       \
    "    if (((enable & 0x1u) != 0u) && s0.a > 0.0 && ModeAllowsBg(videoMode, 0)) {\n"                                                         \
    "        valids[0] = true; cols[0] = floor(s0.rgb * 255.0 + 0.5); keys[0] = float(prio[0] * 8u + 1u); ids[0] = 0;\n" \
    "    }\n"                                                                                                                                \
    "    vec4 s1 = texelFetch(uBg1, ivec2(x, y), 0);\n"                                                                                       \
    "    if (((enable & 0x2u) != 0u) && s1.a > 0.0 && ModeAllowsBg(videoMode, 1)) {\n"                                                         \
    "        valids[1] = true; cols[1] = floor(s1.rgb * 255.0 + 0.5); keys[1] = float(prio[1] * 8u + 2u); ids[1] = 1;\n" \
    "    }\n"                                                                                                                                \
    "    vec4 s2 = texelFetch(uBg2, ivec2(x, y), 0);\n"                                                                                       \
    "    if (((enable & 0x4u) != 0u) && s2.a > 0.0 && ModeAllowsBg(videoMode, 2)) {\n"                                                         \
    "        valids[2] = true; cols[2] = floor(s2.rgb * 255.0 + 0.5); keys[2] = float(prio[2] * 8u + 3u); ids[2] = 2;\n" \
    "    }\n"                                                                                                                                \
    "    vec4 s3 = texelFetch(uBg3, ivec2(x, y), 0);\n"                                                                                       \
    "    if (((enable & 0x8u) != 0u) && s3.a > 0.0 && ModeAllowsBg(videoMode, 3)) {\n"                                                         \
    "        valids[3] = true; cols[3] = floor(s3.rgb * 255.0 + 0.5); keys[3] = float(prio[3] * 8u + 4u); ids[3] = 3;\n" \
    "    }\n"                                                                                                                                \
    "    vec4 so = texelFetch(uObj, ivec2(x, y), 0);\n"                                                                                       \
    "    uint objAlpha = uint(floor(so.a * 255.0 + 0.5));\n"                                                                                  \
    "    if (((enable & 0x10u) != 0u) && so.a > 0.0) {\n"                                                                                  \
    "        uint rest = 255u - objAlpha;\n"                                                                                             \
    "        uint objPrio = rest / 4u;\n"                                                                                                     \
    "        valids[4] = true; cols[4] = floor(so.rgb * 255.0 + 0.5); keys[4] = float(objPrio * 8u); ids[4] = 4;\n"    \
    "    }\n"                                                                                                                                \
    "    int top = -1;\n"                                                                                                                   \
    "    int second = -1;\n"                                                                                                                 \
    "    for (int i = 0; i < 5; i++) {\n"                                                                                                \
    "        if (!valids[i]) { continue; }\n"                                                                                                  \
    "        if (top < 0 || keys[i] < keys[top]) { second = top; top = i; }\n"                                                                       \
    "        else if (second < 0 || keys[i] < keys[second]) { second = i; }\n"                                                                \
    "    }\n"                                                                                                                                \
    "    vec3 backdrop = uBackdrop;\n"                                                                                                       \
    "    vec3 base = (top >= 0) ? cols[top] : backdrop;\n"                                                                                 \
    "    vec3 below = (second >= 0) ? cols[second] : backdrop;\n"                                                                               \
    "    int baseId = (top >= 0) ? ids[top] : 5;\n"                                                                                          \
    "    int belowId = (second >= 0) ? ids[second] : 5;\n"                                                                                    \
    "    bool stTop = false;\n"                                                                                                               \
    "    if (top == 4) {\n"                                                                                                                      \
    "        uint rest = 255u - objAlpha;\n"                                                                                             \
    "        stTop = (rest % 4u) >= 1u;\n"                                                                                                    \
    "    }\n"                                                                                                                                \
    "    uint effect = (bldcnt >> 6) & 3u;\n"                                                                                                \
    "    bool effectsEnabled = (enable & 0x20u) != 0u;\n"                                                                                      \
    "    bool baseFirst = Target1(bldcnt, baseId);\n"                                                                                        \
    "    bool belowSecond = Target2(bldcnt, belowId);\n"                                                                                        \
    "    bool forceBlend = stTop && belowSecond;\n"                                                                                           \
    "    bool alphaBlend = (top >= 0) && baseFirst && belowSecond && (effect == 1u) && effectsEnabled;\n"  \
    "    vec3 result = base;\n"                                                                                                            \
    "    if (forceBlend || alphaBlend) {\n"                                                                                                   \
    "        float a = float(min(16u, bldalpha & 0x1Fu));\n"                                                                                  \
    "        float b = float(min(16u, (bldalpha >> 8) & 0x1Fu));\n"                                                                              \
    "        vec3 blended = floor((base * a + below * b) / 16.0);\n"                                                                                \
    "        result = min(blended, vec3(31.0));\n"                                                                                          \
    "    } else if (top >= 0) {\n"                                                                                        \
    "        if (effectsEnabled && (effect == 2u || effect == 3u) && baseFirst) {\n"                                                              \
    "            float bright = float(min(16u, bldy & 0x1Fu));\n"                                                                              \
    "            if (effect == 2u) {\n"                                                                                              \
    "                result = min(floor((vec3(31.0) * bright + base * (16.0 - bright)) / 16.0), vec3(31.0));\n" \
    "            } else {\n"                                                                                              \
    "                result = floor(base * (16.0 - bright) / 16.0);\n"                                                                                 \
    "            }\n"                                                                                                                            \
    "        }\n"                                                                                                                            \
    "    } else if (Target1(bldcnt, 5)) {\n"                                                                                        \
    "        if (effect == 2u) {\n"                                                                                                              \
    "            float bright = float(min(16u, bldy & 0x1Fu));\n"                                                                                      \
    "            result = min(floor((vec3(31.0) * bright + base * (16.0 - bright)) / 16.0), vec3(31.0));\n" \
    "        } else if (effect == 3u) {\n"                                                                                          \
    "            float bright = float(min(16u, bldy & 0x1Fu));\n"                                                                                      \
    "            result = floor(base * (16.0 - bright) / 16.0);\n"                                                                                  \
    "        }\n"                                                                                                                            \
    "    }\n"                                                                                                                                \
    "    if (forcedBlank) { result = vec3(31.0); }\n"                                                                                          \
    "    vec3 srgb = result * (8.0 / 255.0);\n"                                                                                                \
    "    fragColor = vec4(SrgbToLinear(srgb.r), SrgbToLinear(srgb.g), SrgbToLinear(srgb.b), 1.0);\n"       \
    "}\n"

#endif // SAR_SHADERS_STAGE1_H
