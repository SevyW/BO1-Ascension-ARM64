// sha256.ts - incremental SHA-256 (FIPS 180-4) in plain TypeScript. The page hashes multi-GB game files chunk by
// chunk when it makes packages (client/src/assets/pack.ts); crypto.subtle.digest only takes the whole input at once.
// Pure; checked against node:crypto in web/test/client-pack.test.ts.

const K = new Int32Array([
  0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01,
  0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
  0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
  0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
  0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08,
  0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
  0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
]);

export class Sha256 {
  private readonly h = new Int32Array([
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
  ]);
  private readonly w = new Int32Array(64);
  private readonly buf = new Uint8Array(64);
  private bufLen = 0;
  private length = 0; // bytes hashed so far
  private finished = false;

  update(data: Uint8Array): this {
    if (this.finished) throw new Error("sha256: update after digest");
    const n = data.length;
    let off = 0;
    this.length += n;
    if (this.bufLen > 0) {
      const take = Math.min(64 - this.bufLen, n);
      this.buf.set(data.subarray(0, take), this.bufLen);
      this.bufLen += take;
      off = take;
      if (this.bufLen < 64) return this;
      this.block(this.buf, 0);
      this.bufLen = 0;
    }
    for (; off + 64 <= n; off += 64) this.block(data, off);
    if (off < n) {
      this.buf.set(data.subarray(off), 0);
      this.bufLen = n - off;
    }
    return this;
  }

  digest(): Uint8Array {
    if (this.finished) throw new Error("sha256: digest twice");
    const bits = this.length;
    const pad = new Uint8Array(this.bufLen < 56 ? 64 - this.bufLen : 128 - this.bufLen);
    pad[0] = 0x80;
    const hi = Math.floor(bits / 0x20000000); // length in bits, big-endian, 64 bits
    const lo = (bits % 0x20000000) * 8;
    const e = pad.length - 8;
    pad[e] = hi >>> 24;
    pad[e + 1] = hi >>> 16;
    pad[e + 2] = hi >>> 8;
    pad[e + 3] = hi;
    pad[e + 4] = lo >>> 24;
    pad[e + 5] = lo >>> 16;
    pad[e + 6] = lo >>> 8;
    pad[e + 7] = lo;
    this.update(pad);
    this.finished = true;
    const out = new Uint8Array(32);
    for (let i = 0; i < 8; i++) {
      const v = this.h[i]!;
      out[i * 4] = v >>> 24;
      out[i * 4 + 1] = v >>> 16;
      out[i * 4 + 2] = v >>> 8;
      out[i * 4 + 3] = v;
    }
    return out;
  }

  /** The digest as 64 lowercase hex digits (the sha256sum format). */
  hex(): string {
    let s = "";
    for (const b of this.digest()) s += b.toString(16).padStart(2, "0");
    return s;
  }

  private block(d: Uint8Array, o: number): void {
    const w = this.w;
    const h = this.h;
    for (let i = 0; i < 16; i++, o += 4) w[i] = (d[o]! << 24) | (d[o + 1]! << 16) | (d[o + 2]! << 8) | d[o + 3]!;
    for (let i = 16; i < 64; i++) {
      const x = w[i - 15]!;
      const y = w[i - 2]!;
      const s0 = ((x >>> 7) | (x << 25)) ^ ((x >>> 18) | (x << 14)) ^ (x >>> 3);
      const s1 = ((y >>> 17) | (y << 15)) ^ ((y >>> 19) | (y << 13)) ^ (y >>> 10);
      w[i] = (w[i - 16]! + s0 + w[i - 7]! + s1) | 0;
    }
    let a = h[0]!;
    let b = h[1]!;
    let c = h[2]!;
    let dd = h[3]!;
    let e = h[4]!;
    let f = h[5]!;
    let g = h[6]!;
    let hh = h[7]!;
    for (let i = 0; i < 64; i++) {
      const S1 = ((e >>> 6) | (e << 26)) ^ ((e >>> 11) | (e << 21)) ^ ((e >>> 25) | (e << 7));
      const t1 = (hh + S1 + ((e & f) ^ (~e & g)) + K[i]! + w[i]!) | 0;
      const S0 = ((a >>> 2) | (a << 30)) ^ ((a >>> 13) | (a << 19)) ^ ((a >>> 22) | (a << 10));
      const t2 = (S0 + ((a & b) ^ (a & c) ^ (b & c))) | 0;
      hh = g;
      g = f;
      f = e;
      e = (dd + t1) | 0;
      dd = c;
      c = b;
      b = a;
      a = (t1 + t2) | 0;
    }
    h[0] = (h[0]! + a) | 0;
    h[1] = (h[1]! + b) | 0;
    h[2] = (h[2]! + c) | 0;
    h[3] = (h[3]! + dd) | 0;
    h[4] = (h[4]! + e) | 0;
    h[5] = (h[5]! + f) | 0;
    h[6] = (h[6]! + g) | 0;
    h[7] = (h[7]! + hh) | 0;
  }
}
