#include <avr/sleep.h>
#include <LiquidCrystal.h>
const char* wday[] = {"SO", "MO", "DI", "MI", "DO", "FR", "SA"};
#define BUTTON1 2
#define BUTTON2 3
#define SPEAKER 4
#define BACKLIGHT 6
#define LEAPYEAR(year)  (!((year) % 4) && (((year) % 100) || !((year) % 400)))
volatile uint32_t timetens = 0;
volatile uint32_t timeaction = 0;

char hr;
char min;
char sec;
int year;
int month;
int day;
char wda;

uint32_t backlight = 0;

uint32_t time = 1000;

enum buttonaction_t {
  BNONE = 0,
  BSHORT = 1,
  BLONG = 2
};

volatile uint32_t buttonstart1 = 0;
volatile buttonaction_t buttonaction1 = BNONE;

volatile uint32_t buttonstart2 = 0;
volatile buttonaction_t buttonaction2 = BNONE;

enum state_t {
  TIME,
  SETALARM,
  WEATHER,
  NEWS,
  ALARM
} state = TIME;

char alarmdigit = 0;
char alarmtime[4];
boolean alarm = false;
int snooze = 0;

struct weather_t {
  char day;
  char code;
  char min;
  char max;
} weather[4];

char news[2][200];

char weatherpage = 0;
char newspage = 0;
int newspos = 0;
LiquidCrystal lcd (7, 8, 9, 10, 11, 12, 13);

ISR (TIMER1_COMPA_vect)
{
  timetens++;
  timeaction = 1;
  if (!(timetens % 10))time++;
}
void printPGM(const char* ptr) {
  char res;
  while (res = pgm_read_byte(ptr++))
    Serial.print(res);
}
void lcdPGM(const char* ptr) {
  char res;
  while (res = pgm_read_byte(ptr++))
    lcd.write(res);
}
void inline initTimer1() {
  TCCR1A = 0;
  TCCR1B = (1 << WGM12) | (1 << CS12);
  TCNT1 = 0;
  OCR1A = 6249;
  TIMSK1 |= (1 << OCIE1A);
  sei();
}
void initESP() {
  Serial.begin(38400);
  //while(!Serial.available());
  //Serial.find("com]");
  delay(200);
  printPGM(PSTR("AT+RST\r\n"));
  delay(1900);
  while (!Serial.available());
  printPGM(PSTR("AT+CWMODE=1\r\n"));
  while (!Serial.available());
  Serial.find("OK");
  //Serial.write("ATE0\r\n");
  //while(!Serial.available());
  //Serial.find("OK");
  delay(30);
  printPGM(PSTR("AT+CWJAP=\"SSID\",\"PASSWORD\"\r\n"));
  while (!Serial.available());
  Serial.find("busy p...");
  while (!Serial.available());
  Serial.find("OK");
  delay(20);
  printPGM(PSTR("AT+CIPMUX=0\r\n"));
  while (!Serial.available());
  Serial.find("OK");
  delay(20);
}
void connClose() {
  while (!Serial.available());
  Serial.find("OK");
  delay(100);
  printPGM(PSTR("AT+CIPCLOSE\r\n"));
  while (!Serial.available());
  Serial.find("OK");
  delay(700);
}
void getTime() {
  unsigned char ntpbuf[49];
  memset(ntpbuf, 0, 49);
  ntpbuf[0] = 0xE3;
  ntpbuf[2] = 4;
  ntpbuf[3] = 0xFA;
  printPGM(PSTR("AT+CIPSTART=\"UDP\",\"192.53.103.108\",123\r\n"));
  while (!Serial.available());
  Serial.find("OK");
  delay(200);
  printPGM(PSTR("AT+CIPSEND=48\r\n"));
  while (!Serial.available());
  Serial.find(">");
  delay(60);
  for (uint8_t i = 0; i < 48; i++) {
    Serial.write(ntpbuf[i]);
  }
  Serial.write("\r\n");
  while (!Serial.available());
  Serial.find("+IPD,48:");
  Serial.readBytes(ntpbuf, 48);
  uint32_t num = *(uint32_t*)(ntpbuf + 40);
  uint32_t timest  = ((num >> 24) & 0xff) | ((num << 8) & 0xff0000) | ((num >> 8) & 0xff00) | ((num << 24) & 0xff000000);
  timetens = ntpbuf[44] / 25;
  timest -= 2208988800;
  timest += 7200;
  delay(400);
  connClose();
  time = timest;
}
void httpReq(const char * host, const char * head) {
  printPGM(PSTR("AT+CIPSTART=\"TCP\",\""));
  printPGM(host);
  printPGM(PSTR("\",80\r\n"));
  while (!Serial.available());
  Serial.find("OK");
  while (!Serial.available());
  Serial.find("Linked");
  delay(50);
  printPGM(PSTR("AT+CIPSEND="));
  Serial.print(strlen_P(head), DEC);
  Serial.write("\r\n");
  while (!Serial.available());
  Serial.find(">");
  delay(60);
  printPGM(head);
  while (!Serial.available());
  Serial.find("+IPD,");
}
void getWeather() {
  getDate();
  httpReq(PSTR("query.yahooapis.com"), PSTR("GET /v1/public/yql?q=select%20item.forecast%20from%20weather.forecast%20where%20woeid%3D698064%20and%20u%3D%27c%27&format=json HTTP/1.1\r\nHost: query.yahooapis.com\r\n\r\n"));
  while (!Serial.available());
  for (char i = 0; i < 4; i++) {
    Serial.find("forecast");
    while (!Serial.available());
    Serial.find("\"code\":\"");
    weather[i].code =  Serial.parseInt();
    Serial.find("\"high\":\"");
    weather[i].max =  Serial.parseInt();
    Serial.find("\"low\":\"");
    weather[i].min =  Serial.parseInt();
    weather[i].day = (i + wda) % 7;
  }
  connClose();
}

void getNews() {

  httpReq(PSTR("www.spiegel.de"), PSTR("GET /schlagzeilen/tops/index.rss HTTP/1.1\r\nHost: www.spiegel.de\r\n\r\n"));
  while (!Serial.available());
  for (char i = 0; i < 2; i++) {
    Serial.find("<item>");
    while (!Serial.available());
    Serial.find("<title>");
    char tmp;
    int n = 0;
    while (!Serial.available());
    while ((tmp = Serial.read()) != '<' && n < 150) {
      if (tmp != 0xFF)news[i][n++] = tmp;
      while (!Serial.available());
    };
    Serial.find("<description>");
    news[i][n++] = ' ';
    news[i][n++] = '-';
    news[i][n++] = ' ';
    while (!Serial.available());
    while ((tmp = Serial.read()) != '<' && n < 197) {
      if (tmp != 0xFF)news[i][n++] = tmp;
      while (!Serial.available());
    };
    news[i][n] = '\0';
    for (int g = 0; g < 248; g++) {
      if (news[i][g] == (uint8_t)0xc3) {
        switch (news[i][g + 1]) {
          case (uint8_t)0x9f:
            news[i][g] = 's';
            news[i][++g] = 's';
            continue;
          case (uint8_t)0xbc:
            news[i][g] = 'u';
            break;
          case (uint8_t)0x9c:
            news[i][g] = 'U';
            break;
          case (uint8_t)0xa4:
            news[i][g] = 'a';
            break;
          case (uint8_t)0x84:
            news[i][g] = 'A';
            break;
          case (uint8_t)0xb6:
            news[i][g] = 'o';
            break;
          case (uint8_t)0x96:
            news[i][g] = 'O';
            break;
        }
        news[i][++g] = 'e';
      }

    }
  }

  connClose();
}
void buttonchange1() {
  if (buttonaction1)return;
  if (digitalRead(BUTTON1)) {
    if (buttonstart1) {
      if ((buttonstart1 + 11) < timetens) {
        buttonaction1 = BLONG;
      } else if ((buttonstart1) < timetens) {
        buttonaction1 = BSHORT;
      }
      buttonstart1 = 0;
    }
  } else {
    if (!buttonstart1)buttonstart1 = timetens;
  }
}
void buttonchange2() {
  if (buttonaction2)return;
  if (digitalRead(BUTTON2)) {
    if (buttonstart2) {
      if ((buttonstart2 + 11) < timetens) {
        buttonaction2 = BLONG;
      } else if ((buttonstart2) < timetens) {
        buttonaction2 = BSHORT;
      }
      buttonstart2 = 0;
    }
  } else {
    if (!buttonstart2)buttonstart2 = timetens;
  }
}
void displayTime() {
  lcd.clear();
  if (hr < 10)lcd.print('0');
  lcd.print(int(hr));
  lcd.print(':');
  if (min < 10)lcd.print('0');
  lcd.print(int(min));
  lcd.print(':');
  if (sec < 10)lcd.print('0');
  lcd.print(int(sec));
  lcd.setCursor(13, 1);
  if (snooze)lcd.print("SN");
  else lcd.print(alarm ? "ON" : "OFF");
  lcd.setCursor(0, 2);
  if (day < 10)lcd.print('0');
  lcd.print(int(day));
  lcd.print('.');
  if (month < 10)lcd.print('0');
  lcd.print(int(month));
  lcd.print('.');
  if (year < 10)lcd.print('0');
  lcd.print(int(year));
  lcd.print(wday[wda]);


}
// http://www.jbox.dk/sanos/source/lib/time.c.html
void getDate() {
  const uint8_t _ytab[2][12] = {
    {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31},
    {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31}
  };
  uint32_t tmp = time;
  sec = tmp % 60;
  tmp /= 60;
  min = tmp % 60;
  tmp /= 60;
  hr = tmp % 24;
  if (month && (min != 0 && sec > 3))return;
  int year0 = 1970;
  uint32_t dayno = (uint32_t)tmp / 24;
  wda = (dayno + 4) % 7;
  while (dayno >= (uint32_t)(LEAPYEAR(year0) ? 366 : 365)) {
    dayno -= (LEAPYEAR(year0) ? 366 : 365);
    year0++;
  }
  year = year0 - 2000;
  uint32_t tm_yday = dayno;
  month = 0;
  while (dayno >= (uint32_t)_ytab[LEAPYEAR(year0)][month]) {
    dayno -= _ytab[LEAPYEAR(year0)][month];
    month++;
  }
  day = dayno + 1;
  month += 1;
}
void displaySetAlarm() {
  lcd.clear();
  (timetens % 10 || alarmdigit != 0) ? lcd.print(int(alarmtime[0])) : lcd.print(' ');
  (timetens % 10 || alarmdigit != 1) ? lcd.print(int(alarmtime[1])) : lcd.print(' ');
  lcd.print(':');
  (timetens % 10 || alarmdigit != 2) ? lcd.print(int(alarmtime[2])) : lcd.print(' ');
  (timetens % 10 || alarmdigit != 3) ? lcd.print(int(alarmtime[3])) : lcd.print(' ');
  lcd.setCursor(0, 2);
  lcdPGM(PSTR("SetAlarm"));
}

void displayWeather() {
  const char * wstr;
  //https://developer.yahoo.com/weather/documentation.html
  switch (weather[weatherpage].code) {
    case 0:
    case 1:
    case 2:
    case 3:
    case 4:
    case 23:
      wstr = PSTR("Sturm");
      break;
    case 5:
    case 6:
    case 7:
      wstr = PSTR("Schneeregen");
      break;
    case 8:
    case 9:
      wstr = PSTR("Nieselregen");
      break;
    case 10:
    case 11:
    case 12:
      wstr = PSTR("Regen");
      break;
    case 13:
    case 14:
    case 15:
    case 16:
      wstr = PSTR("Schnee");
      break;
    case 17:
    case 35:
      wstr = PSTR("Hagel");
      break;
    case 18:
    case 19:
    case 20:
    case 21:
    case 22:
      wstr = PSTR("Nebel");
      break;
    case 24:
    case 25:
      wstr = PSTR("Windig");
      break;
    case 27:
    case 28:
    case 29:
    case 30:
      wstr = PSTR("Bewoelkt");
      break;
    case 31:
    case 33:
    case 34:
      wstr = PSTR("Klar");
      break;
    case 32:
      wstr = PSTR("Sonnig");
      break;
    case 38:
    case 39:
      wstr = PSTR("v. Sturm");
      break;
    default:
      wstr = PSTR("Unbekannt");
      break;

  }
  lcd.clear();
  lcd.print(wday[weather[weatherpage].day]);
  lcd.print("  ");
  lcdPGM(wstr);
  lcd.setCursor(0, 2);
  lcdPGM(PSTR("T:"));
  lcd.print(int(weather[weatherpage].min));
  lcdPGM(PSTR("\xDF""C  H:"));
  lcd.print(int(weather[weatherpage].max));
  lcdPGM(PSTR("\xDF""C"));
}
void displayNews() {
  lcd.clear();
  int pos = newspos;
  if (!news[newspage][newspos])newspos = 0;
  while (news[newspage][pos] != '\0' && (pos < (newspos + 17)))lcd.write(news[newspage][pos++]);

}
void displayAlarm() {
  lcd.clear();
  lcdPGM(PSTR("Alarm"));
}
void toneAlarm() {
  if ((timetens / 10) % 2) noTone(SPEAKER);
  else if (timetens % 2)tone(SPEAKER, 1100);
  else noTone(SPEAKER);

}
void inline sleep() {
  char timer = TCCR0B;
  char adc = ADCSRA;
  ADCSRA = 0;
  TCCR0B = 0;
  sleep_enable();
  sleep_bod_disable();
  sei();
  sleep_cpu();
  sleep_disable();
  TCCR0B = timer;
  ADCSRA = adc;
}
void load() {
  lcd.clear();
  lcdPGM(PSTR("Lade"));
  initESP();
  getTime();
  getWeather();
  delay(1000);
  getNews();
}
void setup() {
  set_sleep_mode(SLEEP_MODE_IDLE);
  pinMode(BACKLIGHT, HIGH);
  digitalWrite(BACKLIGHT, HIGH);
  lcd.begin(16, 2);
  alarmtime[0] = eeprom_read_byte((uint8_t*)0);
  alarmtime[1] = eeprom_read_byte((uint8_t*)1);
  alarmtime[2] = eeprom_read_byte((uint8_t*)2);
  alarmtime[3] = eeprom_read_byte((uint8_t*)3);
  initTimer1();
  load();
  pinMode(BUTTON1, INPUT_PULLUP);
  pinMode(BUTTON2, INPUT_PULLUP);
  pinMode(SPEAKER, OUTPUT);
  displayTime();
  attachInterrupt(digitalPinToInterrupt(BUTTON1), buttonchange1, CHANGE);
  attachInterrupt(digitalPinToInterrupt(BUTTON2), buttonchange2, CHANGE);

}

void loop() {

  static state_t back;
  while ((!timeaction) && (!buttonaction1) && (!buttonaction2))sleep();
  if (!(timetens % 10) && timeaction)getDate();
  if (buttonaction1 || buttonaction2) {
    if (!backlight) {
      digitalWrite(BACKLIGHT, HIGH);
      buttonaction1 = BNONE;
      buttonaction2 = BNONE;
      backlight = timetens;
      return;
    }
    backlight = timetens;
  }
  if ((backlight + 200) < timetens) {
    digitalWrite(BACKLIGHT, LOW);
    backlight = 0;
  }
  if (state != SETALARM && state != ALARM && alarm) {
    if (hr == ((alarmtime[0] * 10 + alarmtime[1] + (snooze / 60)) % 24) && min == ((alarmtime[2] * 10 + alarmtime[3] + snooze) % 60) && sec < 2) {
      back = state;
      state = ALARM;
      displayAlarm();
    }
  }
  if (state == TIME && sec < 2) {
    int8_t mina = (alarmtime[2] * 10 + alarmtime[3]) - 2;
    int8_t hra = (alarmtime[0] * 10 + alarmtime[1]);
    if (mina < 0) {
      hra--;
      mina = 58;
    }
    if (hra == -1)hra = 23;
    if ((hra == hr && min == mina && alarm) || ((!(hr % 6)) && (!min)))load();
  }
  switch (state) {
    case TIME:
      if (buttonaction1 == BLONG) {
        state = SETALARM;
        displaySetAlarm();
      } else if (buttonaction2 == BSHORT) {
        state = WEATHER;
        displayWeather();
      } else if (timeaction) {
        if (!(timetens % 10))displayTime();
      } else if (buttonaction2 == BLONG) {
        if (!snooze) {
          alarm = !alarm;
        }
        snooze = 0;
        displayTime();
      }
      break;
    case SETALARM:
      if (buttonaction1 == BLONG) {
        eeprom_update_byte((uint8_t*)0, alarmtime[0]);
        eeprom_update_byte((uint8_t*)1, alarmtime[1]);
        eeprom_update_byte((uint8_t*)2, alarmtime[2]);
        eeprom_update_byte((uint8_t*)3, alarmtime[3]);
        state = TIME;
        displayTime();
      } else if (buttonaction2 == BSHORT) {

        alarmtime[alarmdigit]++;
        if (alarmdigit == 0)alarmtime[0] %= 3;
        else if (alarmdigit == 2)alarmtime[2] %= 6;
        else if (alarmdigit == 1 && alarmtime[0] == 2)alarmtime[1] %= 4;
        alarmtime[alarmdigit] %= 10;
        //TODO overflow handling
        displaySetAlarm();

      } else if (buttonaction1 == BSHORT) {
        alarmdigit++;
        alarmdigit %= 4;
        displaySetAlarm();
      } else if (timeaction) {
        if (!(timetens % 5))displaySetAlarm();
      }
      break;
    case WEATHER:
      if (buttonaction2 == BSHORT) {
        state = NEWS;
        displayNews();
        weatherpage = 0;
      } else if (buttonaction1 == BSHORT) {
        weatherpage++;
        weatherpage %= 4;
        displayWeather();
      }
      break;
    case NEWS:
      if (buttonaction2 == BSHORT) {
        state = TIME;
        newspage = 0;
        newspos = 0;
        displayTime();
      } else if (buttonaction1 == BSHORT) {
        newspage++;
        newspage %= 2;
        newspos = 0;
        displayNews();
      } else if (timeaction) {
        newspos++;
        if (!(timetens % 4))displayNews();
      }
      break;
    case ALARM:
      if (timeaction) {
        if (!(timetens % 10))displayAlarm();
        digitalWrite(BACKLIGHT, (timetens / 5) % 2);
        toneAlarm();
        backlight = timetens;
        break;
      }
      if (buttonaction2 == BSHORT) {
        snooze = 0;
      } else if (buttonaction1 == BSHORT) {
        snooze += 5;
      }
      state = back;
      digitalWrite(BACKLIGHT, HIGH);
      noTone(SPEAKER);
      switch (state) {
        case TIME:
          displayTime();
          break;
        case NEWS:
          displayNews();
          break;
        case WEATHER:
          displayWeather();
          break;
      }
      break;
  }
  if (buttonaction1) buttonaction1 = BNONE;
  if (buttonaction2) buttonaction2 = BNONE;
  if (timeaction) timeaction = 0;
}
