const char CFG_STYLESHEET[] PROGMEM = R"=====(
<!DOCTYPE html>
<html>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>WT32 Power Meter -- Configuration</title>
<style>

:root {
  --bg:         #14181B;
  --panel:      #1B2024;
  --panel-alt:  #20262B;
  --border:     #2E363B;
  --text:       #E8E6E1;
  --text-dim:   #8A9096;
  --copper:     #D97C4A;

  --font-display: ui-monospace, 'SFMono-Regular', Consolas, 'Liberation Mono', Menlo, monospace;
  --font-body: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif;
}

* { box-sizing: border-box; }

body {
  background: var(--bg);
  color: var(--text);
  font-family: var(--font-body);
  text-align: center;
  letter-spacing: 0px;
  margin: 0;
  padding: 16px;
}

.grid-container {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  grid-column-gap: 8px;
  grid-row-gap: 8px;
  max-width: 700px;
  margin: 0 auto;
}

@media (max-width: 620px) {
  .grid-container {
    grid-template-columns: 1fr;
  }
  /* Single column: let items auto-flow in source order instead of the
     explicit two-column placement above, so nothing collides. */
  .titlebox, .bandbox, .subtitle1, .translationitems, .subtitle2, .configitems, .footerbox {
    grid-column: 1 !important;
    grid-row: auto !important;
  }
}

.titlebox  {
  grid-column: 1;
  grid-row: 1;
  background: var(--panel-alt);
  border: 1px solid var(--border);
  border-radius: 6px;
}

.bandbox {
  grid-column: 2;
  grid-row: 1;
  background: var(--panel-alt);
  border: 1px solid var(--border);
  border-radius: 6px;
}

.maintitlebox {
  border-top: 3px solid var(--copper);
  font-size: 15px;
  font-weight: 700;
  letter-spacing: 0.1em;
  text-transform: uppercase;
  line-height: 40px;
}

.subtitlebox {
  background: var(--panel-alt);
  border: 1px solid var(--border);
  border-radius: 6px;
  font-size: 12px;
  font-weight: 700;
  letter-spacing: 0.08em;
  text-transform: uppercase;
  color: var(--text-dim);
  padding: 6px 0;
}

.contentbox {
  background: var(--panel);
  border: 1px solid var(--border);
  border-radius: 6px;
  line-height: 20px;
  padding: 10px;
  margin: 0px;
  font-size: 14px;
  text-align: left;
}

.subtitle1 {
  grid-column: 1 / span 2;
  grid-row: 2;
}

.translationitems {
  grid-column: 1 / span 2;
  grid-row: 3;
}

.subtitle2 {
  grid-column: 1 / span 2;
  grid-row: 4;
}

.configitems {
  grid-column: 1 / span 2;
  grid-row: 5;
}

.footerbox  {
  grid-column: 1 / span 2;
  grid-row: 6;
  background: var(--panel-alt);
  border: 1px solid var(--border);
  border-radius: 6px;
  line-height: 20px;
  font-size: 12px;
  color: var(--text-dim);
  padding: 8px 0;
}

textarea {
  width: 100%;
  background: var(--bg);
  color: var(--text);
  border: 1px solid var(--border);
  border-radius: 4px;
  font-family: var(--font-display);
  font-size: 12px;
  padding: 6px;
}

textarea:focus-visible {
  outline: 2px solid var(--copper);
  outline-offset: 1px;
}

input[type="text"], input:not([type]) {
  background: var(--bg);
  color: var(--text);
  border: 1px solid var(--border);
  border-radius: 4px;
  padding: 3px 6px;
  font-family: var(--font-body);
  font-size: 13px;
}

input[type="checkbox"] {
  accent-color: var(--copper);
  width: 16px;
  height: 16px;
  vertical-align: middle;
}

.linkbutton {
  background: none !important;
  border: none;
  padding: 0 !important;
  font-family: var(--font-body);
  font-size: 12px;
  font-weight: 700;
  color: var(--copper);
  text-decoration: underline;
  cursor: pointer;
}

.styled-table{
  border-collapse: collapse;
  font-size: 0.85em;
  font-family: var(--font-body);
  width: 100%;
  background: var(--panel);
  border: 1px solid var(--border);
  border-radius: 6px;
  overflow: hidden;
}

.styled-table thead tr{
  background-color: var(--panel-alt);
  color: var(--text);
  text-align: left;
}

.styled-table tbody tr{
  border-bottom: 1px solid var(--border);
}

.styled-table tbody tr:nth-of-type(even){
  background-color: var(--panel-alt);
}
.styled-table tbody tr:last-of-type{
  border-bottom: 2px solid var(--copper);
}

.styled-table tbody tr.active-row{
  font-weight: bold;
  color: var(--copper);
}

.styled-table td {
  padding: 6px 8px;
}

.button{
  background-color: var(--copper);
  border: none;
  color: #14181B;
  padding: 6px 12px;
  text-align: center;
  text-decoration: none;
  display: inline-block;
  margin: 4px 2px;
  font-family: var(--font-body);
  font-size: 12px;
  font-weight: 700;
  cursor: pointer;
  border-radius: 6px;
}

.button:focus-visible, .linkbutton:focus-visible {
  outline: 2px solid var(--copper);
  outline-offset: 2px;
}

</style>
)=====";
