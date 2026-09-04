import { describe, expect, it } from "vitest";

import { compileUserContentPack } from "./content-compiler";

const icon = (id: string, value: number) => ({
  id,
  alpha: new Uint8Array(48 * 48).fill(value)
});

describe("compileUserContentPack", () => {
  it("builds a deterministic canonical CMC1 pack", () => {
    const pack = compileUserContentPack([
      icon("zeta", 0x22),
      icon("alpha", 0x11)
    ]);
    const view = new DataView(pack.buffer, pack.byteOffset, pack.byteLength);

    expect(new TextDecoder().decode(pack.subarray(0, 4))).toBe("CMC1");
    expect(pack[4]).toBe(1);
    expect(pack[5]).toBe(2);
    expect(pack[6]).toBe(48);
    expect(pack[7]).toBe(48);
    expect(view.getUint32(8, true)).toBe(pack.byteLength);
    expect(new TextDecoder().decode(pack.subarray(16, 21))).toBe("alpha");
    expect(view.getUint32(48, true)).toBe(96);
    expect(view.getUint32(52, true)).toBe(2304);
    expect(pack[96]).toBe(0x11);
    expect(pack[96 + 2304]).toBe(0x22);
    expect(compileUserContentPack([
      icon("alpha", 0x11),
      icon("zeta", 0x22)
    ])).toEqual(pack);
  });

  it("rejects duplicate, unsafe, oversized, and malformed icons", () => {
    expect(() => compileUserContentPack([
      icon("same", 1),
      icon("same", 2)
    ])).toThrow(/duplicate/i);
    expect(() => compileUserContentPack([icon("../bad", 1)]))
      .toThrow(/identifier/i);
    expect(() => compileUserContentPack([{
      id: "short",
      alpha: new Uint8Array(7)
    }])).toThrow(/2304/);
    expect(() => compileUserContentPack(
      Array.from({ length: 85 }, (_, index) => icon(`i${index}`, index))
    )).toThrow(/84/);
  });
});
