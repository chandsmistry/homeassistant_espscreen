// The screensaver's card and drawer (app 0.4.83): the card lists the steps with one line each, a step opens in the
// drawer, and the choice of the screen changes as it did.
import { flushPromises, mount } from "@vue/test-utils";
import { nextTick } from "vue";
import { beforeEach, describe, expect, it, vi } from "vitest";
import UiSelect from "../src/components/ui/UiSelect.vue";
import SaverInspector from "../src/components/SaverInspector.vue";
import ScreensaverCard from "../src/components/ScreensaverCard.vue";
import type { Inventory, ScreensaverView } from "../src/types";
import { useScreenStore } from "../src/stores/screen";
import { useScreensaverStore } from "../src/stores/screensaver";
import { useInventoryStore } from "../src/stores/inventory";
import { useUiStore } from "../src/stores/ui";
import { useInspectorStore } from "../src/stores/inspector";

let insp: ReturnType<typeof useInspectorStore>;
beforeEach(() => { insp = useInspectorStore(); });

let ui: ReturnType<typeof useUiStore>;
beforeEach(() => { ui = useUiStore(); });

const saver = (patch: Partial<ScreensaverView> = {}): ScreensaverView => ({
  show: true, media: "media_player.tv", more: ["media_player.speaker"], camera: "", order: ["media", "camera", "photos", "clock"], off: ["camera"],
  weather: "auto", items: [], ready: true, pictures: true, standby: true, ...patch,
});

beforeEach(() => {
  vi.stubGlobal("fetch", vi.fn(() => Promise.reject(new Error("offline"))));
  useInventoryStore().inventory = {
    screens: [{ id: "living", name: "Living", virtual: true, shape: { width: 480, height: 480 }, screensaver: saver() }],
    entities: [
      { id: "media_player.tv", name: "Apple TV", area: "Living room" }, { id: "media_player.speaker", name: "Speaker", area: "Living room" },
      { id: "media_player.kitchen", name: "Kitchen", area: "Kitchen" }, { id: "camera.door", name: "Front door", area: "Outside" },
      { id: "weather.home", name: "Home" }, { id: "lock.front", name: "Front lock" },
    ],
    builtin: [], header: { max_items: 6, builtin: [] },
  } as unknown as Inventory;
  useScreenStore().selected = "living";
  insp.inspector = null;
  ui.tab = "settings";
});

const rows = (wrapper: ReturnType<typeof mount>) => wrapper.findAll(".saver-row").map((row) => `${row.find("b").text()}: ${row.find("small").text()}`);

describe("the screensaver card", () => {
  it("lists the steps in their order, each with one line of what it shows", () => {
    const wrapper = mount(ScreensaverCard);
    expect(wrapper.findAll(".saver-row").map((row) => row.attributes("data-kind"))).toEqual(["media", "camera", "photos", "clock"]);
    expect(rows(wrapper)).toEqual(["Music playing: Apple TV, Speaker", "Camera: Off", "Photos: Photos from /media/screensaver", "Clock: Outside temperature"]);
    // No field in the card: the choices live in the drawer, so nothing can push the rows past a narrow card.
    expect(wrapper.find(".ui-select").exists()).toBe(false);
  });

  it("says what a step still lacks, and names the clock's temperature and entities", () => {
    useScreenStore().currentScreen!.screensaver = saver({ media: "", more: [], off: [], weather: "", items: [{ type: "entity", entity: "lock.front", content: "state", icon: "auto", show: "always" }] });
    expect(useScreensaverStore().summary("media")).toEqual({ text: "No player yet", missing: true });
    expect(useScreensaverStore().summary("camera")).toEqual({ text: "No camera yet", missing: true });
    expect(useScreensaverStore().summary("clock").text).toBe("Time and date, Front lock");
  });

  it("opens a step in the drawer on a click, and its switch turns it off without opening it", async () => {
    const wrapper = mount(ScreensaverCard);
    await wrapper.find('.saver-row[data-kind="media"] [role="switch"]').trigger("click");
    await flushPromises();
    expect(useScreenStore().currentScreen!.screensaver!.off).toEqual(["camera", "media"]);
    expect(insp.inspector).toBeNull();
    await wrapper.find('.saver-row[data-kind="clock"]').trigger("click");
    expect(insp.inspector).toEqual({ kind: "saver", step: "clock" });
    expect(wrapper.find('.saver-row[data-kind="clock"]').classes()).toContain("opened");
  });

  it("moves a focused row with the arrow keys", async () => {
    const wrapper = mount(ScreensaverCard, { attachTo: document.body });
    const clock = wrapper.find('.saver-row[data-kind="clock"]');
    await clock.trigger("keydown", { key: "ArrowUp" });
    expect(useScreenStore().currentScreen!.screensaver!.order).toEqual(["media", "camera", "clock", "photos"]);
  });

  it("shows the clock alone on a screen without pictures", () => {
    useScreenStore().currentScreen!.screensaver = saver({ pictures: false });
    const wrapper = mount(ScreensaverCard);
    expect(wrapper.findAll(".saver-row").map((row) => row.attributes("data-kind"))).toEqual(["clock"]);
  });
});

describe("a step in the drawer", () => {
  it("lists the players in the order they are tried, and takes one out", async () => {
    const wrapper = mount(SaverInspector, { props: { step: "media" } });
    expect(wrapper.findAll(".saver-player").map((row) => row.find(".tx b").text())).toEqual(["Apple TV", "Speaker"]);
    expect(wrapper.findAll(".saver-rank").map((rank) => rank.text())).toEqual(["1", "2"]);
    await wrapper.findAll(".saver-player")[0].find("button.x").trigger("click");
    expect(useScreenStore().currentScreen!.screensaver).toMatchObject({ media: "media_player.speaker", more: [] });
  });

  it("turns its step off and moves it a place later", async () => {
    const wrapper = mount(SaverInspector, { props: { step: "media" } });
    expect(wrapper.text()).toContain("1 of 4");
    await wrapper.find('.switch-row [role="switch"]').trigger("click");
    await flushPromises();
    expect(useScreenStore().currentScreen!.screensaver!.off).toContain("media");
    await wrapper.find('button[aria-label="Try later"]').trigger("click");
    expect(useScreenStore().currentScreen!.screensaver!.order).toEqual(["camera", "media", "photos", "clock"]);
  });

  it("sets how long a photo stays and the largest file the Photos step uses", async () => {
    const wrapper = mount(SaverInspector, { props: { step: "photos" } });
    const values = () => wrapper.findAllComponents(UiSelect).map((select) => (select.props() as { modelValue: number }).modelValue);
    expect(values()).toEqual(["fill", "shuffle", "native", 30, 8, "none"]);
    useScreensaverStore().changeSaver({ photo_seconds: 60, photo_mb: 4 });
    await flushPromises();
    expect(useScreenStore().currentScreen!.screensaver).toMatchObject({ photo_seconds: 60, photo_mb: 4 });
    expect(values()).toEqual(["fill", "shuffle", "native", 60, 4, "none"]);
  });

  it("offers portrait pairs, order, optional clock and every photo transition without touching tiles", async () => {
    useScreenStore().currentScreen!.screensaver = saver({ photo_effects: true });
    const wrapper = mount(SaverInspector, { props: { step: "photos" } });
    const selects = wrapper.findAllComponents(UiSelect);
    expect((selects[0]!.props() as {options: readonly (readonly string[])[]}).options.map((p: readonly string[]) => p[0])).toEqual(["fit", "fill", "pair"]);
    expect((selects[1]!.props() as {options: readonly (readonly string[])[]}).options.map((p: readonly string[]) => p[0])).toEqual(["shuffle", "filename"]);
    expect((selects[5]!.props() as {options: readonly (readonly string[])[]}).options).toHaveLength(20);
    selects[0]!.vm.$emit("update:modelValue", "pair");
    selects[5]!.vm.$emit("update:modelValue", "random");
    await flushPromises();
    expect(useScreenStore().currentScreen!.screensaver).toMatchObject({photo_fit: "pair", photo_transition: "random"});
  });

  it("offers all seven clock positions and preserves the selected setting", async () => {
    useScreenStore().currentScreen!.screensaver = saver({photo_clock:true,photo_effects:true,photo_positions:true});
    const wrapper=mount(SaverInspector,{props:{step:"photos"}});
    const selects=wrapper.findAllComponents(UiSelect);
    const position=selects[5]!;
    expect((position.props() as {options: unknown[]}).options).toHaveLength(7);
    position.vm.$emit("update:modelValue","top_right");await flushPromises();
    expect(useScreenStore().currentScreen!.screensaver!.photo_clock_position).toBe("top_right");
    expect(wrapper.find('.photo-clock').classes()).toContain('clock-top_right');
  });

  it("shows the clock in the glass's own shape, without a temperature when none is chosen", async () => {
    useScreenStore().currentScreen!.screensaver = saver({ weather: "" });
    const wrapper = mount(SaverInspector, { props: { step: "clock" } });
    const glass = wrapper.find(".saver-glass");
    expect((glass.element as HTMLElement).style.aspectRatio).toBe("480 / 480");
    expect(glass.find(".saver-glass-time").text()).toMatch(/\d/);
    expect(glass.find(".saver-glass-line").text()).toBe("");
  });

  it("closes when the layout comes back", async () => {
    // The inspector closes the screensaver's drawers itself: nothing has to be started for it.
    insp.openSaverStep("clock");
    ui.tab = "layout";
    await nextTick();
    expect(insp.inspector).toBeNull();
  });
});

describe("Photos preferences on the real save path", () => {
  it("offers five and ten second photo intervals", () => {
    expect(useScreensaverStore().secondsChoices.map(choice=>choice[0])).toEqual([5,10,15,30,60,120,300]);
  });
  it("sends all photo fields and retains them after inventory refresh and a later step edit", async () => {
    const screen=useScreenStore().currentScreen!;screen.virtual=false;
    const choices={photo_fit:"fit" as const,photo_order:"filename" as const,photo_quality:"fast",photo_seconds:15,photo_mb:4,
      photo_clock:true,photo_clock_position:"top_right",photo_transition:"fade"};
    let stored:Record<string,unknown>={};
    const request=vi.fn(async (_url:string,options:RequestInit)=>{
      stored=JSON.parse(String(options.body)).screensaver;
      return new Response(JSON.stringify({screensaver:stored}),{status:200});
    });vi.stubGlobal("fetch",request);
    await useScreenStore().setScreensaver(screen,choices);
    expect(stored).toMatchObject(choices);
    expect(stored).not.toHaveProperty("ready");expect(stored).not.toHaveProperty("photo_effects");
    screen.screensaver=saver(stored);
    expect(screen.screensaver).toMatchObject(choices);
    await useScreenStore().setScreensaver(screen,{off:["media","camera","clock"]});
    expect(stored).toMatchObject(choices);expect(request).toHaveBeenCalledTimes(2);
  });
});
