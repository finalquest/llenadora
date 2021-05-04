#include "EncoderStepCounter.h"
#include "U8glib.h"
#include <arduino-timer.h>
#include <EEPROM.h>

U8GLIB_ST7920_128X64_1X u8g(50, 52, 53); // SPI Com: SCK = en = 18, MOSI = rw = 16, CS = di = 17

#define KEY_NONE 0
#define KEY_PREV 1
#define KEY_NEXT 2
#define KEY_SELECT 3
#define KEY_BACK 4
#define ENCODER_BUTTON 48
#define START_AUTOMATION 37
#define ALL_STOP 36
#define WASH 35

#define WASH_ACTION 24

#define ENCODER_PIN1 51
#define ENCODER_PIN2 49

#define MENU_ITEMS 2
#define CONFIG_ITEMS 2
#define ITEMS_PER_PAGE 6
#define INIT_PAGE 0

struct MENU
{
  const char **menu;
  uint16_t numElements;
};

int tiempos[3] = {10, 10, 10};

const char *menu_strings[2] = {"Empezar", "Configuracion"};
const char *begin_menu[4] = {"A", "Automatico", "Lavado", "Enjuague"};
const char *config_menu_data[4] = {"A", "T. Lavado", "T. Enjuague", "T. Espera"};

MENU pinMatrix[] =
{
  {menu_strings, 2},
  {begin_menu, 4},
  {config_menu_data, 3},
};

uint8_t menu_current = 0;
uint8_t current_page = INIT_PAGE;
uint8_t last_key_code = KEY_NONE;
uint8_t lastpos = 0;
uint8_t current_enter_menu = 0;
long buttonDownTime = 0;
bool runNext = false;
bool menu_redraw_required = true;
bool menu_edit = false;
bool menu_button_val = false;
bool menu_button_last = false;
bool start_automation = false;
bool start_automation_last = false;
bool wash_button = false;
bool wash_button_last = false;
bool all_stop = false;
bool all_stop_last = false;
bool avoid_repeat = false;
auto timer = timer_create_default(); // create a timer with default settings
bool isRunning = false;
bool runningWash = false;
bool runningClean = false;
bool waiting = false;
int washTime;
int cleanTime;
int waitingTime;

EncoderStepCounter encoder(ENCODER_PIN1, ENCODER_PIN2);

void draw(void)
{
  uint8_t i, h;
  u8g_uint_t w, d;

  u8g.setFont(u8g_font_6x13);
  u8g.setFontRefHeightText();
  u8g.setFontPosTop();
  current_page = INIT_PAGE + (menu_current / ITEMS_PER_PAGE);
  h = u8g.getFontAscent() - u8g.getFontDescent();
  w = u8g.getWidth();
  if (!isRunning)
  {
    for (i = 0; i < pinMatrix[current_enter_menu].numElements; i++)
    {
      // u8g.setScale2x2();
      d = (w - u8g.getStrWidth(pinMatrix[current_enter_menu].menu[i + (ITEMS_PER_PAGE * current_page)])) / 2;
      u8g.setDefaultForegroundColor();
      if (i == menu_current - (ITEMS_PER_PAGE * current_page))
        if (menu_edit)
        {
          u8g.setDefaultForegroundColor();
          u8g.drawBox(0, i * h + 1, w, h);
          u8g.setDefaultBackgroundColor();
        }
        else
        {
          u8g.drawFrame(0, i * h + 1, w, h);
        }
      if (current_enter_menu == 2 && i > 0)
      {
        char str[50];
        sprintf(str, "%s %d", pinMatrix[current_enter_menu].menu[i + (ITEMS_PER_PAGE * current_page)], tiempos[i - 1]);
        u8g.drawStr(0, i * h, str);
      }
      else
      {
        u8g.drawStr(0, i * h, pinMatrix[current_enter_menu].menu[i + (ITEMS_PER_PAGE * current_page)]);
      }
    }
  }
  else
  {
    char str[50];
    if (runningWash)
    {
      sprintf(str, "%s %d %s", "Lavando: ", washTime, "segundos");
    }
    else if (runningClean)
    {
      Serial.println("E");
      sprintf(str, "%s %d %s", "Enjuagando: ", cleanTime, "segundos");
    }
    else if (waiting)
    {
      Serial.println("E");
      sprintf(str, "%s %d %s", "Esperando: ", waitingTime, "segundos");
    }
    u8g.drawStr(0, i * h, str);
  }
}

void calculate_pos(uint8_t pos)
{
  if (pos > lastpos + 2)
  {
    lastpos = pos;
    menu_current++;
    menu_redraw_required = true;
    return;
  }
  if (pos < lastpos - 2)
  {
    lastpos = pos;
    if (menu_current >= 0)
    {
      menu_current--;
      menu_redraw_required = true;
    }
    return;
  }
}

bool runWash(void *)
{
  runningWash = true;
  runningClean = false;
  waiting = false;
  washTime--;
  if (washTime <= 0)
  {
    timer.cancel();
    isRunning = false;
    digitalWrite(WASH_ACTION, HIGH);
    if(runNext) {
      startWaiting();
    }
  }
  menu_redraw_required = true;
  return true;
}

bool runWaiting(void *)
{
  runningClean = false;
  runningWash = false;
  waiting = true;
  waitingTime--;
  if (waitingTime <= 0)
  {
    timer.cancel();
    isRunning = false;
    startClean();
  }
  menu_redraw_required = true;
  return true;
}

bool runClean(void *)
{
  runningClean = true;
  runningWash = false;
  waiting = false;
  cleanTime--;
  if (cleanTime <= 0)
  {
    timer.cancel();
    isRunning = false;
    runNext = false;
  }
  menu_redraw_required = true;
  return true;
}

void runAutomation() {
  runNext = true;
  startWash();
}

void allStop() {
  Serial.println("S");
  timer.cancel();
  isRunning = false;
  runNext = false;
  menu_redraw_required = true;
  digitalWrite(WASH_ACTION, HIGH);
}

void startWash() {
  Serial.println("W");
  isRunning = true;
  washTime = tiempos[0];
  cleanTime = 0;
  waitingTime = 0;
  timer.every(1000, runWash);
  digitalWrite(WASH_ACTION, LOW);
}

void startClean() {
  Serial.println("C");
  isRunning = true;
  washTime = 0;
  cleanTime = tiempos[1];
  waitingTime = 0;
  timer.every(1000, runClean);
}

void startWaiting() {
  Serial.println("W");
  isRunning = true;
  washTime = 0;
  cleanTime = 0;
  waitingTime = tiempos[2];
  timer.every(1000, runWaiting);
}

void enterMenu()
{
  //puedo entrar mas adentro del menu?
  int last_entered = current_enter_menu;
  if (menu_current == 0 || current_enter_menu == 0)
  {
    current_enter_menu = current_enter_menu == 0 ? menu_current + 1 : menu_current;
    menu_current = 0;
    menu_redraw_required = true;
  }
  else if (current_enter_menu == 1 && menu_current == 1)
  {
    if (isRunning)
    {
      allStop();
    }
    else
    {
      runAutomation();
    }
  }
  else if (current_enter_menu == 1 && menu_current == 2)
  {
    if (isRunning)
    {
      allStop();
    }
    else
    {
      startWash();
    }
  }
  else if (current_enter_menu == 1 && menu_current == 3)
  {
    if (isRunning)
    {
      allStop();
    }
    else
    {
      startClean();
    }
  }
}

void setup(void)
{
  encoder.begin();
  Serial.begin(9600);
  digitalWrite(WASH_ACTION, HIGH);
  tiempos[0] = EEPROM.read(0);
  tiempos[1] = EEPROM.read(1);
  tiempos[2] = EEPROM.read(2);
  
  Serial.println(washTime);
  
  u8g.setFont(u8g_font_unifont);

  if (u8g.getMode() == U8G_MODE_R3G3B2)
  {
    u8g.setColorIndex(255); // white
  }
  else if (u8g.getMode() == U8G_MODE_GRAY2BIT)
  {
    u8g.setColorIndex(3); // max intensity
  }
  else if (u8g.getMode() == U8G_MODE_BW)
  {
    u8g.setColorIndex(1); // pixel on
  }
  else if (u8g.getMode() == U8G_MODE_HICOLOR)
  {
    u8g.setHiColorByRGB(255, 255, 255);
  }

  pinMode(13, OUTPUT);
  pinMode(ENCODER_BUTTON, INPUT_PULLUP);
  pinMode(START_AUTOMATION, INPUT_PULLUP);
  pinMode(ALL_STOP, INPUT_PULLUP);
  pinMode(WASH, INPUT_PULLUP);
  pinMode(WASH_ACTION, OUTPUT);
}

void loop(void)
{
  timer.tick(); // tick the timer
  menu_button_val = digitalRead(ENCODER_BUTTON);
  start_automation = digitalRead(START_AUTOMATION);
  all_stop = digitalRead(ALL_STOP);
  wash_button = digitalRead(WASH);

  digitalWrite(13, menu_button_val);

  if (wash_button == LOW && wash_button_last == HIGH) {
    allStop();
    startWash();
  }
  if (all_stop == LOW && all_stop_last == HIGH) {
    allStop();
  }
  if (start_automation == LOW && start_automation_last == HIGH) {
    allStop();
    runAutomation();
  }
  if (menu_button_val == LOW && menu_button_last == HIGH)
  {
    buttonDownTime = millis();
    avoid_repeat = false;
    enterMenu();
  }

  if (menu_button_val == LOW && avoid_repeat == false && menu_current > 0 && (millis() - buttonDownTime) > long(2000))
  {
    menu_edit = !menu_edit;
    if(menu_edit == false) {
      EEPROM.update(0, tiempos[0]);
      EEPROM.update(1, tiempos[1]);
      EEPROM.update(2, tiempos[2]);
    }
    menu_redraw_required = true;
    avoid_repeat = true;
  }

  // picture loop
  if (menu_redraw_required)
  {
    u8g.firstPage();
    do
    {
      draw();
    } while (u8g.nextPage());
    menu_redraw_required = false;
  }

  encoder.tick();
  uint8_t pos = encoder.getPosition();
  if (!isRunning)
  {
    if (pos != lastpos && !menu_edit)
    {
      calculate_pos(pos);
    }
    else if (pos != lastpos && menu_edit)
    {
      tiempos[menu_current - 1] = pos;
      menu_redraw_required = true;
      lastpos = pos;
    }
  }

  menu_button_last = menu_button_val;
  start_automation_last = start_automation;
  all_stop_last = all_stop;
  wash_button_last = wash_button;
  // rebuild the picture after some delay
  //delay(50);
}
