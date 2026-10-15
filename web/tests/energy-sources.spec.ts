// The energy card's Sensors section (inspector/EnergySourcesSection.vue): it must stay light on a house with thousands
// of sensors, and save only when a sensor is picked or a field is left, never on each key.
import { mount } from "@vue/test-utils";
import { beforeEach, describe, expect, it, vi } from "vitest";
import EnergySourcesSection from "../src/components/inspector/EnergySourcesSection.vue";
import { useInventoryStore } from "../src/stores/inventory";

const setTileOption = vi.fn();
vi.mock("../src/editor/tiles", () => ({ setTileOption: (...args: unknown[]) => setTileOption(...args) }));

const tile = { entity: "screen.energy", name: "", slot: 0, options: {} } as any;
beforeEach(() => {
  setTileOption.mockClear();
  useInventoryStore().inventory = { connected: true, screens: [], entities: Array.from({ length: 12000 }, (_, i) => ({ id: `sensor.s${i}`, name: `Sensor ${i}`, domain: "sensor" })) } as any;
});

describe("the energy card's sensors", () => {
  it("mounts fast with 12000 sensors and draws no list until a field has the cursor", () => {
    const started = performance.now();
    const view = mount(EnergySourcesSection, { props: { tile } });
    expect(performance.now() - started).toBeLessThan(1500);
    expect(view.findAll("input").length).toBeGreaterThanOrEqual(10);
    expect(view.findAll('[role="option"]').filter((o) => o.isVisible()).length).toBe(0);
    expect(view.findAll('[role="option"]').length).toBeLessThanOrEqual(10 * 40);
  });

  it("saves a picked sensor once, and typing alone saves nothing", async () => {
    const view = mount(EnergySourcesSection, { props: { tile } });
    const grid = view.findAll("input")[0];
    await grid.setValue("sensor.s1");
    expect(setTileOption).not.toHaveBeenCalled();
    await grid.trigger("blur");
    expect(setTileOption).toHaveBeenCalledTimes(1);
    expect(setTileOption).toHaveBeenCalledWith(tile, "energy_sources", { grid: "sensor.s1" });
  });

  it("puts back a value that is not a sensor", async () => {
    const view = mount(EnergySourcesSection, { props: { tile } });
    const grid = view.findAll("input")[0];
    await grid.setValue("light.kitchen");
    await grid.trigger("blur");
    expect(setTileOption).not.toHaveBeenCalled();
    expect((grid.element as HTMLInputElement).value).toBe("");
  });
});
