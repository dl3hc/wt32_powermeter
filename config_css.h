const char CFG_STYLESHEET[] PROGMEM = R"=====(
<!DOCTYPE html>
<html>
<style>

body {
  background: #FFFFFF;
  font-family: verdana, sans-serif;
  color: #009879;
  font-style: normal;
  font-weight: bold;
  font-variant: normal;
  text-align: center;
  border-color: #009879;
  letter-spacing: 0px;
}

.grid-container {
  display: grid;
  grid-template-columns: 320px 320px;
  grid-column-gap: 5px;
  grid-row-gap: 5px;
}

.titlebox  {
  grid-column: 1;
  grid-row: 1;
}

.maintitlebox {
  border-style: solid;
  font-size: 20px;
  line-height: 40px;
}

.subtitlebox {
  border-style: solid;
  font-size: 16px;
}

.contentbox {
  border-style: solid;
  line-height: 20px;
  padding: 0px;
  margin: 0px;
  font-size: 16px;
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
  border-style: solid;
  line-height: 20px;
  font-size: 12px;
}

textarea {
  width: 310px;
}

.linkbutton {
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

.styled-table{
  border-collapse: collapse;
  font-size: 0.9em;
  font-family: sans-serif;
  min-width: 400px;
  width: 100%;
  box-shadow: 0 0 20px rgba(0, 0, 0, 0.15);
}

.styled-table thead tr{
  background-color: #009879;
  color: #ffffff;
  text-align: left;
}

.styled-table tbody tr{
  border-bottom: 1px solid #dddddd;
}

.styled-table tbody tr:nth-of-type(even){
  background-color: #f3f3f3;
}
.styled-table tbody tr:last-of-type{
  border-bottom: 2px solid #009879;
}

.styled-table tbody tr.active-row{
  font-weight: bold;
  color: #009879;
}

.button{
  background-color: #009879;
  border: none;
  color: white;
  padding: 5px 5px;
  text-align: center;
  text-decoration: none;
  display: inline-block;
  margin: 4px 2px;
  cursor: pointer;
  border-radius: 8px;
}

</style>
)=====";