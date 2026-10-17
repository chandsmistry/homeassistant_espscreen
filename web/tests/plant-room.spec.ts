// The plant room card's Sensors section (inspector/PlantRoomSection.vue): a preset in the placeholders, a replaced role
// saved once, a preset value never stored, a value that is not an entity put back.
import { mount } from "@vue/test-utils";
import { beforeEach, describe, expect, it, vi } from "vitest";
import PlantRoomSection from "../src/components/inspector/PlantRoomSection.vue";
import { PLANT_PRESET, PLANT_ROLES } from "../src/model/plant-room";
import { useInventoryStore } from "../src/stores/inventory";

const setTileOption = vi.fn();
vi.mock("../src/editor/tiles", () => ({ setTileOption: (...args: unknown[]) => setTileOption(...args) }));

const tile = { entity: "screen.plant_room", name: "", slot: 0, options: {} } as any;
beforeEach(() => {
  setTileOption.mockClear();
  useInventoryStore().inventory = { connected: true, screens: [], entities: Array.from({ length: 5000 }, (_, i) => ({ id: `sensor.s${i}`, name: `Sensor ${i}`, domain: "sensor" })) } as any;
});

describe("the plant room card's sensors", () => {
  it("has a field for every role, the preset as its placeholder", () => {
    const view = mount(PlantRoomSection, { props: { tile } });
    const inputs = view.findAll("input");
    expect(inputs.length).toBe(PLANT_ROLES.length);
    expect(inputs[0].attributes("placeholder")).toBe(PLANT_PRESET[PLANT_ROLES[0]]);
  });

  it("saves a replaced role once, and typing alone saves nothing", async () => {
    const view = mount(PlantRoomSection, { props: { tile } });
    const first = view.findAll("input")[0];
    await first.setValue("sensor.s1");
    expect(setTileOption).not.toHaveBeenCalled();
    await first.trigger("blur");
    expect(setTileOption).toHaveBeenCalledTimes(1);
    expect(setTileOption).toHaveBeenCalledWith(tile, "plant_room", { [PLANT_ROLES[0]]: "sensor.s1" });
  });

  it("never stores the preset, and puts back what is not an entity", async () => {
    const view = mount(PlantRoomSection, { props: { tile } });
    const first = view.findAll("input")[0];
    await first.setValue("not an entity");
    await first.trigger("blur");
    expect(setTileOption).not.toHaveBeenCalled();
    expect((first.element as HTMLInputElement).value).toBe("");
    await first.setValue(PLANT_PRESET[PLANT_ROLES[0]]);
    await first.trigger("blur");
    expect(setTileOption).toHaveBeenCalledWith(tile, "plant_room", {});
  });
});
