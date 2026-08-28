const char MAIN_page[] PROGMEM = R"=====(
<body>
<div class="panel">

  <header class="nameplate">
    <span class="nameplate__brand">WT32 Power Meter</span>
    <span class="nameplate__antenna" id="AntennaName"></span>
    <span class="nameplate__band">Band <b id="BANDValue">0</b></span>
  </header>

  <main class="modules">

    <section id="fwd_box" class="module module--fwd">
      <h2 class="module__label">FWD Power</h2>
      <div class="module__body">
        <div class="module__readout">
          <span class="value" id="FWDWatt">0</span>
          <span class="value value--sub" id="FWDdBm">0</span>
          <span class="value value--sub" id="FWDVoltage">0</span>
        </div>
        <div id="fwd_led_box" class="bargraph">
          <span class="bargraph__max"><span id="max_led_pwr_fwd">0</span> W</span>
          <canvas id="fwd_vu_meter" width="30" height="150" data-val="0">No canvas</canvas>
          <span class="bargraph__min">0 W</span>
        </div>
      </div>
    </section>

    <section id="ref_box" class="module module--ref">
      <h2 class="module__label">REF Power</h2>
      <div class="module__body">
        <div class="module__readout">
          <span class="value" id="REFWatt">0</span>
          <span class="value value--sub" id="REFdBm">0</span>
          <span class="value value--sub" id="REFVoltage">0</span>
        </div>
        <div id="ref_led_box" class="bargraph">
          <span class="bargraph__max"><span id="max_led_pwr_ref">0</span> W</span>
          <canvas id="ref_vu_meter" width="30" height="150" data-val="0">No canvas</canvas>
          <span class="bargraph__min">0 W</span>
        </div>
      </div>
    </section>

    <section id="vswr_box" class="module module--vswr">
      <h2 class="module__label">VSWR</h2>
      <div class="module__body">
        <div class="module__readout">
          <span class="value" id="VSWRValue">0</span>
          <span class="value value--sub">RL <span id="RLValue">0</span> dB</span>
        </div>
        <div id="vswr_led_box" class="bargraph">
          <span class="bargraph__max"><span id="max_led_vswr">0</span></span>
          <canvas id="swr_vu_meter" width="30" height="150" data-val="0">No canvas</canvas>
          <span class="bargraph__min">1</span>
        </div>
      </div>
    </section>

  </main>

  <footer class="statusbar">
    <span class="statusbar__temp" id="temp_box"><span id="TEMPValue">0</span></span>
    <form method="post" action="config"><button class="button" value="config" name="config" type="submit">Configuration</button></form>
    <span class="statusbar__version">wt32powermeter v<span id="version">0</span> by DK1MI, maintained by DL3HC</span>
  </footer>

</div>
</body>
</html>
)=====";
