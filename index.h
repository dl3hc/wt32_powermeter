const char MAIN_page[] PROGMEM = R"=====(
<body>
<div class="grid-container">

  <div id="title_box" class="titlebox maintitlebox">
    <span id="AntennaName"></span>
  </div>

  <div id="title_box" class="bandbox maintitlebox">
    Band: <span id="BANDValue">0</span>
  </div>

  <div class="fwd_title subtitlebox">
    FWD Power
  </div>

  <div class="ref_title subtitlebox">
    REF Power
  </div>

  <div class="vswr_title subtitlebox">
    VSWR
  </div>

  <div id="fwd_box" class="box fwdcontent contentbox_text">
    <div class="innerbox_left">
      <p><span id="FWDWatt">0</span> </br><span id="FWDdBm">0</span> </br><span id="FWDVoltage">0</span>
    </div>
    <div id="fwd_led_box" class="innerbox_right">
      <span id="max_led_pwr_fwd">0</span> W
      <section class="main">
        <canvas id="fwd_vu_meter" width="30" height="150" data-val="0">No canvas</canvas>
      </section>
      0 W
    </div>
  </div>

  <div id="ref_box" class="box refcontent contentbox_text">
    <div class="innerbox_left">
      <p><span id="REFWatt">0</span> </br><span id="REFdBm">0</span> </br><span id="REFVoltage">0</span>
    </div>
    <div id="ref_led_box" class="innerbox_right">
      <span id="max_led_pwr_ref">0</span> W
      <section class="main">
        <canvas id="ref_vu_meter" width="30" height="150" data-val="0">No canvas</canvas>
      </section>
      0 W
    </div>
  </div>


  <div id="vswr_box" class="box vswrcontent contentbox_text">
    <div class="innerbox_left">
      <p><span id="VSWRValue">0</span> </br>RL: <span id="RLValue">0</span> dB
    </div>
    <div id="vswr_led_box" class="innerbox_right">
      <span id="max_led_vswr">0</span>
      <section class="main">
        <canvas id="swr_vu_meter" width="30" height="150" data-val="0">No canvas</canvas>
      </section>
      1
    </div>
  </div>

  <div class="footerbox">
    <form method='post' action='config'><button class='button' value='config' name='config' type='submit'>Configuration</button> - wt32powermeter v<span id="version">0</span> by DK1MI</form>
  </div>

  <div id="temp_box" class="tempbox">
    <span id="TEMPValue">0</span>
  </div>

</div>

</body>
</html>
)=====";