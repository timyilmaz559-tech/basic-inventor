#include <Adafruit_SSD1306.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <DHT.h>
#include <avr/wdt.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

#define DHTPIN 2
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

const int panel_current_pin = A1;// Akım sensörü pini
const int panel_voltage_pin = A2;// Voltaj devresi

const int batarya_enerji_role_pini = 3;// çıkış prizine bataryadan enerji basma kesme rölesi
const int batarya_panel_sarj_role_pini = 5;// bataryanın panelden şarj kesme rölesi
const int panel_fazla_uretim_harcama_role_pini = 7;// fazla enerjinin tüketildiği çıkış kesme rölesi
const int batarya_doluluk_pini = A0;// bataryanın doluluğunu ölçen analog giriş
const int buzzer = 8;

struct datas {
    float panel_pin_voltage = 0;// panel anlık voltage
    float panel_pin_last_voltage = 0;// panel anlık son voltage

    int batarya_doluluk_yuzdesi = 0;
    float h = 0.00;// nem
    float t = 0.00;// sıcaklık

    float panel_current = 0.0;// Panelin anlık akımı
    float anlik_guc = 0.0;// panelin anlık watt
    float panel_last_current = 0.0;// panelin son anlık akımı
    // Kullanılan elektrik miktarı için
    float kullanilan_enerji_kwh = 0.0;
    unsigned long son_zaman = 0;

    bool enerji_batarya_kullanimi = false;
        bool batarya_sarj_edilme_durumu = false;
    bool tehlike_durumu = false;// arıza vb. durum bildirim değeri

    String batarya_sarj_gosterim = "";
    String enerji_kullanim_kaynagi = "";
};
datas room;

unsigned long baslangic_zamani = 0;
const long sure = 2000;

void setup() {
    Serial.begin(9600);// raspberry ile iletişim portu
    dht.begin();

    // output pins
    pinMode(batarya_enerji_role_pini, OUTPUT);
    pinMode(batarya_panel_sarj_role_pini, OUTPUT);
    pinMode(panel_fazla_uretim_harcama_role_pini, OUTPUT);
    pinMode(buzzer, OUTPUT);

    // input pins
    pinMode(batarya_doluluk_pini, INPUT);
    pinMode(panel_current_pin, INPUT);
    pinMode(panel_voltage_pin, INPUT);

    // ekran başlatılamama hata kontrolü
    if(!display.begin(SSD1306_SWITCHCAPVCC, 0X3C)) {
        Serial.print("Oled başlatılamadı");
        for(;;);
    }
    display.clearDisplay();
    display.display();

    // watchdog timer ayarlaması
    wdt_disable();
    delay(100);
    wdt_enable(WDTO_4S);
}

void loop() {
    // watchdog timer kontrolü
    wdt_reset();

    room.batarya_doluluk_yuzdesi = map(analogRead(batarya_doluluk_pini), 0, 1023, 0, 100);

    // Panelin anlık akımını oku (ACS712 30A için)
    int current_raw = analogRead(panel_current_pin);
    float current_voltage = (current_raw / 1023.0) * 5.0;
    //panelin voltajını hesapla
    room.panel_pin_voltage = (analogRead(panel_voltage_pin) / 1023.0) * 5.0; // 5V referans
    room.panel_current = (current_voltage - 2.5) / 0.066; // ACS712 30A için
    // Kullanılan elektrik miktarı (kWh) hesaplama
    unsigned long simdiki_zaman = millis();
    room.anlik_guc = room.panel_pin_voltage * room.panel_current; // Watt
    if (room.son_zaman > 0 && room.batarya_doluluk_yuzdesi >= 20) {// eğer sistemde güç açıksa
        float saat_farki = (simdiki_zaman - room.son_zaman) / 3600000.0; // ms -> saat
        room.kullanilan_enerji_kwh += (room.anlik_guc * saat_farki) / 1000.0; // kWh
    }
    room.son_zaman = simdiki_zaman;

    // şarj ve fazla enerji kontrolü
    if(room.panel_pin_voltage >= 12 && room.batarya_doluluk_yuzdesi < 100) {
        // önce diğer röleleri kapat sonra ilgili röleyi aç
        digitalWrite(panel_fazla_uretim_harcama_role_pini, LOW);
        digitalWrite(batarya_panel_sarj_role_pini, HIGH);
        room.batarya_sarj_edilme_durumu = true;
        room.batarya_sarj_gosterim = "Panel";
    }else if(room.batarya_doluluk_yuzdesi < 100 && room.panel_pin_voltage < 12) {
        // önce diğer röleleri kapat sonra ilgili röleyi aç
        digitalWrite(batarya_panel_sarj_role_pini, LOW);
        digitalWrite(panel_fazla_uretim_harcama_role_pini, LOW);
        room.batarya_sarj_edilme_durumu = true;
        room.batarya_sarj_gosterim = "Sarj olmuyor";
    }else if(room.batarya_doluluk_yuzdesi == 100 && room.panel_pin_voltage >= 12) {
        // önce diğer röleleri kapat sonra ilgili röleyi aç
        digitalWrite(batarya_panel_sarj_role_pini, LOW);
        digitalWrite(panel_fazla_uretim_harcama_role_pini, HIGH);
        room.batarya_sarj_edilme_durumu = false;
        room.batarya_sarj_gosterim = "Dolu";
    }

    //enerji kullanım kaynağı kontrolü
    if(room.batarya_doluluk_yuzdesi >= 20) {
        // önce diğer röleleri kapat sonra ilgili röleyi aç
        digitalWrite(batarya_enerji_role_pini, HIGH);
        room.enerji_kullanim_kaynagi = "Batarya";
    }else {
        // önce diğer röleleri kapat sonra ilgili röleyi aç
        digitalWrite(batarya_enerji_role_pini, LOW);
        room.enerji_kullanim_kaynagi = "Dusuk Enerji";
    }

    // ısı nem okumasını 2 sn yede bir yap
    unsigned long gecen_sure = millis();
    if(gecen_sure - baslangic_zamani >= sure) {
        baslangic_zamani = gecen_sure;
        isi_nem_okuma();
    }

    ekran_gosterim();
    veri_gonderim();
    ariza_tespit();

    delay(100);
}

void isi_nem_okuma() {
    wdt_reset();

    room.h = dht.readHumidity();
    room.t = dht.readTemperature();

    if(isnan(room.h) || isnan(room.t)) {
        return;
    }
}

void ekran_gosterim() {
    // harici bir oled üzerinden veri doğrulaması yapılabilecek
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.setCursor(0, 0);

    display.println(room.batarya_sarj_gosterim);
    display.println("Batarya sarj: " + String(room.batarya_doluluk_yuzdesi));
    display.println("Enerji kaynagi: " + room.enerji_kullanim_kaynagi);

    display.println("Panel voltaj: " + String(room.panel_pin_voltage));
    display.println("Panel akim: " + String(room.panel_current) + " A");
    display.println("Panel guc: " + String(room.anlik_guc));
    display.println("Kullanilan KWh: " + String(room.kullanilan_enerji_kwh, 2));

    display.display();
}

void veri_gonderim() {
    // Serial üzerinden raspberry'e veri akışı
    Serial.print(String(room.kullanilan_enerji_kwh) + ",");
    Serial.print(String(room.panel_pin_voltage) + ",");
    Serial.print(String(room.panel_current) + ",");
    Serial.print(String(room.t) + ",");
    Serial.print(String(room.h) + ",");
    Serial.println(String(room.batarya_doluluk_yuzdesi));
}

void ariza_tespit() {
    wdt_reset();
    // olağandışı bir enerji dalgalanması ya da düşüşü ara
    if(room.panel_current < room.panel_last_current && room.panel_last_current - room.panel_current >= 5) {// aradaki fark örnektir
        room.tehlike_durumu = true;
        digitalWrite(buzzer, HIGH);
        delay(250);
        digitalWrite(buzzer, LOW);
        delay(250);
    }else if(room.panel_pin_voltage < room.panel_pin_last_voltage && room.panel_pin_last_voltage - room.panel_pin_voltage >= 10) {// aradaki fark örnektir
        room.tehlike_durumu = true;
        digitalWrite(buzzer, HIGH);
        delay(250);
        digitalWrite(buzzer, LOW);
        delay(250);
    }else {
        room.tehlike_durumu = false;
        digitalWrite(buzzer, LOW);
    }

    // kontrolden sonra son değerleri ata
    // aynı değer çıkmasını önler
    room.panel_last_current = room.panel_current;
    room.panel_pin_last_voltage = room.panel_pin_voltage;
}