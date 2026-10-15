#!/usr/bin/env python3
"""phtool.py - extractor / viewer for Pitfall: The Mayan Adventure (Win95) .PH asset files.

Format (reverse engineered from PITFALL.EXE, Kinesoft 1995):

  A .PH file is a *sequential stream* of records.  There is no directory; the
  engine reads records in a fixed order (see manifests.json, derived from the
  decompiled loader FUN_004457d0 / FUN_00447190 / FUN_004460e0).

  record      := u32 size ; u8 body[size]
  sprite body := u32 magic (0x34561234) ; u32 pal_off ; u16 ncolors ; u16 nframes ;
                 u32 frame_off[nframes]            (relative to body+0x10)
                 frames... ; rgb24 palette[ncolors] at pal_off
  frame       := i32 w ; i32 h ; i32 hot_x ; i32 hot_y ; rle rows
  rle         := repeat { b<0x7e : skip b transparent px | 0x7f : end of row |
                          0x7e : end of frame | b>=0x80 : (b-0x7f) literal px follow }
  raw body    := u32 magic ; u32 pal_off ; u16 ncolors ; u16 cell_bytes ; u16 w ; u16 h ;
                 u8 pixels[pal_off-0x10] ; rgb24 palette[ncolors]       (font / scoline)
  bg body     := u32 w_tiles ; u32 h_tiles ; u32 tiles_off ; u32 pal_off ; u32 ncolors-1 ;
                 u16 map[w*h] (bits0-11 tile, 0x2000 vflip, 0x4000 hflip, 0x8000/0x1000 attr)
                 u8 tiles[ntiles][64] (8x8) ; rgb24 palette[ncolors]
  level file  := bg, trg, det, dt2, als, parallax-bg, [extra bg], sprites...
"""
import struct, sys, os, json

MAGIC = 0x34561234

class Record:
    def __init__(self, data, off):
        self.size, = struct.unpack_from('<I', data, off)
        self.off = off + 4
        self.body = data[self.off:self.off + self.size]
        self.end = self.off + self.size

def records(data):
    off = 0
    while off + 4 <= len(data):
        r = Record(data, off)
        if r.size == 0 or r.end > len(data):
            break
        yield r
        off = r.end

def palette(body, pal_off, n):
    return [tuple(body[pal_off + i*3: pal_off + i*3 + 3]) for i in range(n)]

def parse_sprite(body):
    magic, pal_off = struct.unpack_from('<II', body, 0)
    assert magic == MAGIC, hex(magic)
    nc, nf = struct.unpack_from('<HH', body, 8)
    offs = struct.unpack_from('<%dI' % nf, body, 12)
    frames = []
    for o in offs:
        w, h, hx, hy = struct.unpack_from('<iiii', body, o)
        pix = bytearray([0xff]) * (w * h)   # 0xff = transparent
        p = o + 16
        x = y = 0
        while True:
            b = body[p]; p += 1
            if b == 0x7e:
                break
            if b == 0x7f:
                x = 0; y += 1
                continue
            if b < 0x80:
                x += b
                continue
            n = b - 0x7f
            for i in range(n):
                if 0 <= x < w and 0 <= y < h:
                    pix[y*w + x] = body[p]
                p += 1; x += 1
        frames.append(dict(w=w, h=h, hx=hx, hy=hy, pix=bytes(pix)))
    return dict(ncolors=nc, pal=palette(body, pal_off, nc), frames=frames)

def parse_raw(body):
    magic, pal_off = struct.unpack_from('<II', body, 0)
    assert magic == MAGIC
    nc, cell, w, h = struct.unpack_from('<HHHH', body, 8)
    pix = body[16:pal_off]
    return dict(ncolors=nc, pal=palette(body, pal_off, nc), cell=cell, w=w, h=h, pix=pix,
                ncells=len(pix) // cell if cell else 0)

def parse_bg(body):
    w, h, tiles_off, pal_off, ncm1 = struct.unpack_from('<IIIII', body, 0)
    tilemap = struct.unpack_from('<%dH' % (w*h), body, 20)
    tiles = body[tiles_off:pal_off]
    ntiles = len(tiles) // 64
    # bg pixel index 0 is transparent (shows the parallax layer); index i>0 -> palette[i-1]
    return dict(w=w, h=h, tilemap=tilemap, tiles=tiles, ntiles=ntiles,
                ncolors=ncm1 + 1, pal=[(0, 0, 0)] + palette(body, pal_off, ncm1))

def render_bg(bg):
    """Return (w_px, h_px, bytes indices) with 0xff for colour 0?  No - bg is opaque."""
    W, H = bg['w'] * 8, bg['h'] * 8
    out = bytearray(W * H)
    tiles = bg['tiles']; tm = bg['tilemap']; w = bg['w']
    for ty in range(bg['h']):
        for tx in range(w):
            e = tm[ty*w + tx]
            t = e & 0xfff
            if t >= bg['ntiles']:
                continue
            src = tiles[t*64:(t+1)*64]
            hf, vf = e & 0x4000, e & 0x2000
            for yy in range(8):
                sy = 7 - yy if vf else yy
                row = src[sy*8:sy*8+8]
                if hf: row = row[::-1]
                o = (ty*8 + yy) * W + tx*8
                out[o:o+8] = row
    return W, H, bytes(out)

def to_png(path, w, h, idx, pal, transparent=None):
    from PIL import Image
    img = Image.new('RGBA', (w, h))
    px = img.load()
    for y in range(h):
        for x in range(w):
            v = idx[y*w + x]
            if v == transparent or v >= len(pal):
                px[x, y] = (0, 0, 0, 0)
            else:
                r, g, b = pal[v]; px[x, y] = (r, g, b, 255)
    img.save(path)

def main():
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument('file'); ap.add_argument('out')
    ap.add_argument('--manifest', default=os.path.join(os.path.dirname(__file__), 'manifests.json'))
    ap.add_argument('--level', type=int, help='level number (for LEVELxx.PH)')
    a = ap.parse_args()
    data = open(a.file, 'rb').read()
    os.makedirs(a.out, exist_ok=True)
    man = json.load(open(a.manifest))
    recs = list(records(data))
    print(len(recs), 'records')
    if a.level is None:
        names = man['init']
        for i, r in enumerate(recs):
            kind, name = names[i] if i < len(names) else ('spr', 'rec%03d' % i)
            dump_record(r, kind, name, a.out)
    else:
        lv = a.level
        tbl = man['level_files'][str(lv)]
        i = 0
        for kind, name in tbl:
            if not name: continue
            r = recs[i]; i += 1
            if kind == 'bg':
                bg = parse_bg(r.body)
                W, H, idx = render_bg(bg)
                print(f'{name}: {bg["w"]}x{bg["h"]} tiles, {bg["ntiles"]} tiles, {bg["ncolors"]} colours')
                to_png(os.path.join(a.out, name + '.png'), W, H, idx, bg['pal'], transparent=0)
                json.dump(dict(w=bg['w'], h=bg['h'], ntiles=bg['ntiles'], ncolors=bg['ncolors']),
                          open(os.path.join(a.out, name + '.json'), 'w'))
            else:
                open(os.path.join(a.out, name), 'wb').write(r.body)
                print(f'{name}: {len(r.body)} bytes (raw blob)')
        for kind, name in man['levels'][str(lv)]:
            if i >= len(recs): print('MISSING', name); continue
            dump_record(recs[i], kind, name, a.out); i += 1
        print('consumed', i, 'of', len(recs), 'records')

def dump_record(r, kind, name, out):
    if kind == 'raw':
        raw = parse_raw(r.body)
        cw, ch = raw['w'], raw['h']
        n = raw['ncells']
        W = cw * min(n, 16); H = ch * ((n + 15) // 16)
        idx = bytearray(W * H)
        for c in range(n):
            cx, cy = (c % 16) * cw, (c // 16) * ch
            for y in range(ch):
                idx[(cy+y)*W + cx:(cy+y)*W + cx + cw] = raw['pix'][c*raw['cell'] + y*cw: c*raw['cell'] + y*cw + cw]
        to_png(os.path.join(out, name + '.png'), W, H, idx, raw['pal'])
        print(f'{name}: raw {n} cells of {cw}x{ch}, {raw["ncolors"]} colours')
    else:
        spr = parse_sprite(r.body)
        fr = spr['frames']
        W = sum(f['w'] + 1 for f in fr); H = max(f['h'] for f in fr) if fr else 1
        idx = bytearray([0xff]) * (W * H)
        x0 = 0
        for f in fr:
            for y in range(f['h']):
                idx[y*W + x0: y*W + x0 + f['w']] = f['pix'][y*f['w']:(y+1)*f['w']]
            x0 += f['w'] + 1
        to_png(os.path.join(out, name + '.png'), W, H, idx, spr['pal'], transparent=0xff)
        json.dump([dict(w=f['w'], h=f['h'], hx=f['hx'], hy=f['hy']) for f in fr],
                  open(os.path.join(out, name + '.json'), 'w'))
        print(f'{name}: {len(fr)} frames, {spr["ncolors"]} colours, max {H}px tall')

if __name__ == '__main__':
    main()
