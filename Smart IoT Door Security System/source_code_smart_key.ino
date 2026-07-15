#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <XPT2046_Touchscreen.h>
#include <EEPROM.h>
#include <MFRC522.h>
#include <WiFi.h>
#include <HTTPClient.h>

// ════════════════════════════════════════════
//  CẤU HÌNH
// ════════════════════════════════════════════
#define WIFI_SSID      "abc"
#define WIFI_PASSWORD  "khang2k5"
String firebaseURL = "https://testfirebase-65de7-default-rtdb.firebaseio.com/RELAY_KEY.json";

// ── Pin ──────────────────────────────────────
#define TFT_CS    5
#define TFT_RST   4
#define TFT_DC    2
#define TOUCH_CS  21
#define RELAY_PIN 33
#define RFID_RST  27
#define RFID_SS   13
#define MC38_PIN  25

// ── EEPROM ───────────────────────────────────
#define EEPROM_SIZE       128
#define EEPROM_PASS_ADDR  0
#define EEPROM_PASS_LEN   9
#define EEPROM_CARD_COUNT 10
#define EEPROM_CARD_BASE  11
#define CARD_UID_LEN      12
#define MAX_CARDS         5
#define PASS_MAX_LEN      8

// ── Calibrate cảm ứng ────────────────────────
#define TOUCH_X_MIN  440
#define TOUCH_X_MAX  3700
#define TOUCH_Y_MIN  570
#define TOUCH_Y_MAX  3500

// ── Relay ────────────────────────────────────
#define RELAY_ON          HIGH
#define RELAY_OFF         LOW
#define RELAY_DURATION_MS 10000

// ── Màu ──────────────────────────────────────
#define C_BG        0x0841
#define C_PANEL     0x10A2
#define C_WHITE     0xFFFF
#define C_BLACK     0x0000
#define C_ACCENT    0x07FF
#define C_GREEN     0x07E0
#define C_RED       0xF800
#define C_YELLOW    0xFFE0
#define C_ORANGE    0xFC60
#define C_GRAY      0x4228
#define C_DARK      0x2104
#define C_BORDER    0x2965
#define C_BTN_PASS  0x0338
#define C_BTN_RFID  0x0318
#define C_BTN_APP   0x3010
#define C_BTN_TC    0x2810
#define C_BTN_BACK  0x8200
#define C_KEY_NUM   0x1082
#define C_KEY_DEL   0x8000
#define C_KEY_CLR   0xA200
#define C_KEY_OK    0x0640
#define C_KEY_PRESS 0x07FF
#define C_BTN_ADD   0x0640
#define C_BTN_DEL   0x8800
#define C_CARD_BG   0x18C3
#define C_CARD_SEL  0x034B

// ── Màn hình ─────────────────────────────────
#define SCR_W 320
#define SCR_H 240

// ── Trang ────────────────────────────────────
#define PAGE_HOME         0
#define PAGE_PASSWORD     1
#define PAGE_APP          2
#define PAGE_TUYCHON      3
#define PAGE_CHANGE_PW    4
#define PAGE_RFID_CONFIRM 5
#define PAGE_RFID_MGMT    6
#define PAGE_RFID_ADD     7

// ── Bàn phím ─────────────────────────────────
#define KEY_COLS 4
#define KEY_ROWS 4
#define KEY_X    8
#define KEY_Y    80
#define KEY_W    70
#define KEY_H    36
#define KEY_GAP  4
#define KEY_R    4

const char keyLabels[KEY_ROWS][KEY_COLS][4] = {
  {"7","8","9","DEL"},
  {"4","5","6","CLR"},
  {"1","2","3","OK" },
  {"0","*","#","<<" }
};
const uint8_t keyType[KEY_ROWS][KEY_COLS] = {
  {0,0,0,1},{0,0,0,2},{0,0,0,3},{0,0,0,4}
};
uint16_t keyBg[] = {C_KEY_NUM, C_KEY_DEL, C_KEY_CLR, C_KEY_OK, C_BTN_BACK};

// ════════════════════════════════════════════
//  BIẾN TOÀN CỤC
// ════════════════════════════════════════════
uint8_t  currentPage  = PAGE_HOME;
String   inputStr     = "";
String   newPassStr   = "";
bool     holding      = false;
int      holdRow = -1, holdCol = -1;
String   msgText      = "";
uint16_t msgColor     = C_GREEN;
unsigned long msgTimer = 0;
#define  MSG_MS 1800

uint8_t  changePwStep = 0;
char     savedPassword[PASS_MAX_LEN+1] = "1234";

char    cardList[MAX_CARDS][CARD_UID_LEN];
uint8_t cardCount = 0;

bool          relayOpen  = false;
unsigned long relayTimer = 0;
String        openSource = "";

// ── MC38 ─────────────────────────────────────
bool doorHasOpened = false;
bool lastDoorState = LOW;

// ── Firebase ─────────────────────────────────
bool relay_firebase          = false;
bool wifiConnected           = false;
volatile bool needFirebaseReset = false;
volatile bool needRedrawStatus  = false;

// ── RFID ─────────────────────────────────────
String        lastRFIDUID  = "";
String        rfidUID      = "";
bool          rfidValid    = false;

String        rfidMsgText  = "";
uint16_t      rfidMsgColor = C_GREEN;
unsigned long rfidMsgTimer = 0;

int  selectedCard   = -1;
bool waitingNewCard = false;

// ── Timer thêm thẻ ───────────────────────────
unsigned long addCardTimer    = 0;
bool          pendingGoToMgmt = false;

// ── RFID scan timer ──────────────────────────
unsigned long lastRFIDScan = 0;

// ════════════════════════════════════════════
//  FIX CHẮNG MÀN HÌNH: cờ bảo vệ SPI bus
//  Khi scanRFID() đang dùng SPI để giao tiếp
//  với MFRC522, cờ này = true.
//  refreshRFIDMsgArea() sẽ chờ cờ = false
//  trước khi vẽ lên TFT, tránh hai thiết bị
//  tranh nhau SPI bus gây nhiễu màn hình.
// ════════════════════════════════════════════
volatile bool spiRFIDBusy = false;
Adafruit_ILI9341    tft     = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);
XPT2046_Touchscreen ts(TOUCH_CS);
MFRC522             mfrc522(RFID_SS, RFID_RST);

// ── Khai báo hàm ─────────────────────────────
void drawPage(uint8_t page);
void drawStatusFooter();
void drawBackBar();
void drawPassDisplay();
void drawChangePwDisplay();
void drawRFIDConfirmDisplay();
void drawAppStatus();
void drawRFIDMgmtPage();
void drawRFIDAddPage();
void drawKey(int row, int col, bool pressed);
void saveCards();
void loadCards();
void scanRFID();
void handleRFIDGlobalScan();
void handleRFIDAddScan();
void refreshRFIDMsgArea();

// ════════════════════════════════════════════
//  EEPROM: PASSWORD
// ════════════════════════════════════════════
void savePassword() {
  for(int i=0; i<=PASS_MAX_LEN; i++)
    EEPROM.write(EEPROM_PASS_ADDR+i, savedPassword[i]);
  EEPROM.commit();
}

void loadPassword() {
  char buf[PASS_MAX_LEN+1];
  for(int i=0; i<=PASS_MAX_LEN; i++)
    buf[i] = EEPROM.read(EEPROM_PASS_ADDR+i);
  buf[PASS_MAX_LEN] = '\0';
  bool valid = strlen(buf) >= 1;
  for(int i=0; i<(int)strlen(buf); i++)
    if(buf[i]<32 || buf[i]>126){ valid=false; break; }
  if(valid){ strcpy(savedPassword, buf); }
  else     { strcpy(savedPassword, "1234"); savePassword(); }
  Serial.printf("Password loaded: %s\n", savedPassword);
}
// ════════════════════════════════════════════
//  EEPROM: CARDS
// ════════════════════════════════════════════
void saveCards() {
  EEPROM.write(EEPROM_CARD_COUNT, cardCount);
  for(int i=0; i<MAX_CARDS; i++){
    int base = EEPROM_CARD_BASE + i*CARD_UID_LEN;
    for(int j=0; j<CARD_UID_LEN; j++)
      EEPROM.write(base+j, (i<cardCount) ? cardList[i][j] : 0);
  }
  EEPROM.commit();
}

void loadCards() {
  cardCount = EEPROM.read(EEPROM_CARD_COUNT);
  if(cardCount > MAX_CARDS) cardCount = 0;
  for(int i=0; i<cardCount; i++){
    int base = EEPROM_CARD_BASE + i*CARD_UID_LEN;
    for(int j=0; j<CARD_UID_LEN; j++)
      cardList[i][j] = EEPROM.read(base+j);
    cardList[i][CARD_UID_LEN-1] = '\0';
  }
  Serial.printf("Loaded %d cards\n", cardCount);
}

bool isCardAuthorized(const String& uid) {
  for(int i=0; i<cardCount; i++)
    if(uid == String(cardList[i])) return true;
  return false;
}

bool addCard(const String& uid) {
  if(cardCount >= MAX_CARDS) return false;
  for(int i=0; i<cardCount; i++)
    if(uid == String(cardList[i])) return false;
  uid.toCharArray(cardList[cardCount], CARD_UID_LEN);
  cardCount++;
  saveCards();
  return true;
}

bool deleteCard(int idx) {
  if(idx<0 || idx>=cardCount) return false;
  for(int i=idx; i<cardCount-1; i++)
    memcpy(cardList[i], cardList[i+1], CARD_UID_LEN);
  memset(cardList[cardCount-1], 0, CARD_UID_LEN);
  cardCount--;
  saveCards();
  return true;
}

// ════════════════════════════════════════════
//  RELAY + MC38
// ════════════════════════════════════════════
void openRelay(const char* source) {
  digitalWrite(RELAY_PIN, RELAY_ON);
  relayOpen     = true;
  relayTimer    = millis();
  openSource    = String(source);
  doorHasOpened = false;
  lastDoorState = LOW;
  Serial.printf(">>> RELAY ON [%s]\n", source);
}

void closeRelay() {
  digitalWrite(RELAY_PIN, RELAY_OFF);
  relayOpen     = false;
  openSource    = "";
  doorHasOpened = false;
  Serial.println(">>> RELAY OFF");
}
void resetFirebaseKey() {
  if(WiFi.status() != WL_CONNECTED) return;
  HTTPClient http;
  http.begin(firebaseURL);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(3000);
  int code = http.PUT("0");
  Serial.printf(">>> Firebase reset=0, code=%d\n", code);
  http.end();
  relay_firebase = false;
}
// ════════════════════════════════════════════
//  TASK: FIREBASE (Core 0)
// ════════════════════════════════════════════
void firebaseTask(void* p) {
  while(true) {
    if(WiFi.status() == WL_CONNECTED) {
      wifiConnected = true;
      HTTPClient http;
      http.begin(firebaseURL);
      http.setTimeout(2000);
      int code = http.GET();
      if(code == 200) {
        String payload = http.getString();
        payload.trim();
        bool fb_on = (payload=="1" || payload=="\"1\"");
        if(fb_on != relay_firebase) {
          relay_firebase = fb_on;
          if(fb_on && currentPage==PAGE_APP) {
            openRelay("APP");
            needRedrawStatus = true;
          }
        }
      }
      http.end();
    } else {
      wifiConnected = false;
    }
    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}
// ════════════════════════════════════════════
//  RFID SCAN — chạy NỀN trong loop()
//  FIX: dùng spiRFIDBusy để báo hiệu đang
//  dùng SPI, tránh xung đột với TFT khi
//  MC38 kích hoạt đóng cửa đúng lúc này.
// ════════════════════════════════════════════
// Thêm vào biến toàn cục
uint8_t rfidFailCount = 0;
void scanRFID() {
  if(!mfrc522.PICC_IsNewCardPresent()) {
    rfidFailCount++;
    if(rfidFailCount >= 10) {   // sau 10 lần thất bại (~3 giây) thì reset
      mfrc522.PCD_Init();
      rfidFailCount = 0;
      Serial.println("RFID: reset RC522");
    }
    return;
  }
  rfidFailCount = 0;  // có thẻ → reset counter
  if(!mfrc522.PICC_ReadCardSerial()) {
    mfrc522.PCD_Init();
    return;
  }
  String uid = "";
  for(byte i=0; i<mfrc522.uid.size; i++){
    if(mfrc522.uid.uidByte[i] < 0x10) uid += "0";
    uid += String(mfrc522.uid.uidByte[i], HEX);
    if(i < mfrc522.uid.size-1) uid += " ";
  }
  uid.toUpperCase();
  lastRFIDUID = uid;
  rfidUID     = uid;
  rfidValid   = isCardAuthorized(uid);
  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();
  Serial.printf("RFID: %s [%s]\n", uid.c_str(), rfidValid?"HOP LE":"SAI");

  if(currentPage == PAGE_RFID_ADD)
    handleRFIDAddScan();
  else
    handleRFIDGlobalScan();
}
// ════════════════════════════════════════════
//  XỬ LÝ RFID TOÀN CỤC (mở cửa)
// ════════════════════════════════════════════
void handleRFIDGlobalScan() {
  if(rfidValid) {
    openRelay("RFID");
    rfidMsgText  = "THE HOP LE - MO CUA!";
    rfidMsgColor = C_GREEN;
    needRedrawStatus = true;
  } else {
    rfidMsgText  = "THE KHONG HOP LE!";
    rfidMsgColor = C_RED;
  }
  rfidMsgTimer = millis();
  refreshRFIDMsgArea();
}

// ════════════════════════════════════════════
//  VẼ LẠI VÙNG THÔNG BÁO
//  FIX: chờ SPI rảnh trước khi vẽ TFT,
//  tránh chắng màn hình khi MC38 đóng cửa
//  đúng lúc RFID đang giao tiếp SPI.
// ════════════════════════════════════════════
void refreshRFIDMsgArea() {
  // BỎ while(spiRFIDBusy), chỉ vẽ thẳng
  switch(currentPage) {
    case PAGE_HOME:
      drawStatusFooter();
      break;
    case PAGE_PASSWORD:
    case PAGE_APP:
    case PAGE_CHANGE_PW:
    case PAGE_RFID_CONFIRM:
      drawBackBar();
      break;
    default:
      break;
  }
}
// ════════════════════════════════════════════
//  SETUP
// ════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  delay(300);

  EEPROM.begin(EEPROM_SIZE);
  loadPassword();
  loadCards();

  pinMode(RELAY_PIN, OUTPUT); digitalWrite(RELAY_PIN, RELAY_OFF);
  pinMode(MC38_PIN,  INPUT_PULLUP);
  pinMode(TFT_CS,    OUTPUT); digitalWrite(TFT_CS,   HIGH);
  pinMode(TOUCH_CS,  OUTPUT); digitalWrite(TOUCH_CS, HIGH);
  pinMode(RFID_SS,   OUTPUT); digitalWrite(RFID_SS,  HIGH);

  SPI.begin(18, 19, 23);
  tft.begin();
  tft.setRotation(1);
  ts.begin();
  ts.setRotation(3);
  mfrc522.PCD_Init();
  delay(50);

  byte ver = mfrc522.PCD_ReadRegister(mfrc522.VersionReg);
  Serial.printf("RFID version: 0x%02X %s\n", ver, (ver==0x91||ver==0x92)?"OK":"WARN");

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Dang ket noi WiFi");
  int retry = 0;
  while(WiFi.status() != WL_CONNECTED && retry < 20) {
    delay(500);
    Serial.print(".");
    retry++;
  }
  if(WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi OK! IP: " + WiFi.localIP().toString());
    wifiConnected = true;
  } else {
    Serial.println("\nWiFi THAT BAI!");
  }

  drawPage(PAGE_HOME);

  xTaskCreatePinnedToCore(firebaseTask, "Firebase", 8192, NULL, 1, NULL, 0);

  Serial.println("System ready");
}

// ════════════════════════════════════════════
//  LOOP
// ════════════════════════════════════════════
void loop() {

  // ── Chuyển trang sau khi thêm thẻ ────────────
  if(pendingGoToMgmt && millis()-addCardTimer > 2000) {
    pendingGoToMgmt = false;
    drawPage(PAGE_RFID_MGMT);
    return;
  }

  // ── Firebase reset ────────────────────────────
  if(needFirebaseReset){ needFirebaseReset=false; resetFirebaseKey(); }

  // ── MC38: theo dõi trạng thái cửa ────────────
  if(relayOpen) {
    bool doorNow = digitalRead(MC38_PIN);
    delay(10);
    bool doorNow2 = digitalRead(MC38_PIN);

    if(doorNow == doorNow2) {
      if(!doorHasOpened && lastDoorState==LOW && doorNow==HIGH) {
        doorHasOpened = true;
        Serial.println("MC38: Cua DA MO");
      }
      if(doorHasOpened && lastDoorState==HIGH && doorNow==LOW) {
        Serial.println("MC38: Cua DONG LAI -> KHOA NGAY");
        bool wasApp = (openSource=="APP" || relay_firebase);
        closeRelay();
        // FIX: delay nhỏ để RFID kịp nhả SPI trước khi vẽ TFT
        delay(30);
        needRedrawStatus = true;
        if(wasApp) needFirebaseReset = true;
      }
      lastDoorState = doorNow;
    }

    if(relayOpen && millis()-relayTimer > RELAY_DURATION_MS) {
      Serial.println(">>> Timeout 10s -> KHOA");
      bool wasApp = (openSource=="APP" || relay_firebase);
      closeRelay();
      // FIX: delay nhỏ để RFID kịp nhả SPI trước khi vẽ TFT
      delay(30);
      needRedrawStatus = true;
      if(wasApp) needFirebaseReset = true;
    }
  }

  // ── Cập nhật footer ───────────────────────────
  if(needRedrawStatus) {
    needRedrawStatus = false;
    refreshRFIDMsgArea();
    if(rfidMsgText.length()==0 && currentPage==PAGE_HOME)
      drawStatusFooter();
  }

  // ── RFID scan NỀN — luôn quét ở mọi trang ─────
  if(millis() - lastRFIDScan > 300) {
    lastRFIDScan = millis();
    if(!pendingGoToMgmt) {
      scanRFID();
    }
  }

  // ── Xóa thông báo RFID sau MSG_MS ─────────────
  if(rfidMsgText.length()>0 && millis()-rfidMsgTimer > MSG_MS) {
    rfidMsgText = "";
    refreshRFIDMsgArea();
  }

  // ── Xóa thông báo trang sau MSG_MS ────────────
  if(msgText.length()>0 && millis()-msgTimer > MSG_MS) {
    msgText = "";
    if(currentPage==PAGE_PASSWORD)     drawPassDisplay();
    if(currentPage==PAGE_APP)          drawAppStatus();
    if(currentPage==PAGE_CHANGE_PW)    drawChangePwDisplay();
    if(currentPage==PAGE_RFID_CONFIRM) drawRFIDConfirmDisplay();
    if(currentPage==PAGE_RFID_MGMT)    drawRFIDMgmtPage();
    if(currentPage==PAGE_RFID_ADD)     drawRFIDAddPage();
  }

  // ── Touch ─────────────────────────────────────
  if(!ts.touched()) {
    if(holding){
      if(currentPage==PAGE_PASSWORD ||
         currentPage==PAGE_CHANGE_PW ||
         currentPage==PAGE_RFID_CONFIRM)
        drawKey(holdRow, holdCol, false);
      holding=false; holdRow=holdCol=-1;
    }
    return;
  }

  TS_Point p = ts.getPoint();
  int sx = constrain(map(p.x, TOUCH_X_MIN, TOUCH_X_MAX, 0, SCR_W), 0, SCR_W-1);
  int sy = constrain(map(p.y, TOUCH_Y_MIN, TOUCH_Y_MAX, 0, SCR_H), 0, SCR_H-1);

  switch(currentPage) {
    case PAGE_HOME:         handleHomeTouch(sx,sy);      break;
    case PAGE_PASSWORD:     handleKbdTouch(sx,sy);       break;
    case PAGE_APP:          handleAppTouch(sx,sy);       break;
    case PAGE_TUYCHON:      handleTCTouch(sx,sy);        break;
    case PAGE_CHANGE_PW:    handleKbdTouch(sx,sy);       break;
    case PAGE_RFID_CONFIRM: handleKbdTouch(sx,sy);       break;
    case PAGE_RFID_MGMT:    handleRFIDMgmtTouch(sx,sy);  break;
    case PAGE_RFID_ADD:     handleRFIDAddTouch(sx,sy);   break;
  }
  delay(50);
}

// ════════════════════════════════════════════
//  ĐIỀU HƯỚNG
// ════════════════════════════════════════════
void drawPage(uint8_t page) {
  currentPage = page;
  inputStr=""; newPassStr=""; holding=false; msgText=""; rfidMsgText="";
  tft.fillScreen(C_BG);
  switch(page) {
    case PAGE_HOME:         drawHomePage();         break;
    case PAGE_PASSWORD:     drawPasswordPage();     break;
    case PAGE_APP:          drawAppPage();          break;
    case PAGE_TUYCHON:      drawTCPage();           break;
    case PAGE_CHANGE_PW:    changePwStep=0; drawChangePwPage(); break;
    case PAGE_RFID_CONFIRM: drawRFIDConfirmPage();  break;
    case PAGE_RFID_MGMT:    selectedCard=-1; drawRFIDMgmtPage(); break;
    case PAGE_RFID_ADD:     waitingNewCard=true; drawRFIDAddPage(); break;
  }
}

// ════════════════════════════════════════════
//  TRANG CHỦ
// ════════════════════════════════════════════
void drawHomePage() {
  drawHeader("CHON CACH MO CUA");
  drawBigBtn(0, "MAT KHAU",       C_BTN_PASS, C_WHITE);
  drawBigBtn(1, "APP (Firebase)", C_BTN_APP,  C_WHITE);
  drawBigBtn(2, "TUY CHINH",      C_BTN_TC,   C_WHITE);
  tft.setTextSize(1); tft.setTextColor(C_ACCENT);
  tft.setCursor(10, 218);
  tft.print("The RFID: dua the vao dau doc de mo cua bat cu luc nao");
  drawStatusFooter();
}

void drawBigBtn(int idx, const char* lbl, uint16_t bg, uint16_t fg) {
  int x=10, y=38+idx*58, w=300, h=50, r=6;
  tft.fillRoundRect(x,y,w,h,r,bg);
  tft.drawRoundRect(x,y,w,h,r,C_BORDER);
  tft.setTextColor(fg); tft.setTextSize(2);
  tft.setCursor(x+(w-strlen(lbl)*12)/2, y+(h-16)/2);
  tft.print(lbl);
}

void handleHomeTouch(int sx, int sy) {
  if      (sy>=38  && sy<=88 ) drawPage(PAGE_PASSWORD);
  else if (sy>=96  && sy<=146) drawPage(PAGE_APP);
  else if (sy>=154 && sy<=204) drawPage(PAGE_TUYCHON);
}

// ════════════════════════════════════════════
//  TRANG MẬT KHẨU
// ════════════════════════════════════════════
void drawPasswordPage() {
  drawHeader("NHAP MAT KHAU");
  drawPassDisplay();
  drawAllKeys();
  drawBackBar();
}

void drawPassDisplay() {
  tft.fillRoundRect(8,32,304,44,5,C_PANEL);
  tft.drawRoundRect(8,32,304,44,5,
    relayOpen && openSource=="PASSWORD" ? C_GREEN : C_ACCENT);
  tft.setTextSize(1); tft.setTextColor(C_GRAY);
  tft.setCursor(16,38); tft.print("MAT KHAU");
  if(msgText.length()>0) {
    tft.setTextColor(msgColor);
    tft.setCursor(8+(304-msgText.length()*6)/2, 50);
    tft.print(msgText);
  } else if(inputStr.length()==0) {
    tft.setTextColor(C_GRAY); tft.setCursor(16,50);
    tft.print("Nhap mat khau...");
  } else {
    String m=""; for(int i=0;i<(int)inputStr.length();i++) m+="*";
    tft.setTextSize(3); tft.setTextColor(C_YELLOW);
    int tx=8+304-m.length()*18-12; if(tx<16)tx=16;
    tft.setCursor(tx,38); tft.print(m);
  }
}

void handlePassKey(const char* k) {
  if     (strcmp(k,"DEL")==0){ if(inputStr.length()>0) inputStr.remove(inputStr.length()-1); }
  else if(strcmp(k,"CLR")==0){ inputStr=""; }
  else if(strcmp(k,"<<")==0) { drawPage(PAGE_HOME); return; }
  else if(strcmp(k,"OK" )==0){ checkPassword(); return; }
  else if(inputStr.length()<PASS_MAX_LEN) inputStr+=k;
  drawPassDisplay();
}

void checkPassword() {
  bool ok = (inputStr.length()==strlen(savedPassword));
  if(ok) for(int i=0;i<(int)inputStr.length();i++)
    if(inputStr[i]!=savedPassword[i]){ ok=false; break; }
  if(ok){ openRelay("PASSWORD"); msgText="MO CUA THANH CONG!"; msgColor=C_GREEN; }
  else  { msgText="SAI MAT KHAU!"; msgColor=C_RED; }
  msgTimer=millis(); inputStr="";
  drawPassDisplay(); drawBackBar();
}

// ════════════════════════════════════════════
//  TRANG APP FIREBASE
// ════════════════════════════════════════════
void drawAppPage() {
  drawHeader("DIEU KHIEN QUA APP");
  drawAppStatus(); drawBackBar();
}

void drawAppStatus() {
  tft.fillRoundRect(8,32,304,150,8,C_PANEL);
  tft.drawRoundRect(8,32,304,150,8, wifiConnected?C_ACCENT:C_GRAY);
  tft.setTextSize(1);
  tft.setTextColor(wifiConnected?C_GREEN:C_RED);
  tft.setCursor(16,48);
  tft.print(wifiConnected?"WiFi: DA KET NOI":"WiFi: CHUA KET NOI");
  tft.setTextColor(C_GRAY); tft.setCursor(16,66);
  tft.print("DB: testfirebase-65de7");
  tft.drawFastHLine(16,80,288,C_BORDER);
  tft.setTextColor(C_WHITE); tft.setCursor(16,92);
  tft.print("Set RELAY_KEY=1 de mo cua");
  tft.setTextColor(C_GRAY); tft.setCursor(16,108);
  tft.print("Tu dong ve 0 khi cua dong lai");
  tft.drawFastHLine(16,125,288,C_BORDER);
  if(relayOpen && openSource=="APP") {
    tft.setTextColor(C_GREEN); tft.setTextSize(2);
    tft.setCursor(60,135); tft.print(">>> MO CUA <<<");
  } else if(msgText.length()>0) {
    tft.setTextColor(msgColor); tft.setTextSize(1);
    tft.setCursor(8+(304-msgText.length()*6)/2, 142);
    tft.print(msgText);
  } else {
    tft.setTextColor(C_GRAY); tft.setTextSize(1);
    tft.setCursor(16,142); tft.print("Dang cho lenh tu Firebase...");
  }
}

void handleAppTouch(int sx, int sy) {
  if(sy>=SCR_H-14 && sx>=SCR_W-65) drawPage(PAGE_HOME);
}

// ════════════════════════════════════════════
//  TRANG TÙY CHỌN
// ════════════════════════════════════════════
void drawTCPage() {
  drawHeader("TUY CHINH");
  drawTCBtn(0, "DOI MAT KHAU",  0x0640, C_WHITE);
  drawTCBtn(1, "THEM/XOA RFID", C_BTN_RFID, C_WHITE);
  drawTCBtn(2, "QUAY LAI",      C_BTN_BACK, C_WHITE);
  tft.setTextSize(1); tft.setTextColor(C_GRAY);
  tft.setCursor(8, SCR_H-20); tft.print("MK: ");
  for(int i=0; i<(int)strlen(savedPassword); i++) tft.print("*");
  tft.printf("  The: %d/%d", cardCount, MAX_CARDS);
  tft.setCursor(200, SCR_H-20);
  tft.setTextColor(wifiConnected?C_GREEN:C_RED);
  tft.print(wifiConnected?"WiFi:OK":"No WiFi");
}

void drawTCBtn(int idx, const char* lbl, uint16_t bg, uint16_t fg) {
  int x=60, y=56+idx*52, w=200, h=40, r=5;
  tft.fillRoundRect(x,y,w,h,r,bg);
  tft.drawRoundRect(x,y,w,h,r,C_BORDER);
  tft.setTextColor(fg); tft.setTextSize(1);
  tft.setCursor(x+(w-strlen(lbl)*6)/2, y+(h-8)/2);
  tft.print(lbl);
}

void handleTCTouch(int sx, int sy) {
  if(sx>=60&&sx<=260&&sy>=56 &&sy<=96 ) drawPage(PAGE_CHANGE_PW);
  else if(sx>=60&&sx<=260&&sy>=108&&sy<=148) drawPage(PAGE_RFID_CONFIRM);
  else if(sx>=60&&sx<=260&&sy>=160&&sy<=200) drawPage(PAGE_HOME);
}

// ════════════════════════════════════════════
//  TRANG XÁC NHẬN MẬT KHẨU
// ════════════════════════════════════════════
void drawRFIDConfirmPage() {
  drawHeader("XAC NHAN QUYEN TRUY CAP");
  drawRFIDConfirmDisplay();
  drawAllKeys();
  drawBackBar();
}

void drawRFIDConfirmDisplay() {
  tft.fillRoundRect(8,32,304,44,5,C_PANEL);
  tft.drawRoundRect(8,32,304,44,5,C_ORANGE);
  tft.setTextSize(1); tft.setTextColor(C_ORANGE);
  tft.setCursor(16,38); tft.print("NHAP MAT KHAU DE TIEP TUC");
  if(msgText.length()>0) {
    tft.setTextColor(msgColor);
    tft.setCursor(8+(304-msgText.length()*6)/2, 50);
    tft.print(msgText);
  } else if(inputStr.length()==0) {
    tft.setTextColor(C_GRAY); tft.setCursor(16,50);
    tft.print("Nhap mat khau admin...");
  } else {
    String m=""; for(int i=0;i<(int)inputStr.length();i++) m+="*";
    tft.setTextSize(3); tft.setTextColor(C_YELLOW);
    int tx=8+304-m.length()*18-12; if(tx<16)tx=16;
    tft.setCursor(tx,38); tft.print(m);
  }
}

void handleRFIDConfirmKey(const char* k) {
  if(strcmp(k,"DEL")==0){ if(inputStr.length()>0) inputStr.remove(inputStr.length()-1); drawRFIDConfirmDisplay(); return; }
  if(strcmp(k,"CLR")==0){ inputStr=""; drawRFIDConfirmDisplay(); return; }
  if(strcmp(k,"<<")==0) { drawPage(PAGE_TUYCHON); return; }
  if(strcmp(k,"OK" )==0){ checkRFIDConfirmPassword(); return; }
  if(inputStr.length()<PASS_MAX_LEN) inputStr+=k;
  drawRFIDConfirmDisplay();
}

void checkRFIDConfirmPassword() {
  bool ok = (inputStr.length()==strlen(savedPassword));
  if(ok) for(int i=0;i<(int)inputStr.length();i++)
    if(inputStr[i]!=savedPassword[i]){ ok=false; break; }
  if(ok){ inputStr=""; drawPage(PAGE_RFID_MGMT); }
  else  { msgText="SAI MAT KHAU!"; msgColor=C_RED; msgTimer=millis(); inputStr=""; drawRFIDConfirmDisplay(); }
}

// ════════════════════════════════════════════
//  TRANG QUẢN LÝ RFID
// ════════════════════════════════════════════
void drawRFIDMgmtPage() {
  drawHeader("QUAN LY THE RFID");
  tft.fillRoundRect(4,32,312,160,6,C_PANEL);
  tft.drawRoundRect(4,32,312,160,6,C_BORDER);
  if(cardCount==0) {
    tft.setTextColor(C_GRAY); tft.setTextSize(1);
    tft.setCursor(80,108); tft.print("Chua co the nao");
  } else {
    for(int i=0; i<cardCount; i++){
      int cy=36+i*30;
      bool sel=(i==selectedCard);
      tft.fillRoundRect(8,cy,230,24,4, sel?C_CARD_SEL:C_CARD_BG);
      tft.drawRoundRect(8,cy,230,24,4, sel?C_ACCENT:C_BORDER);
      tft.setTextColor(sel?C_WHITE:C_ACCENT); tft.setTextSize(1);
      tft.setCursor(14,cy+8); tft.printf("[%d] %s", i+1, cardList[i]);
      tft.fillRoundRect(244,cy,68,24,4,C_BTN_DEL);
      tft.drawRoundRect(244,cy,68,24,4,C_BORDER);
      tft.setTextColor(C_WHITE);
      tft.setCursor(258,cy+8); tft.print("XOA");
    }
  }
  int btnY=196;
  bool canAdd=(cardCount<MAX_CARDS);
  tft.fillRoundRect(4,btnY,150,36,6, canAdd?C_BTN_ADD:C_DARK);
  tft.drawRoundRect(4,btnY,150,36,6,C_BORDER);
  tft.setTextColor(canAdd?C_WHITE:C_GRAY); tft.setTextSize(1);
  tft.setCursor(4+(150-strlen("+ THEM THE MOI")*6)/2, btnY+14);
  tft.print("+ THEM THE MOI");
  tft.setTextColor(C_GRAY); tft.setCursor(162,btnY+8);
  tft.printf("%d/%d the", cardCount, MAX_CARDS);
  tft.fillRoundRect(240,btnY,76,36,6,C_BTN_BACK);
  tft.drawRoundRect(240,btnY,76,36,6,C_BORDER);
  tft.setTextColor(C_WHITE);
  tft.setCursor(240+(76-strlen("QUAY LAI")*6)/2, btnY+14);
  tft.print("QUAY LAI");
  if(msgText.length()>0) {
    tft.setTextColor(msgColor); tft.setTextSize(1);
    tft.setCursor(8+(304-msgText.length()*6)/2, SCR_H-8);
    tft.print(msgText);
  }
}

void handleRFIDMgmtTouch(int sx, int sy) {
  int btnY=196;
  if(sx>=4&&sx<=154&&sy>=btnY&&sy<=btnY+36) {
    if(cardCount<MAX_CARDS) drawPage(PAGE_RFID_ADD);
    else { msgText="DA DAY (toi da 5 the)!"; msgColor=C_RED; msgTimer=millis(); drawRFIDMgmtPage(); }
    return;
  }
  if(sx>=240&&sx<=316&&sy>=btnY&&sy<=btnY+36){ drawPage(PAGE_TUYCHON); return; }
  for(int i=0; i<cardCount; i++){
    int cy=36+i*30;
    if(sx>=244&&sx<=312&&sy>=cy&&sy<=cy+24){
      deleteCard(i);
      msgText="DA XOA THE!"; msgColor=C_GREEN; msgTimer=millis();
      selectedCard=-1; drawRFIDMgmtPage(); return;
    }
    if(sx>=8&&sx<=238&&sy>=cy&&sy<=cy+24){
      selectedCard=(selectedCard==i)?-1:i;
      drawRFIDMgmtPage(); return;
    }
  }
}

// ════════════════════════════════════════════
//  TRANG THÊM THẺ
// ════════════════════════════════════════════
void drawRFIDAddPage() {
  drawHeader("THEM THE MOI");
  tft.fillRoundRect(4,32,312,170,8,C_PANEL);
  tft.drawRoundRect(4,32,312,170,8,C_ACCENT);
  if(msgText.length()>0) {
    tft.setTextColor(msgColor); tft.setTextSize(1);
    tft.setCursor(8+(304-msgText.length()*6)/2, 90);
    tft.print(msgText);
    tft.setTextColor(C_GRAY); tft.setCursor(50,110);
    tft.print("Chuyen trang sau 2 giay...");
  } else {
    tft.drawRoundRect(100,50,120,60,8,C_ACCENT);
    tft.drawRoundRect(104,54,112,52,6,C_BTN_RFID);
    tft.setTextColor(C_GREEN); tft.setTextSize(1);
    tft.setCursor(112,76); tft.print("[ THE RFID MOI ]");
    tft.setTextColor(C_WHITE);
    tft.setCursor(48,128); tft.print("Dua the can them vao dau doc...");
    tft.setTextColor(C_GRAY); tft.setCursor(16,150);
    tft.printf("Dang co: %d/%d the", cardCount, MAX_CARDS);
  }
  tft.fillRoundRect(110,178,100,36,6,C_BTN_BACK);
  tft.drawRoundRect(110,178,100,36,6,C_BORDER);
  tft.setTextColor(C_WHITE); tft.setTextSize(1);
  tft.setCursor(110+(100-strlen("HUY")*6)/2, 178+14);
  tft.print("HUY");
}

void handleRFIDAddScan() {
  Serial.println("handleRFIDAddScan UID: " + rfidUID);
  if(!waitingNewCard) return;

  bool dup = false;
  for(int i=0; i<cardCount; i++)
    if(rfidUID==String(cardList[i])){ dup=true; break; }

  if(dup)
    { msgText="THE DA TON TAI!";      msgColor=C_ORANGE; }
  else if(addCard(rfidUID))
    { msgText="THEM THE THANH CONG!"; msgColor=C_GREEN;  }
  else
    { msgText="LOI THEM THE!";        msgColor=C_RED;    }

  msgTimer        = millis();
  waitingNewCard  = false;
  pendingGoToMgmt = true;
  addCardTimer    = millis();
  drawRFIDAddPage();
}

void handleRFIDAddTouch(int sx, int sy) {
  if(sx>=110 && sx<=210 && sy>=178 && sy<=214) {
    if(!pendingGoToMgmt) {
      waitingNewCard  = false;
      pendingGoToMgmt = false;
      drawPage(PAGE_RFID_MGMT);
    }
  }
}

// ════════════════════════════════════════════
//  TRANG ĐỔI MẬT KHẨU
// ════════════════════════════════════════════
void drawChangePwPage() {
  drawHeader("DOI MAT KHAU");
  drawChangePwDisplay();
  drawAllKeys();
  drawBackBar();
}

void drawChangePwDisplay() {
  tft.fillRoundRect(8,32,304,44,5,C_PANEL);
  const char* lbs[]={"NHAP MAT KHAU CU","NHAP MAT KHAU MOI","XAC NHAN MAT KHAU MOI"};
  uint16_t    cls[]={C_YELLOW, C_ACCENT, C_ORANGE};
  tft.drawRoundRect(8,32,304,44,5,cls[changePwStep]);
  tft.setTextSize(1); tft.setTextColor(cls[changePwStep]);
  tft.setCursor(16,38); tft.print(lbs[changePwStep]);
  if(msgText.length()>0) {
    tft.setTextColor(msgColor);
    tft.setCursor(8+(304-msgText.length()*6)/2, 50);
    tft.print(msgText);
  } else if(inputStr.length()==0) {
    tft.setTextColor(C_GRAY); tft.setCursor(16,50);
    tft.print("Nhap va bam OK...");
  } else {
    String m=""; for(int i=0;i<(int)inputStr.length();i++) m+="*";
    tft.setTextSize(3); tft.setTextColor(C_WHITE);
    int tx=8+304-m.length()*18-12; if(tx<16)tx=16;
    tft.setCursor(tx,38); tft.print(m);
  }
  for(int i=0;i<3;i++)
    tft.fillCircle(144+(i-1)*20, 28, 4, (i<=changePwStep)?C_ACCENT:C_DARK);
}

void handleChangePwKey(const char* k) {
  if(strcmp(k,"DEL")==0){ if(inputStr.length()>0) inputStr.remove(inputStr.length()-1); drawChangePwDisplay(); return; }
  if(strcmp(k,"CLR")==0){ inputStr=""; drawChangePwDisplay(); return; }
  if(strcmp(k,"<<")==0) { drawPage(PAGE_TUYCHON); return; }
  if(strcmp(k,"OK" )==0){ processChangePw(); return; }
  if(inputStr.length()<PASS_MAX_LEN) inputStr+=k;
  drawChangePwDisplay();
}

void processChangePw() {
  if(changePwStep==0) {
    bool ok=(inputStr.length()==strlen(savedPassword));
    if(ok) for(int i=0;i<(int)inputStr.length();i++)
      if(inputStr[i]!=savedPassword[i]){ ok=false; break; }
    if(ok){ changePwStep=1; inputStr=""; msgText=""; drawChangePwDisplay(); }
    else  { msgText="MAT KHAU CU SAI!"; msgColor=C_RED; msgTimer=millis(); inputStr=""; drawChangePwDisplay(); }
  } else if(changePwStep==1) {
    if(inputStr.length()<4){ msgText="Toi thieu 4 ky tu!"; msgColor=C_ORANGE; msgTimer=millis(); inputStr=""; drawChangePwDisplay(); }
    else { newPassStr=inputStr; changePwStep=2; inputStr=""; msgText=""; drawChangePwDisplay(); }
  } else {
    if(inputStr==newPassStr){
      inputStr.toCharArray(savedPassword, PASS_MAX_LEN+1);
      savePassword();
      msgText="DOI MAT KHAU OK!"; msgColor=C_GREEN; msgTimer=millis();
      inputStr=""; newPassStr="";
      delay(1500); drawPage(PAGE_TUYCHON);
    } else {
      msgText="KHONG KHOP! Nhap lai"; msgColor=C_RED; msgTimer=millis();
      inputStr=""; changePwStep=1; newPassStr=""; drawChangePwDisplay();
    }
  }
}

// ════════════════════════════════════════════
//  KEYBOARD TOUCH
// ════════════════════════════════════════════
void handleKbdTouch(int sx, int sy) {
  if(sy>=SCR_H-14 && sx>=SCR_W-65) {
    if     (currentPage==PAGE_CHANGE_PW)    drawPage(PAGE_TUYCHON);
    else if(currentPage==PAGE_RFID_CONFIRM) drawPage(PAGE_TUYCHON);
    else                                    drawPage(PAGE_HOME);
    return;
  }
  int col=(sx-KEY_X)/(KEY_W+KEY_GAP);
  int row=(sy-KEY_Y)/(KEY_H+KEY_GAP);
  if(row<0||row>=KEY_ROWS||col<0||col>=KEY_COLS) return;
  int kx=KEY_X+col*(KEY_W+KEY_GAP), ky=KEY_Y+row*(KEY_H+KEY_GAP);
  if(sx<kx||sx>kx+KEY_W||sy<ky||sy>ky+KEY_H) return;
  if(holding&&row==holdRow&&col==holdCol) return;
  if(holding) drawKey(holdRow,holdCol,false);
  holdRow=row; holdCol=col; holding=true;
  drawKey(row,col,true);
  if(currentPage==PAGE_PASSWORD)     handlePassKey(keyLabels[row][col]);
  if(currentPage==PAGE_CHANGE_PW)    handleChangePwKey(keyLabels[row][col]);
  if(currentPage==PAGE_RFID_CONFIRM) handleRFIDConfirmKey(keyLabels[row][col]);
}

// ════════════════════════════════════════════
//  UI HELPERS
// ════════════════════════════════════════════
void drawHeader(const char* title) {
  tft.fillRect(0,0,SCR_W,30,C_PANEL);
  tft.drawFastHLine(0,30,SCR_W,C_ACCENT);
  tft.setTextColor(C_ACCENT); tft.setTextSize(1);
  tft.setCursor((SCR_W-strlen(title)*6)/2, 11);
  tft.print(title);
}

void drawBackBar() {
  tft.fillRect(0,SCR_H-14,SCR_W,14,C_PANEL);
  tft.setTextSize(1);
  if(rfidMsgText.length()>0) {
    tft.setTextColor(rfidMsgColor);
    tft.setCursor(8+(304-(int)rfidMsgText.length()*6)/2, SCR_H-10);
    tft.print("RFID: "); tft.print(rfidMsgText);
    return;
  }
  if(relayOpen){
    tft.setTextColor(C_GREEN); tft.setCursor(8,SCR_H-10);
    tft.print(">>> CUA DANG MO: "); tft.print(openSource);
  } else {
    tft.setTextColor(C_GRAY); tft.setCursor(8,SCR_H-10);
    tft.print("Chua mo cua");
  }
  tft.setTextColor(C_ACCENT);
  tft.setCursor(SCR_W-60, SCR_H-10); tft.print("[BACK]");
}

void drawStatusFooter() {
  tft.fillRect(0,SCR_H-20,SCR_W,20,C_PANEL);
  tft.drawFastHLine(0,SCR_H-20,SCR_W,C_BORDER);
  tft.setTextSize(1);
  if(rfidMsgText.length()>0) {
    tft.setTextColor(rfidMsgColor);
    tft.setCursor(8, SCR_H-13);
    tft.print("RFID: "); tft.print(rfidMsgText);
    return;
  }
  if(relayOpen){
    tft.setTextColor(C_GREEN); tft.setCursor(8,SCR_H-13);
    tft.print(">>> MO CUA: "); tft.print(openSource);
  } else {
    tft.setTextColor(C_RED); tft.setCursor(8,SCR_H-13);
    tft.print("DONG CUA");
  }
  tft.setTextColor(wifiConnected?C_GREEN:C_GRAY);
  tft.setCursor(SCR_W-60, SCR_H-13);
  tft.print(wifiConnected?"WiFi:OK":"No WiFi");
}

void drawAllKeys() {
  for(int r=0;r<KEY_ROWS;r++)
    for(int c=0;c<KEY_COLS;c++)
      drawKey(r,c,false);
}

void drawKey(int row, int col, bool pressed) {
  int x=KEY_X+col*(KEY_W+KEY_GAP), y=KEY_Y+row*(KEY_H+KEY_GAP);
  uint16_t bg=pressed?C_KEY_PRESS:keyBg[keyType[row][col]];
  uint16_t fg=pressed?C_BLACK:C_WHITE;
  uint16_t bd=pressed?C_WHITE:C_BORDER;
  tft.fillRoundRect(x,y,KEY_W,KEY_H,KEY_R,bg);
  tft.drawRoundRect(x,y,KEY_W,KEY_H,KEY_R,bd);
  const char* lbl=keyLabels[row][col];
  tft.setTextColor(fg);
  if(strlen(lbl)==1){ tft.setTextSize(2); tft.setCursor(x+(KEY_W-12)/2, y+(KEY_H-16)/2); }
  else              { tft.setTextSize(1); tft.setCursor(x+(KEY_W-(int)strlen(lbl)*6)/2, y+(KEY_H-8)/2); }
  tft.print(lbl);
}
