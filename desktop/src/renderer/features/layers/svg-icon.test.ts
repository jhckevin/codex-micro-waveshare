// @vitest-environment jsdom

import { describe, expect, it } from "vitest";

import { sanitizeMonochromeSvg } from "./svg-icon";

describe("sanitizeMonochromeSvg", () => {
  it("normalizes every visible source color to monochrome black", () => {
    const sanitized = sanitizeMonochromeSvg(`
      <svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 32 32">
        <path fill="#ff0066" stroke="rgb(0, 255, 0)" d="M2 2h28v28H2z"/>
        <circle fill="none" stroke="currentColor" cx="16" cy="16" r="4"/>
      </svg>
    `);

    expect(sanitized).toContain('viewBox="0 0 32 32"');
    expect(sanitized).not.toMatch(/ff0066|rgb\(|currentColor/i);
    expect(sanitized.match(/#000000/g)?.length).toBeGreaterThanOrEqual(3);
    expect(sanitized).toContain('fill="none"');
  });

  it.each([
    '<svg xmlns="http://www.w3.org/2000/svg"><script>alert(1)</script></svg>',
    '<svg xmlns="http://www.w3.org/2000/svg"><foreignObject/></svg>',
    '<svg xmlns="http://www.w3.org/2000/svg"><image href="https://x/y.png"/></svg>',
    '<svg xmlns="http://www.w3.org/2000/svg" onload="alert(1)"><path d="M0 0"/></svg>',
    '<svg xmlns="http://www.w3.org/2000/svg"><path style="fill:url(https://x)"/></svg>',
    '<svg xmlns="http://www.w3.org/2000/svg"><path filter="url(#f)" d="M0 0"/></svg>'
  ])("rejects active or externally-referenced SVG: %s", (source) => {
    expect(() => sanitizeMonochromeSvg(source)).toThrow();
  });

  it("rejects malformed and unbounded input", () => {
    expect(() => sanitizeMonochromeSvg("<svg><path></svg>")).toThrow();
    expect(() => sanitizeMonochromeSvg("x".repeat(256 * 1024 + 1))).toThrow();
  });
});
