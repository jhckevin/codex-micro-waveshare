const MAX_SOURCE_BYTES = 256 * 1024;
const SAFE_ELEMENTS = new Set([
  "svg",
  "g",
  "path",
  "rect",
  "circle",
  "ellipse",
  "line",
  "polyline",
  "polygon"
]);
const SAFE_ATTRIBUTES = new Set([
  "xmlns",
  "viewBox",
  "width",
  "height",
  "x",
  "y",
  "x1",
  "y1",
  "x2",
  "y2",
  "cx",
  "cy",
  "r",
  "rx",
  "ry",
  "d",
  "points",
  "transform",
  "fill",
  "stroke",
  "stroke-width",
  "stroke-linecap",
  "stroke-linejoin",
  "stroke-miterlimit",
  "fill-rule",
  "clip-rule",
  "opacity",
  "fill-opacity",
  "stroke-opacity"
]);

function rejectUnsafeValue(value: string): void {
  if (/url\s*\(|javascript:|data:|https?:|file:/i.test(value)) {
    throw new Error("SVG contains an external or active reference");
  }
}

export function sanitizeMonochromeSvg(source: string): string {
  if (typeof source !== "string" || source.length === 0 ||
      new TextEncoder().encode(source).byteLength > MAX_SOURCE_BYTES) {
    throw new Error("SVG source is empty or exceeds 256 KiB");
  }
  const documentValue = new DOMParser().parseFromString(source, "image/svg+xml");
  if (documentValue.querySelector("parsererror") ||
      documentValue.documentElement.localName !== "svg") {
    throw new Error("SVG is malformed");
  }

  const elements = [
    documentValue.documentElement,
    ...Array.from(documentValue.documentElement.querySelectorAll("*"))
  ];
  for (const element of elements) {
    if (!SAFE_ELEMENTS.has(element.localName)) {
      throw new Error(`Unsupported SVG element: ${element.localName}`);
    }
    for (const attribute of Array.from(element.attributes)) {
      if (!SAFE_ATTRIBUTES.has(attribute.name) ||
          /^on/i.test(attribute.name)) {
        throw new Error(`Unsupported SVG attribute: ${attribute.name}`);
      }
      if (attribute.name !== "xmlns") rejectUnsafeValue(attribute.value);
    }
    for (const paint of ["fill", "stroke"] as const) {
      const value = element.getAttribute(paint);
      if (value !== null && value.trim().toLowerCase() !== "none") {
        element.setAttribute(paint, "#000000");
      }
    }
  }
  if (!documentValue.documentElement.hasAttribute("viewBox")) {
    throw new Error("SVG must declare a viewBox");
  }
  if (!documentValue.documentElement.hasAttribute("fill")) {
    documentValue.documentElement.setAttribute("fill", "#000000");
  }
  documentValue.documentElement.removeAttribute("width");
  documentValue.documentElement.removeAttribute("height");
  return new XMLSerializer().serializeToString(documentValue.documentElement);
}

export async function rasterizeMonochromeSvg(
  source: string
): Promise<Uint8Array> {
  const sanitized = sanitizeMonochromeSvg(source);
  const objectUrl = URL.createObjectURL(
    new Blob([sanitized], { type: "image/svg+xml" }));
  try {
    const image = new Image();
    image.decoding = "async";
    await new Promise<void>((resolve, reject) => {
      image.onload = () => resolve();
      image.onerror = () => reject(new Error("SVG rasterization failed"));
      image.src = objectUrl;
    });
    const canvas = document.createElement("canvas");
    canvas.width = 48;
    canvas.height = 48;
    const context = canvas.getContext("2d", {
      alpha: true,
      willReadFrequently: true
    });
    if (!context) throw new Error("Canvas 2D is unavailable");
    context.clearRect(0, 0, 48, 48);
    context.drawImage(image, 0, 0, 48, 48);
    const rgba = context.getImageData(0, 0, 48, 48).data;
    const alpha = new Uint8Array(48 * 48);
    for (let index = 0; index < alpha.length; ++index) {
      alpha[index] = rgba[index * 4 + 3] ?? 0;
    }
    return alpha;
  } finally {
    URL.revokeObjectURL(objectUrl);
  }
}
