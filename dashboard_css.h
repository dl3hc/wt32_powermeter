const char DB_STYLESHEET[] PROGMEM = R"=====(
<!DOCTYPE html>
<html>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>WT32 Power Meter</title>
<style>

/* ============================================================
   Design tokens -- "instrument panel" theme.
   A dark RF test-equipment faceplate: copper accent for FWD,
   slate-blue for REF, amber for VSWR, green/red for status LEDs.
   ============================================================ */
:root {
  --bg:         #14181B;
  --panel:      #1B2024;
  --panel-alt:  #20262B;
  --border:     #2E363B;
  --text:       #E8E6E1;
  --text-dim:   #8A9096;

  --copper:     #D97C4A;
  --slate:      #6FA8C7;
  --amber:      #E8B84B;
  --green:      #4CE07A;
  --red:        #FF5A45;

  --font-display: ui-monospace, 'SFMono-Regular', Consolas, 'Liberation Mono', Menlo, monospace;
  --font-body: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif;
}

* { box-sizing: border-box; }

body {
  background: var(--bg);
  color: var(--text);
  font-family: var(--font-body);
  margin: 0;
  padding: 16px;
  display: flex;
  justify-content: center;
}

.panel {
  width: 100%;
  max-width: 720px;
  display: flex;
  flex-direction: column;
  gap: 8px;
}

/* ---------- nameplate (header) ---------- */
.nameplate {
  background: var(--panel-alt);
  border: 1px solid var(--border);
  border-radius: 6px;
  padding: 10px 16px;
  display: flex;
  align-items: baseline;
  justify-content: space-between;
  flex-wrap: wrap;
  gap: 4px 16px;
}

.nameplate__brand {
  font-size: 11px;
  font-weight: 700;
  letter-spacing: 0.14em;
  text-transform: uppercase;
  color: var(--text-dim);
}

.nameplate__antenna {
  font-family: var(--font-display);
  font-size: 15px;
  font-weight: 700;
  color: var(--text);
  letter-spacing: 0.02em;
}

.nameplate__band {
  font-size: 11px;
  font-weight: 700;
  letter-spacing: 0.1em;
  text-transform: uppercase;
  color: var(--text-dim);
}

.nameplate__band b {
  font-family: var(--font-display);
  color: var(--text);
  font-weight: 700;
  letter-spacing: normal;
  text-transform: none;
}

/* ---------- modules (FWD / REF / VSWR) ---------- */
.modules {
  display: grid;
  grid-template-columns: repeat(3, 1fr);
  gap: 8px;
}

@media (max-width: 680px) {
  .modules {
    grid-template-columns: 1fr;
  }
}

.module {
  background: var(--panel);
  border: 1px solid var(--border);
  border-radius: 6px;
  padding: 12px;
  border-top: 3px solid var(--accent);
  transition: box-shadow 0.2s ease, border-color 0.2s ease;
}

.module--fwd  { --accent: var(--copper); }
.module--ref  { --accent: var(--slate); }
.module--vswr { --accent: var(--amber); }

.module.redbox {
  --accent: var(--red);
  border-color: var(--red);
}

@media (prefers-reduced-motion: no-preference) {
  .module.redbox {
    animation: alert-pulse 1.6s ease-in-out infinite;
  }
}

@keyframes alert-pulse {
  0%, 100% { box-shadow: 0 0 0 rgba(255, 90, 69, 0); }
  50%      { box-shadow: 0 0 14px rgba(255, 90, 69, 0.55); }
}

.module__label {
  margin: 0 0 10px 0;
  font-size: 11px;
  font-weight: 700;
  letter-spacing: 0.12em;
  text-transform: uppercase;
  color: var(--accent);
}

.module__body {
  display: flex;
  align-items: flex-end;
  justify-content: space-between;
  gap: 8px;
}

.module__readout {
  display: flex;
  flex-direction: column;
  gap: 4px;
  min-width: 0;
}

.value {
  display: block;
  font-family: var(--font-display);
  font-size: 22px;
  font-weight: 700;
  color: var(--text);
  text-shadow: 0 0 8px rgba(217, 124, 74, 0.35);
  text-shadow: 0 0 8px color-mix(in srgb, var(--accent) 55%, transparent);
  overflow-wrap: anywhere;
}

.value--sub {
  font-size: 13px;
  font-weight: 500;
  color: var(--text-dim);
  text-shadow: none;
}

/* ---------- LED bargraph column ---------- */
.bargraph {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 2px;
  flex-shrink: 0;
}

.bargraph__max,
.bargraph__min {
  font-family: var(--font-display);
  font-size: 10px;
  color: var(--text-dim);
}

.bargraph canvas {
  display: block;
}

/* ---------- status bar (footer) ---------- */
.statusbar {
  background: var(--panel-alt);
  border: 1px solid var(--border);
  border-radius: 6px;
  padding: 8px 16px;
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 8px;
  flex-wrap: wrap;
  font-size: 12px;
  color: var(--text-dim);
}

.statusbar__temp {
  font-family: var(--font-display);
  color: var(--text);
}

.button {
  background: none;
  border: 1px solid var(--border);
  border-radius: 4px;
  padding: 4px 10px !important;
  font-family: var(--font-body);
  font-size: 12px;
  font-weight: 700;
  letter-spacing: 0.04em;
  color: var(--text);
  cursor: pointer;
}

.button:hover {
  border-color: var(--copper);
  color: var(--copper);
}

.button:focus-visible,
a:focus-visible {
  outline: 2px solid var(--copper);
  outline-offset: 2px;
}

.statusbar__version {
  font-size: 11px;
  color: var(--text-dim);
}

</style>
)=====";
