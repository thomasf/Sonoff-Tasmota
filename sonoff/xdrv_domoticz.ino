/*
Copyright (c) 2017 Theo Arends.  All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

- Redistributions of source code must retain the above copyright notice,
  this list of conditions and the following disclaimer.
- Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
*/

#ifdef USE_DOMOTICZ

#define DOMOTICZ_MAX_SENSORS  5

const char HTTP_FORM_DOMOTICZ[] PROGMEM =
  "<fieldset><legend><b>&nbsp;Domoticz parameters&nbsp;</b></legend><form method='post' action='sv'>"
  "<input id='w' name='w' value='4' hidden><input id='r' name='r' value='1' hidden>"
  "<br/><table style='width:97%'>"
  "<tr><td><b>In topic</b> (" DOMOTICZ_IN_TOPIC ")</td><td style='width:30%'><input id='it' name='it' length=32 placeholder='" DOMOTICZ_IN_TOPIC "' value='{d1}'></td></tr>"
  "<tr><td><b>Out topic</b> (" DOMOTICZ_OUT_TOPIC ")</td><td><input id='ot' name='ot' length=32 placeholder='" DOMOTICZ_OUT_TOPIC "' value='{d2}'></td></tr>";

const char domoticz_sensors[DOMOTICZ_MAX_SENSORS][14] PROGMEM =
  { "Temp", "Temp,Hum", "Temp,Hum,Baro", "Power,Energy", "Illuminance" };

int domoticz_update_timer = 0;
byte domoticz_update_flag = 1;

unsigned long getKeyIntValue(const char *json, const char *key)
{
  char *p, *b, log[LOGSZ];
  int i;

  // search key
  p = strstr(json, key);
  if (!p) return 0;
  // search following separator :
  b = strchr(p + strlen(key), ':');
  if (!b) return 0;
  // Only the following chars are allowed between key and separator :
  for(i = b - json + strlen(key); i < p-json; i++) {
    switch (json[i]) {
    case ' ':
    case '\n':
    case '\t':
    case '\r':
      continue;
    default:
      return 0;
    }
  }
  b++;
  // Allow integers as string too (used in "svalue" : "9")
  while ((b[0] == ' ') || (b[0] == '"')) b++;
  // Convert to integer
  return atoi(b);
}

void mqtt_publishDomoticzPowerState(byte device)
{
  char svalue[MESSZ];

  if (sysCfg.domoticz_relay_idx[device -1] && (strlen(sysCfg.domoticz_in_topic) != 0)) {
    if ((device < 1) || (device > Maxdevice)) device = 1;

    if (sysCfg.module == SONOFF_LED) {
      snprintf_P(svalue, sizeof(svalue), PSTR("{\"idx\":%d,\"nvalue\":2,\"svalue\":\"%d\"}"),
        sysCfg.domoticz_relay_idx[device -1], sysCfg.led_dimmer[device -1]);
      mqtt_publish(sysCfg.domoticz_in_topic, svalue);
    }
    else if ((device == 1) && (pin[GPIO_WS2812] < 99)) {
      snprintf_P(svalue, sizeof(svalue), PSTR("{\"idx\":%d,\"nvalue\":2,\"svalue\":\"%d\"}"),
        sysCfg.domoticz_relay_idx[device -1], sysCfg.ws_dimmer);
      mqtt_publish(sysCfg.domoticz_in_topic, svalue);
    }
    snprintf_P(svalue, sizeof(svalue), PSTR("{\"idx\":%d,\"nvalue\":%d,\"svalue\":\"\"}"),
      sysCfg.domoticz_relay_idx[device -1], (power & (0x01 << (device -1))) ? 1 : 0);
    mqtt_publish(sysCfg.domoticz_in_topic, svalue);
  }
}

void domoticz_updatePowerState(byte device)
{
   if (domoticz_update_flag) mqtt_publishDomoticzPowerState(device);
   domoticz_update_flag = 1;
}

void domoticz_mqttUpdate()
{
  if ((sysCfg.domoticz_update_timer || domoticz_update_timer) && sysCfg.domoticz_relay_idx[0]) {
    domoticz_update_timer--;
    if (domoticz_update_timer <= 0) {
      domoticz_update_timer = sysCfg.domoticz_update_timer;
      for (byte i = 1; i <= Maxdevice; i++) mqtt_publishDomoticzPowerState(i);
    }
  }
}

void domoticz_setUpdateTimer(uint16_t value)
{
  domoticz_update_timer = value;
}

void domoticz_mqttSubscribe()
{
  if (sysCfg.domoticz_relay_idx[0] && (strlen(sysCfg.domoticz_out_topic) != 0)) {
    char stopic[TOPSZ];
    snprintf_P(stopic, sizeof(stopic), PSTR("%s/#"), sysCfg.domoticz_out_topic); // domoticz topic
    mqttClient.subscribe(stopic);
    mqttClient.loop();  // Solve LmacRxBlk:1 messages
  }
}

boolean domoticz_update()
{
  return domoticz_update_flag;
}

boolean domoticz_mqttData(char *topicBuf, uint16_t stopicBuf, char *dataBuf, uint16_t sdataBuf)
{
  char log[LOGSZ], stemp1[10];
  unsigned long idx = 0;
  int16_t nvalue, found = 0;
  
  domoticz_update_flag = 1;
  if (!strncmp(topicBuf, sysCfg.domoticz_out_topic, strlen(sysCfg.domoticz_out_topic)) != 0) {
    if (sdataBuf < 20) return 1;
    idx = getKeyIntValue(dataBuf,"\"idx\"");
    nvalue = getKeyIntValue(dataBuf,"\"nvalue\"");

    snprintf_P(log, sizeof(log), PSTR("DMTZ: idx %d, nvalue %d"), idx, nvalue);
    addLog(LOG_LEVEL_DEBUG_MORE, log);

    if (nvalue >= 0 && nvalue <= 2) {
      for (byte i = 0; i < Maxdevice; i++) {
        if ((idx > 0) && (idx == sysCfg.domoticz_relay_idx[i])) {
          snprintf_P(stemp1, sizeof(stemp1), PSTR("%d"), i +1);
          if (nvalue == 2) {
            nvalue = getKeyIntValue(dataBuf,"\"svalue1\"");
            if ((pin[GPIO_WS2812] < 99) && (sysCfg.ws_dimmer == nvalue)) return 1;
            if ((sysCfg.module == SONOFF_LED) && (sysCfg.led_dimmer[i] == nvalue)) return 1;
            snprintf_P(topicBuf, stopicBuf, PSTR("%s/%s/DIMMER%s"),
              SUB_PREFIX, sysCfg.mqtt_topic, (Maxdevice > 1) ? stemp1 : "");
            snprintf_P(dataBuf, sdataBuf, PSTR("%d"), nvalue);
            found = 1;
          } else {
            if (((power >> i) &1) == nvalue) return 1;
            snprintf_P(topicBuf, stopicBuf, PSTR("%s/%s/%s%s"),
              SUB_PREFIX, sysCfg.mqtt_topic, sysCfg.mqtt_subtopic, (Maxdevice > 1) ? stemp1 : "");
            snprintf_P(dataBuf, sdataBuf, PSTR("%d"), nvalue);
            found = 1;
          }
          break;
        }
      }
    }
    if (!found) return 1;

    snprintf_P(log, sizeof(log), PSTR("DMTZ: Receive topic %s, data %s"), topicBuf, dataBuf);
    addLog(LOG_LEVEL_DEBUG_MORE, log);

    domoticz_update_flag = 0;
  }
  return 0;
}

/*********************************************************************************************\
 * Commands
\*********************************************************************************************/

boolean domoticz_command(char *type, uint16_t index, char *dataBuf, uint16_t data_len, int16_t payload, char *svalue, uint16_t ssvalue)
{
  boolean serviced = true;
  
  if (!strcmp(type,"DOMOTICZINTOPIC")) {
    if ((data_len > 0) && (data_len < sizeof(sysCfg.domoticz_in_topic))) {
      strlcpy(sysCfg.domoticz_in_topic, (payload == 1) ? DOMOTICZ_IN_TOPIC : dataBuf, sizeof(sysCfg.domoticz_in_topic));
      restartflag = 2;
    }
    snprintf_P(svalue, ssvalue, PSTR("{\"DomoticzInTopic\":\"%s\"}"), sysCfg.domoticz_in_topic);
  }
  else if (!strcmp(type,"DOMOTICZOUTTOPIC")) {
    if ((data_len > 0) && (data_len < sizeof(sysCfg.domoticz_out_topic))) {
      strlcpy(sysCfg.domoticz_out_topic, (payload == 1) ? DOMOTICZ_OUT_TOPIC : dataBuf, sizeof(sysCfg.domoticz_out_topic));
      restartflag = 2;
    }
    snprintf_P(svalue, ssvalue, PSTR("{\"DomoticzOutTopic\":\"%s\"}"), sysCfg.domoticz_out_topic);
  }
  else if (!strcmp(type,"DOMOTICZIDX") && (index > 0) && (index <= Maxdevice)) {
    if ((data_len > 0) && (payload >= 0)) {
      sysCfg.domoticz_relay_idx[index -1] = payload;
      restartflag = 2;
    }
    snprintf_P(svalue, ssvalue, PSTR("{\"DomoticzIdx%d\":%d}"), index, sysCfg.domoticz_relay_idx[index -1]);
  }
  else if (!strcmp(type,"DOMOTICZKEYIDX") && (index > 0) && (index <= Maxdevice)) {
    if ((data_len > 0) && (payload >= 0)) {
      sysCfg.domoticz_key_idx[index -1] = payload;
    }
    snprintf_P(svalue, ssvalue, PSTR("{\"DomoticzKeyIdx%d\":%d}"), index, sysCfg.domoticz_key_idx[index -1]);
  }
  else if (!strcmp(type,"DOMOTICZSWITCHIDX") && (index > 0) && (index <= Maxdevice)) {
    if ((data_len > 0) && (payload >= 0)) {
      sysCfg.domoticz_switch_idx[index -1] = payload;
    }
    snprintf_P(svalue, ssvalue, PSTR("{\"DomoticzSwitchIdx%d\":%d}"), index, sysCfg.domoticz_key_idx[index -1]);
  }
  else if (!strcmp(type,"DOMOTICZSENSORIDX") && (index > 0) && (index <= DOMOTICZ_MAX_SENSORS)) {
    if ((data_len > 0) && (payload >= 0)) {
      sysCfg.domoticz_sensor_idx[index -1] = payload;
    }
    snprintf_P(svalue, ssvalue, PSTR("{\"DomoticzSensorIdx%d\":%d}"), index, sysCfg.domoticz_sensor_idx[index -1]);
  }
  else if (!strcmp(type,"DOMOTICZUPDATETIMER")) {
    if ((data_len > 0) && (payload >= 0) && (payload < 3601)) {
      sysCfg.domoticz_update_timer = payload;
    }
    snprintf_P(svalue, ssvalue, PSTR("{\"DomoticzUpdateTimer\":%d}"), sysCfg.domoticz_update_timer);
  }
  else {
    serviced = false;
  }
  return serviced;
}

void domoticz_commands(char *svalue, uint16_t ssvalue)
{
  snprintf_P(svalue, ssvalue, PSTR("{\"Commands\":\"DomoticzInTopic, DomoticzOutTopic, DomoticzIdx, DomoticzKeyIdx, DomoticzSwitchIdx, DomoticzSensorIdx, DomoticzUpdateTimer\"}"));
}

boolean domoticz_button(byte key, byte device, byte state, byte svalflg)
{
  if ((sysCfg.domoticz_key_idx[device -1] || sysCfg.domoticz_switch_idx[device -1]) && (svalflg)) {
    char svalue[MESSZ];

    snprintf_P(svalue, sizeof(svalue), PSTR("{\"command\":\"switchlight\",\"idx\":%d,\"switchcmd\":\"%s\"}"),
      (key) ? sysCfg.domoticz_switch_idx[device -1] : sysCfg.domoticz_key_idx[device -1], (state) ? (state == 2) ? "Toggle" : "On" : "Off");
    mqtt_publish(sysCfg.domoticz_in_topic, svalue);
    return 1;
  } else {
    return 0;
  }
}

/*********************************************************************************************\
 * Sensors
\*********************************************************************************************/

uint8_t dom_hum_stat(char *hum)
{
  uint8_t h = atoi(hum);
  return (!h) ? 0 : (h < 40) ? 2 : (h > 70) ? 3 : 1;
}

void dom_sensor(byte idx, char *data)
{
  char dmess[64];

  if (sysCfg.domoticz_sensor_idx[idx] && (strlen(sysCfg.domoticz_in_topic) != 0)) {
    snprintf_P(dmess, sizeof(dmess), PSTR("{\"idx\":%d,\"nvalue\":0,\"svalue\":\"%s\"}"),
      sysCfg.domoticz_sensor_idx[idx], data);
    mqtt_publish(sysCfg.domoticz_in_topic, dmess);
  }
}

void domoticz_sensor1(char *temp)
{
  dom_sensor(0, temp);
}

void domoticz_sensor2(char *temp, char *hum)
{
  char data[16];
  snprintf_P(data, sizeof(data), PSTR("%s;%s;%d"), temp, hum, dom_hum_stat(hum));
  dom_sensor(1, data);
}

void domoticz_sensor3(char *temp, char *hum, char *baro)
{
  char data[32];
  snprintf_P(data, sizeof(data), PSTR("%s;%s;%d;%s;5"), temp, hum, dom_hum_stat(hum), baro);
  dom_sensor(2, data);
}

void domoticz_sensor4(uint16_t power, char *energy)
{
  char data[16];
  snprintf_P(data, sizeof(data), PSTR("%d;%s"), power, energy);
  dom_sensor(3, data);
}

void domoticz_sensor5(uint16_t lux)
{
  char data[8];
  snprintf_P(data, sizeof(data), PSTR("%d"), lux);
  dom_sensor(4, data);
}

/*********************************************************************************************\
 * Presentation
\*********************************************************************************************/

void handleDomoticz()
{
  if (_httpflag == HTTP_USER) {
    handleRoot();
    return;
  }
  addLog_P(LOG_LEVEL_DEBUG, PSTR("HTTP: Handle Domoticz config"));

  char stemp[20];
  
  String page = FPSTR(HTTP_HEAD);
  page.replace("{v}", "Configure Domoticz");
  page += FPSTR(HTTP_FORM_DOMOTICZ);
  page.replace("{d1}", String(sysCfg.domoticz_in_topic));
  page.replace("{d2}", String(sysCfg.domoticz_out_topic));
  for (int i = 0; i < Maxdevice; i++) {
    page += F("<tr><td><b>Idx {1</b></td></td><td><input id='r{1' name='r{1' length=8 placeholder='0' value='{2'></td></tr>");
    page += F("<tr><td><b>Key idx {1</b></td><td><input id='k{1' name='k{1' length=8 placeholder='0' value='{3'></td></tr>");
    page += F("<tr><td><b>Switch idx {1</b></td><td><input id='s{1' name='s{1' length=8 placeholder='0' value='{4'></td></tr>");
    page.replace("{1", String(i +1));
    page.replace("{2", String((int)sysCfg.domoticz_relay_idx[i]));
    page.replace("{3", String((int)sysCfg.domoticz_key_idx[i]));
    page.replace("{4", String((int)sysCfg.domoticz_switch_idx[i]));
  }
  for (int i = 0; i < DOMOTICZ_MAX_SENSORS; i++) {
    page += F("<tr><td><b>Sensor idx {1</b> - {2</td><td><input id='l{1' name='l{1' length=8 placeholder='0' value='{4'></td></tr>");
    page.replace("{1", String(i +1));
    snprintf_P(stemp, sizeof(stemp), domoticz_sensors[i]);
    page.replace("{2", stemp);
    page.replace("{4", String((int)sysCfg.domoticz_sensor_idx[i]));
  }
  page += F("<tr><td><b>Update timer</b> (" STR(DOMOTICZ_UPDATE_TIMER) ")</td><td><input id='ut' name='ut' length=32 placeholder='" STR(DOMOTICZ_UPDATE_TIMER) "' value='{d7}'</td></tr>");
  page.replace("{d7}", String((int)sysCfg.domoticz_update_timer));
  page += F("</table>");
  page += FPSTR(HTTP_FORM_END);
  page += FPSTR(HTTP_BTN_CONF);
  showPage(page);
}

void domoticz_saveSettings()
{
  char log[LOGSZ], stemp[20];

  strlcpy(sysCfg.domoticz_in_topic, (!strlen(webServer->arg("it").c_str())) ? DOMOTICZ_IN_TOPIC : webServer->arg("it").c_str(), sizeof(sysCfg.domoticz_in_topic));
  strlcpy(sysCfg.domoticz_out_topic, (!strlen(webServer->arg("ot").c_str())) ? DOMOTICZ_OUT_TOPIC : webServer->arg("ot").c_str(), sizeof(sysCfg.domoticz_out_topic));
  for (byte i = 0; i < 4; i++) {
    snprintf_P(stemp, sizeof(stemp), PSTR("r%d"), i +1);
    sysCfg.domoticz_relay_idx[i] = (!strlen(webServer->arg(stemp).c_str())) ? 0 : atoi(webServer->arg(stemp).c_str());
    snprintf_P(stemp, sizeof(stemp), PSTR("k%d"), i +1);
    sysCfg.domoticz_key_idx[i] = (!strlen(webServer->arg(stemp).c_str())) ? 0 : atoi(webServer->arg(stemp).c_str());
    snprintf_P(stemp, sizeof(stemp), PSTR("s%d"), i +1);
    sysCfg.domoticz_switch_idx[i] = (!strlen(webServer->arg(stemp).c_str())) ? 0 : atoi(webServer->arg(stemp).c_str());
  }
  for (byte i = 0; i < DOMOTICZ_MAX_SENSORS; i++) {
    snprintf_P(stemp, sizeof(stemp), PSTR("l%d"), i +1);
    sysCfg.domoticz_sensor_idx[i] = (!strlen(webServer->arg(stemp).c_str())) ? 0 : atoi(webServer->arg(stemp).c_str());
  }
  sysCfg.domoticz_update_timer = (!strlen(webServer->arg("ut").c_str())) ? DOMOTICZ_UPDATE_TIMER : atoi(webServer->arg("ut").c_str());
  snprintf_P(log, sizeof(log), PSTR("HTTP: Domoticz in %s, out %s, idx %d, %d, %d, %d, update timer %d"),
    sysCfg.domoticz_in_topic, sysCfg.domoticz_out_topic,
    sysCfg.domoticz_relay_idx[0], sysCfg.domoticz_relay_idx[1], sysCfg.domoticz_relay_idx[2], sysCfg.domoticz_relay_idx[3],
    sysCfg.domoticz_update_timer);
  addLog(LOG_LEVEL_INFO, log);
  snprintf_P(log, sizeof(log), PSTR("HTTP: key %d, %d, %d, %d, switch %d, %d, %d, %d, sensor %d, %d, %d, %d, %d"),
    sysCfg.domoticz_key_idx[0], sysCfg.domoticz_key_idx[1], sysCfg.domoticz_key_idx[2], sysCfg.domoticz_key_idx[3],
    sysCfg.domoticz_switch_idx[0], sysCfg.domoticz_switch_idx[1], sysCfg.domoticz_switch_idx[2], sysCfg.domoticz_switch_idx[3],
    sysCfg.domoticz_sensor_idx[0], sysCfg.domoticz_sensor_idx[1], sysCfg.domoticz_sensor_idx[2], sysCfg.domoticz_sensor_idx[3], sysCfg.domoticz_sensor_idx[4]);
  addLog(LOG_LEVEL_INFO, log);
}
#endif  // USE_DOMOTICZ

