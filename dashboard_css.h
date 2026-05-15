const char DB_STYLESHEET[] PROGMEM = R"=====(
<!DOCTYPE html>
<html>
<style>

body {
  background: #FFFFFF;
  font-family: verdana, sans-serif;
  font-size: 12px;
  color: #009879;
  font-style: normal;
  font-weight: bold;
  font-variant: normal;
  text-align: center;
  border-color: #009879;
}

.grid-container {
  display: grid;
  grid-template-columns: 180px 180px 180px;
  grid-column-gap: 5px;
  grid-row-gap: 5px;
}

.box{
  background: #009879;
  padding: 5px;
  box-sizing: border-box;
  vertical-align: top;
  color: #FFF;
  margin:0px;
  display: inline-block;
}

.innerbox_left{
  float:left;
  vertical-align: middle;
  padding: 50px 0;
  margin: 5px;
  height: 90%;
}

.innerbox_right{
  padding: 5px;
  float:right;
  width: 30%;
  vertical-align: middle;
}

.fwdtitle {
  grid-column: 1;
  grid-row: 2;
}

.reftitle {
  grid-column: 2;
  grid-row: 2;
}

.vswrtitle {
  grid-column: 3;
  grid-row: 2;
}

.fwdcontent {
  grid-column: 1;
  grid-row: 3;
}

.refcontent {
  grid-column: 2;
  grid-row: 3;
}

.vswrcontent {
  grid-column: 3;
  grid-row: 3;
}

.titlebox  {
  grid-column: 1 / span 2;
  grid-row: 1;
}

.bandbox  {
  grid-column: 3;
  grid-row: 1;
}

.footerbox  {
  grid-column: 1 / span 2;
  grid-row: 4;
  border-style: solid;
  font-size: 12px;
  line-height: 20px;
}

.tempbox  {
  grid-column: 3;
  grid-row: 4;
  border-style: solid;
  font-size: 12px;
  line-height: 20px;
}

.subtitlebox {
  border-style: solid;
  font-size: 14px;
}

.maintitlebox {
  border-style: solid;
  font-size: 18px;
}

.contentbox_text {
  font-size: 14px;
  text-align: center;
  line-height: 30px;
}

.redbox{
  background: #FFAA79;
}

.button {
  background: none!important;
  border: none;
  padding: 0!important;
  font-family: verdana, sans-serif;
  font-size: 12px;
  font-weight: bold;
  color: #009879;
  text-decoration: underline;
  cursor: pointer;
}


</style>
)=====";