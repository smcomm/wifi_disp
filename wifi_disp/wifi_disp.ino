#include <FS.h>
#define VER "1.1"
#define HOSTNAME "disp_"
extern "C" {
#include "user_interface.h"
}
char disp_buf[22];
uint32_t next_disp = 120; //下次开机
String hostname = HOSTNAME;
float v;
uint8_t proc; //用lcd ram 0 传递过来的变量， 用于通过重启，进行功能切换
//0,1-正常 2-AP 3-OTA  4-http update
#define AP_MODE 2
#define OTA_MODE 3
#define HTTP_UPDATE_MODE 4

#include "ota.h"
#include "ds1820.h"
#include "wifi_client.h"
#include "ap_web.h"
#include "ht16c21.h"
#include "http_update.h"

bool power_in = false;
void setup()
{
  uint8_t i;
  float v0;
  Serial.begin(115200);
  Serial.println();
  Serial.println("Software Ver=" VER);
  hostname += String(ESP.getChipId(), HEX);
  WiFi.hostname(hostname);
  Serial.println("Hostname: " + hostname);
  delay(100);
  Serial.flush();
  ds_init();
  ht16c21_setup();
  pinMode(13, OUTPUT);
  digitalWrite(13, HIGH);
  get_batt();
  v0 = v;
  Serial.print("电池电压");
  Serial.println(v);
  digitalWrite(13, LOW);
  get_batt();
  digitalWrite(13, HIGH);
  if (v - v0 > 0.1) { //有外接电源
    v0 = v;
    get_batt();
    if (v0 - v > 0.1) {
      power_in = true;
      Serial.println("外接电源");
    }
  }
  ht16c21_cmd(0x84,3);  //0-关闭  3-开启
  if (!load_ram() && !load_ram() && !load_ram()) {
    ram_buf[0] = 0xff; //读取错误
    ram_buf[7] = 0; // 1 充电， 0 不充电
  }
  proc = ram_buf[0];
  switch (proc) {
    case HTTP_UPDATE_MODE: //http_update
      ram_buf[0] = 0;
      disp("H UP ");
      break;
    case OTA_MODE:
      ram_buf[0] = HTTP_UPDATE_MODE;//ota以后，
      disp(" OTA ");
      break;
    case AP_MODE:
      ram_buf[0] = OTA_MODE; //ota
      send_ram();
      AP();
      return;
      break;
    default:
      ram_buf[0] = AP_MODE;
      sprintf(disp_buf, " %3.2f ", v);
      disp(disp_buf);
      break;
  }
  send_ram();
  //更新时闪烁
  ht16c21_cmd(0x88, 1); //闪烁

  if (wifi_connect() == false) {
    ram_buf[9] |= 0x10; //x1
    ram_buf[0] = 0;
    send_ram();

    Serial.print("不能链接到AP\r\n20分钟后再试试\r\n本次上电时长");
    Serial.print(millis());
    Serial.println("ms");
    poweroff(1200);
    return;
  }
  ht16c21_cmd(0x88, 0); //停止闪烁
  if (proc == AP_MODE) return;
  if (proc == OTA_MODE) {
    ota_setup();
    return;
  }
  if (proc == HTTP_UPDATE_MODE) {
    if (http_update() == true) {
      ram_buf[0] = 0;
      send_ram();
    }

    ESP.restart();
    return;
  }
  uint16_t httpCode = http_get();
  if (httpCode >= 400) {
    Serial.print("不能链接到web\r\n20分钟后再试试\r\n本次上电时长");
    Serial.print(millis());
    Serial.println("ms");
    poweroff(1200);
    return;
  }
  Serial.print("uptime=");
  Serial.print(millis());
  if (next_disp == 0) next_disp = 120;
  Serial.print("ms,sleep=");
  Serial.println(next_disp);
  poweroff(next_disp);
}
bool power_off = false;
void poweroff(uint32_t sec) {
  wdt_disable();
  if (ram_buf[7] & 1) {
    digitalWrite(13, LOW);
  } else {
    digitalWrite(13, HIGH);
  }

  if (power_in) { //如果外面接了电， 就进入LIGHT_SLEEP模式 电流0.8ma， 保持充电
    sec = sec / 2;
    if (ram_buf[7] & 1)
      Serial.println("充电中");
    wifi_set_sleep_type(LIGHT_SLEEP_T);
    Serial.print("休眠");
    if (sec > 60) {
      Serial.print(sec / 60);
      Serial.print("分钟");
    }
    Serial.print(sec % 60);
    Serial.println("秒");
    delay(sec * 1000); //空闲时进入LIGHT_SLEEP_T模式
  }
  if (ram_buf[7] & 1)
    Serial.println("充电结束");
  Serial.print("关机");
  if (sec > 60) {
    Serial.print(sec / 60);
    Serial.print("分钟");
  }
  Serial.print(sec % 60);
  Serial.println("秒");
  Serial.println("bye!");
  Serial.flush();
  system_deep_sleep_set_option(0);
  digitalWrite(LED_BUILTIN, LOW);
  ESP.deepSleep((uint64_t) 1000000 * sec, WAKE_RF_DEFAULT);
  //system_deep_sleep((uint64_t)1000000 * sec);
  power_off = true;
}

float get_batt() {//锂电池电压
  uint32_t dat = analogRead(A0);
  dat = analogRead(A0)
        + analogRead(A0)
        + analogRead(A0)
        + analogRead(A0)
        + analogRead(A0)
        + analogRead(A0)
        + analogRead(A0)
        + analogRead(A0);
  v = dat / 8 * (499 + 97.6) / 97.6 / 1023 ;
  return v;
}
void loop()
{
  if (power_off) return;
  switch (proc) {
    case OTA_MODE:
      ota_loop();
      break;
    case AP_MODE:
      ap_loop();
      break;
  }
}