import { describe, expect, it } from "vitest";
import { tapChoices, tapHint } from "../src/model/tile-options";
import type { PageGrid } from "../src/types";
import { arrangeTiles, emptyLayout, validatePages } from "../src/model/pages";

const grid: PageGrid = { columns: 4, rows: 3 };

describe("the Power Distribution cards (fork)", () => {
  it("offers opening the PD mode selector or doing nothing, rather than map actions", () => {
    const panel = { supports: () => true } as never;
    for (const entity of ['screen.pd_header', 'screen.pd_batteries']) {
      const tile = { entity, name: '', slot: 0, options: { display: 'map' } };
      expect(tapChoices(tile, panel, 'auto', []).map(([key]) => key)).toEqual(['action', 'none']);
      expect(tapHint(tile, panel, 'auto')?.text).toContain('choose its value');
      expect(tapHint(tile, panel, 'none')).toBeNull();
    }
  });
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
