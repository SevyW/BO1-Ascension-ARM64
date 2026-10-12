// progress.ts - a progress bar and a remaining-time label (the import and the packager on the Game files screen).

import { h } from "./dom";

export function duration(sec: number): string {
  if (!Number.isFinite(sec) || sec <= 0) return "";
  if (sec < 60) return `${Math.ceil(sec)} s`;
  if (sec < 3600) return `${Math.ceil(sec / 60)} min`;
  return `${(sec / 3600).toFixed(1)} h`;
}

export function bar(fraction: number, testid?: string): HTMLElement {
  const pct = Math.max(0, Math.min(1, fraction)) * 100;
  return h(
    "div",
    { class: "bar", role: "progressbar", "aria-valuemin": "0", "aria-valuemax": "100", "aria-valuenow": pct.toFixed(0), "data-testid": testid },
    h("div", { class: "bar-fill", style: `width:${pct.toFixed(1)}%` }),
  );
}
