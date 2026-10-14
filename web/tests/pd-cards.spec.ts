import { describe, expect, it } from "vitest";
import type { PageGrid } from "../src/types";
import { arrangeTiles, emptyLayout, validatePages } from "../src/model/pages";

const grid: PageGrid = { columns: 4, rows: 3 };

describe("the Power Distribution cards (fork)", () => {
  it("are accepted as picture cards of the screen's own", () => {
    const names = ["pd_header", "pd_dp3", "pd_ultra", "pd_ac", "pd_max", "pd_batteries"];
    const tiles = names.map((name, slot) => ({ tile: { entity: `screen.${name}`, name: "", slot, options: { display: "map", size: "wide" } }, slot: slot * 2 }));
    const layout = arrangeTiles(emptyLayout("Office"), { columns: 2, rows: 6 }, tiles);
    expect(() => validatePages(layout, { columns: 2, rows: 6 })).not.toThrow();
  });
  it("accepts the combined battery card spanning the right half of the screen", () => {
    const tile = { entity: "screen.pd_batteries", name: "", slot: 2,
      options: { display: "map", size: "2x4" } };
    const layout = arrangeTiles(emptyLayout("Office"), { columns: 4, rows: 4 }, [{ tile, slot: 2 }]);
    expect(() => validatePages(layout, { columns: 4, rows: 4 })).not.toThrow();
  });
  it("are not accepted with another display", () => {
    expect(() => arrangeTiles(emptyLayout("Office"), grid, [{ tile: { entity: "screen.pd_header", name: "", slot: 0, options: { display: "standard", size: "wide" } }, slot: 0 }])).toThrow();
  });
});
