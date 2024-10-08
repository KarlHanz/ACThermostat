/*
   https://hub.gyver.ru/old/
  в настройках web: test.mosquitto.org, 8081
  "Thermostat", "Wemos",

  //https://github.com/GyverLibs/GyverHub

  Библиотека SimplePortal допилена под этот проект! Официальная работать не будет!

  !Долгая инициализация сенсоров
  !Не работают уведомления в бот об изм темп

*/


#define AP_SSID ""
#define AP_PASS ""


#define BOT_TOKEN ""
#define CHAT_ID ""


#define relay1 D1
#define relay2 D2
#define btn D7
#define buzz D0

#include <Arduino.h>
#include <GyverHub.h>
#include <FastBot.h>
#include <SimplePortal.h>
#include <ESP8266WiFi.h>
#include <EEPROM.h>
#include <OneWire.h>
#include <DallasTemperature.h>

#define ONE_WIRE_BUS_1 D5 // 
#define ONE_WIRE_BUS_2 D6 // 
OneWire oneWire_1(ONE_WIRE_BUS_1);
OneWire oneWire_2(ONE_WIRE_BUS_2);
DallasTemperature sensor_1(&oneWire_1);
DallasTemperature sensor_2(&oneWire_2);

FastBot bot(BOT_TOKEN);
GyverHub hub("Thermostat", "Wemos", ""); //ID для web //Wemos

struct LoginPass {
  char ssid[32];
  char pass[32];
  char token[64];
  char chatid[32];
};
LoginPass lp;

GHbutton b1, b2;
unsigned long prev_connect = 0;     //Время последней попытки подключения
unsigned long time_notconnected = 0;  //Таймер запоминает время в котором нет подключения
unsigned long time_connected = 0;     //Таймер запоминает время в которое еще есть подключение к wifi
float temp1, temp2 = 25;
float tempMdl = 25;
int16_t sld1 = 35; //умолчание датчик температуры для кондиционера 1
int16_t sld2 = 35; //умолчание датчик температуры для кондиционера 2
int16_t sld3 = 30; //умолчание для перегрева помещения
bool sw1, sw2 = 0;
bool led1, led2, relay1State, relay2State = 0;
bool lastRelay1State, lastRelay2State = 0;
bool alarmEnable = 1; //разрешаем отправку тревоги
bool sendDataTemp = 1;
bool msgFlag1, msgFlag2, msgFlag3, msgFlag4, msgFlag5 = 1;
String label_s;

void build() {
  GHbuild b = hub.getBuild();  // получить свойства текущего билда
  if (b.type == GH_BUILD_ACTION) {                // это действие
    Serial.println(b.action.name);                // имя компонента
    Serial.println(b.action.value);               // значение
    Serial.println(); //для отслеживания факта отправки значений со слайдеров
  }

  // BeginWidgets достаточно вызвать один раз, интерфейс будет собираться сам
  hub.BeginWidgets();

  // Индикация
  hub.Title(F("Статус контактора"));
  hub.WidgetSize(50);
  hub.LED_(F("led1"), led1, F("Контактор 1")); //
  hub.LED_(F("led2"), led2, F("Контактор 2")); //

  // Температура
  hub.Title(F("Датчики температуры"));
  hub.WidgetSize(50);
  hub.Gauge_(F("ga1"), temp1, F("°C"), F("Кондиционер 1"), 10, 50, 1, GH_GREEN);
  hub.Gauge_(F("ga2"), temp2, F("°C"), F("Кондиционер 2"), 10, 50, 1, GH_GREEN);
  hub.WidgetSize(100);
  hub.Label_(F("lbl"), label_s, F("Средняя температура"));   //


  // Установка температур срабатывания
  hub.Title(F("Установка температуры"));
  hub.WidgetSize(100);
  hub.Slider(&sld1, GH_INT16, F("Средняя темп включения кондиционера 1"), 10, 50, 1, GH_BLUE);
  hub.Slider(&sld2, GH_INT16, F("Средняя темп включения кондиционера 2"), 10, 50, 1, GH_BLUE);
  hub.Slider(&sld3, GH_INT16, F("Уведомления о превышении температуры помещения"), 10, 50, 1, GH_RED);

  // Принудительное управление
  hub.Title(F("Ручное управление"));
  hub.WidgetSize(25);
  hub.Switch(&sw1, F("Ручное конд 1"));
  hub.Button(&b1, F("ВКЛ/ВЫКЛ"), GH_BLUE);
  hub.Switch(&sw2, F("Ручное конд 2"));
  hub.Button(&b2, F("ВКЛ/ВЫКЛ"), GH_BLUE);

  hub.EndWidgets(); //Если вся панель управления состоит из виджетов - вызывать EndWidgets() в конце не нужно.
} //end build

void setup() {
  Serial.begin(9600);
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(btn, INPUT_PULLUP);
  pinMode(relay1, OUTPUT);
  pinMode(relay2, OUTPUT);
  pinMode(buzz, OUTPUT);

  if (!digitalRead(btn)) {
    digitalWrite(buzz, LOW);
    portalStart(); // если кнопка нажата - открываем портал
  }

  sensor_1.begin();
  sensor_2.begin();

  EEPROM.begin(300);
  EEPROM.get(0, lp); // читаем логин пароль из памяти

  Serial.println("");
  Serial.print("ssid= ");
  Serial.println(lp.ssid);
  Serial.print("pass= ");
  Serial.println(lp.pass);
  Serial.print("token= ");
  Serial.println(lp.token);
  Serial.print("chatid= ");
  Serial.println(lp.chatid);


  connectWiFi(); //подпрограмма должна быть после чтения EEPROM.get(0, lp)
  bot.setToken(lp.token);

  hub.setupMQTT("test.mosquitto.org", 1883);
  hub.onBuild(build); // обработчик билда
  hub.onInfo([](GHinfo_t info) { // добавить поля в Info
    switch (info) {
      case GH_INFO_VERSION:
        hub.addInfo(F("Custom_ver"), F("v1.5"));
        break;
      case GH_INFO_NETWORK:
        hub.addInfo(F("Custom_net"), "net value");
        break;
      case GH_INFO_MEMORY:
        hub.addInfo(F("Custom_mem"), String(123));
        break;
      case GH_INFO_SYSTEM:
        hub.addInfo(F("Custom_sys"), "SimpleDevices 07/2023");
        break;
    }
  });
  hub.begin();    // запустить HUB


  bot.setChatID(lp.chatid);
  bot.setBufferSizes(512, 1024); // установить размеры буфера на приём и отправку
  delay(1000);
  bot.attach(newMsg);   // подключаем функцию-обработчик
  bot.sendMessage("Thermostat started!"); //сообщение при старте
  bot.showMenu("Уведомления ВКЛ \t Уведомления ОТКЛ \n Запрос температуры");



}//end setup


void loop() {

  if (portalTick()) { //обработчик новой сети WiFi
    Serial.println(portalStatus());
    if (portalStatus() == SP_SUBMIT) {
      strcpy (lp.ssid, portalCfg.SSID); // копируем
      strcpy (lp.pass, portalCfg.pass);
      strcpy (lp.token, portalCfg.token);
      strcpy (lp.chatid, portalCfg.chatid);

      EEPROM.put(0, lp);              // сохраняем
      EEPROM.commit();                // записываем
      Serial.print("ssid= ");
      Serial.println(lp.ssid);
      Serial.print("pass= ");
      Serial.println(lp.pass);
      Serial.print("token= ");
      Serial.println(lp.token);
      Serial.print("chatid= ");
      Serial.println(lp.chatid);

    }
    delay(100);
    ESP.restart(); //перезагрузка
  }


  stat_wifi();                      //Постоянная проверка состояния подключения к сети wifi
  hub.tick();
  bot.tick();


  //-------формирование температуры---------
  //static GHtimer tmr1(10000); //таймер на 10 секунд
  static GHtimer tmr1(0, 0, 1); // таймер на 1 минуту
  if (tmr1.ready()) {
    sensor_1.setResolution(9);
    sensor_2.setResolution(9);
    sensor_1.requestTemperatures();
    sensor_2.requestTemperatures();
    temp1 = sensor_1.getTempCByIndex(0); //sensor N1
    temp2 = sensor_2.getTempCByIndex(0); //sensor N2

    //-----автовыбор сенсора температуры и формирование данных температуры---------
    if ((temp1 <= 0 || temp1 >= 50) && (temp2 <= 0 || temp2 >= 50)) tempMdl = 25;
    else if (temp1 <= 0 || temp1 >= 50)  tempMdl = temp2;
    else if (temp2 <= 0 || temp2 >= 50)  tempMdl = temp1;
    else tempMdl = ((float)temp1 + (float)temp2) / 2;
    hub.sendUpdate("ga1", String(temp1)); // обновляем показания раз в 1 минуту
    hub.sendUpdate("ga2", String(temp2)); //
    hub.sendUpdate("lbl", String(tempMdl)); //

    //------проверка условий--------
    if (tempMdl >= sld3 && msgFlag5 == 1)  { //
      bot.sendMessage("Перегрев помещения!"); //уведомление неотключаемое!
      msgFlag5 = 0;
    }

    if (tempMdl < sld3 && msgFlag5 == 0)  { // сброс уведомления
      msgFlag5 = 1;
    }

    if ((temp1 < sld1) && alarmEnable == 1 && msgFlag1 == 1)  { //Уведомление не работает
      bot.sendMessage("Температура для конд 1 в норме!"); //
      msgFlag1 = 0;
      msgFlag2 = 1;
    }

    if ((temp1 >= sld1) && alarmEnable == 1 && msgFlag2 == 1)  { //Уведомление не работает
      bot.sendMessage("Температура для конд 1 повышена!");
      msgFlag2 = 0;
      msgFlag1 = 1;
    }
    if ((temp2 < sld2) && alarmEnable == 1 && msgFlag3 == 1)  { //Уведомление не работает
      bot.sendMessage("Температура для конд 2 в норме!");
      msgFlag3 = 0;
      msgFlag4 = 1;
    }

    if ((temp2 >= sld2) && alarmEnable == 1 && msgFlag4 == 1)  { //Уведомление не работает
      bot.sendMessage("Температура для конд 2 повышена!");
      msgFlag4 = 0;
      msgFlag3 = 1;
    }
  } //end timer



  static GHtimer tmr2(500); //отправка обновлений не чаще два раза в сек
  if (tmr2) {
    //-------обработчик авто режима (автоматическое вкл/выкл)---------
    if (sw1 == 0 && (tempMdl >= sld1)) { //если средняя темп выше установленной
      relay1State = 1; //включить реле 1
      led1 = relay1State; //отобразить статус светодиодом
      hub.sendUpdate("led1", String(led1)); // обновляем led1
      digitalWrite(relay1, relay1State);
    }
    if (sw1 == 0 && (tempMdl < sld1)) { //если средняя темп ниже установленной
      relay1State = 0; //вЫключить реле 1
      led1 = relay1State; //отобразить статус светодиодом
      hub.sendUpdate("led1", String(led1)); // обновляем led1
      digitalWrite(relay1, relay1State);
    }

    if (sw2 == 0 && (tempMdl >= sld2)) { //если средняя темп выше установленной
      relay2State = 1; //включить реле 2
      led2 = relay2State; //отобразить статус светодиодом
      hub.sendUpdate("led2", String(led2)); // обновляем led2
      digitalWrite(relay2, relay2State);
    }
    if (sw2 == 0 && (tempMdl < sld2)) { //если средняя темп ниже установленной
      relay2State = 0; //вЫключить реле 2
      led2 = relay2State; //отобразить статус светодиодом
      hub.sendUpdate("led2", String(led2)); // обновляем led2
      digitalWrite(relay2, relay2State);
    }
  } //end timer

  //-------обработчик ручного режима (принудительное вкл/выкл)---------
  if (sw1 && b1) { //ручной режим
    static GHtimer tmr3(300);
    if (tmr3.ready()) {
      relay1State = !relay1State;
      led1 = relay1State;
      hub.sendUpdate("led1", String(led1)); // обновляем led1 в момент переключения
      digitalWrite(relay1, relay1State);
    }
  }

  if (sw2 && b2) { //ручной режим
    static GHtimer tmr4(300);
    if (tmr4.ready()) {
      relay2State = !relay2State;
      led2 = relay2State;
      hub.sendUpdate("led2", String(led2)); // обновляем led2  в момент переключения
      digitalWrite(relay2, relay2State);
    }
  }

  //отправка температуры сразу по кнопке
  if (sendDataTemp == 1) { //команда ОТПРАВИТЬ
    //------пакет данных о температуре
    String t1 = String(temp1, 1); //
    String str1; //сообщение с датчика температуры N1
    str1.reserve(20); //резерв памяти для строки
    str1 += F("Кондиционер 1 = ");
    str1 += t1;
    str1 += F("С \n");

    String t2 = String(temp2, 1); //
    String str2; //сообщение с датчика температуры N2
    str2.reserve(20); //резерв памяти для строки
    str2 += F("Кондиционер 2 = ");
    str2 += t2;
    str2 += F("С \n");

    String t3 = String(tempMdl, 1); //
    String str3; // средняя температура
    str3.reserve(20); //резерв памяти для строки
    str3 += F("Средняя температура = ");
    str3 += t3;
    str3 += F("С \n");

    String str100; //общее сообщение о температурах (N1,N2,средняя)
    str100.reserve(100);
    str100 += str1;
    str100 += str2;
    str100 += str3;

    bot.sendMessage(str100);
    sendDataTemp = 0;
  }

  //-------отправка сообщений статуса---------
  if (relay1State != lastRelay1State) { //сообщения о переключениях реле
    if (relay1State == HIGH && alarmEnable == 1)  bot.sendMessage("Контактор 1 включен!");
    if (relay1State == LOW && alarmEnable == 1)   bot.sendMessage("Контактор 1 вЫключен!");
    digitalWrite(buzz, 1);
    delay(500);
    digitalWrite(buzz, 0);
  }

  if (relay2State != lastRelay2State) { //сообщения о переключениях реле
    if (relay2State == HIGH && alarmEnable == 1)  bot.sendMessage("Контактор 2 включен!");
    if (relay2State == LOW && alarmEnable == 1)   bot.sendMessage("Контактор 2 вЫключен!");
    digitalWrite(buzz, 1);
    delay(500);
    digitalWrite(buzz, 0);
  }

  lastRelay1State = relay1State;
  lastRelay2State = relay2State;

} //end loop

void newMsg(FB_msg & msg) { //менюшка в телеграмм-боте

  //  bot.showMenu("Уведомления ВКЛ \t Уведомления ОТКЛ \n Запрос температуры");

  if (msg.text == "Уведомления ВКЛ") {  // включить все уведомления
    alarmEnable = 1; //
    bot.sendMessage("Уведомления автоматики включены!");
  }

  if (msg.text == "Уведомления ОТКЛ") {  // вЫключить уведомления
    alarmEnable = 0;
    bot.sendMessage("Уведомления автоматики отключены!");
  }

  if (msg.text == "Запрос температуры") {
    sendDataTemp = 1;
    bot.sendMessage("Температура:");
  }
}


void connectWiFi() {              //функция подключения к сети wifi с мигалками//в setup
  WiFi.begin(lp.ssid, lp.pass);                //подключаемся
  prev_connect = millis();                         //записываем время когда была попытка подключиться
  while (WiFi.status() != WL_CONNECTED) {          //пока нет подключения к сети wifi...
    for (unsigned short x = 0; x <= 2; x++) {      //цикл для обозначения встроенным светодиодом о том, что нет соединения с wifi сетью (быстрое мерцание) unsigned short в arduino ide 0...255
      digitalWrite(2, HIGH);    //включаем встроенный светодиод
      delay(250);               //ждем 0,25сек
      digitalWrite(2, LOW);     //выключаем встроенный светодиод
      delay(250);               //ждем 0,25сек
    }
    if ((millis() - prev_connect) > 3000) return;  //ждем 3 сек и выходим из этой подпрограммы в общий цикл loop
  }
}

void stat_wifi() {                //Проверка состояния подключения к сети wifi //в loop
  if (WiFi.status() != WL_CONNECTED) {    //если нет подключения к сети wifi...
    digitalWrite(2, LOW);                //включаем встроенный светодиод
    delay(100);                            //ждем 0,05сек
    digitalWrite(2, HIGH);                 //выключаем встроенный светодиод
    delay(100);                            //ждем 0,05сек
    time_notconnected = millis();         //записываем последнее время при котором не было подключения к сети wifi для таймера переподключения
  }
  if (WiFi.status() == WL_CONNECTED) {
    time_connected = millis();   //если плата подключена к сети wifi, записываем текущее время в переменную time_connected
    digitalWrite(LED_BUILTIN, LOW); //индикатор вкл
  }

  if (WiFi.status() != WL_CONNECTED && ((time_notconnected - time_connected) >= 60000) && (millis() - prev_connect) > 15000) {
    connectWiFi(); //если нет подключения более 60 сек пытаемся переподключиться
  }

}
