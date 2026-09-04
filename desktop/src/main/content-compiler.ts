export interface UserContentIcon {
  id: string;
  alpha: Uint8Array;
}

const HEADER_SIZE = 16;
const ENTRY_SIZE = 40;
const ID_CAPACITY = 32;
const ICON_SIZE = 48 * 48;
const MAX_ICONS = 84;
const MAX_BYTES = 2 * 1024 * 1024;
const IDENTIFIER = /^[A-Za-z0-9._-]{1,31}$/;

export function compileUserContentPack(
  sourceIcons: readonly UserContentIcon[]
): Uint8Array {
  if (sourceIcons.length > MAX_ICONS) {
    throw new RangeError(`User content supports at most ${MAX_ICONS} icons`);
  }
  const icons = [...sourceIcons].sort((left, right) =>
    left.id.localeCompare(right.id, "en"));
  const identifiers = new Set<string>();
  for (const icon of icons) {
    if (!IDENTIFIER.test(icon.id)) {
      throw new Error(`Invalid icon identifier: ${icon.id}`);
    }
    if (identifiers.has(icon.id)) {
      throw new Error(`Duplicate icon identifier: ${icon.id}`);
    }
    identifiers.add(icon.id);
    if (!(icon.alpha instanceof Uint8Array) ||
        icon.alpha.byteLength !== ICON_SIZE) {
      throw new Error(`Icon ${icon.id} must contain exactly ${ICON_SIZE} A8 bytes`);
    }
  }

  const pixelsOffset = HEADER_SIZE + icons.length * ENTRY_SIZE;
  const totalSize = pixelsOffset + icons.length * ICON_SIZE;
  if (totalSize > MAX_BYTES) {
    throw new RangeError("User content pack exceeds 2 MiB");
  }
  const wire = new Uint8Array(totalSize);
  const view = new DataView(wire.buffer);
  wire.set(new TextEncoder().encode("CMC1"), 0);
  wire[4] = 1;
  wire[5] = icons.length;
  wire[6] = 48;
  wire[7] = 48;
  view.setUint32(8, totalSize, true);

  let pixelOffset = pixelsOffset;
  icons.forEach((icon, index) => {
    const entryOffset = HEADER_SIZE + index * ENTRY_SIZE;
    wire.set(new TextEncoder().encode(icon.id), entryOffset);
    view.setUint32(entryOffset + ID_CAPACITY, pixelOffset, true);
    view.setUint32(entryOffset + ID_CAPACITY + 4, ICON_SIZE, true);
    wire.set(icon.alpha, pixelOffset);
    pixelOffset += ICON_SIZE;
  });
  return wire;
}
