#include <LiquidCrystal.h>

// https://codebender.cc/library/HX711
#include "HX711.h"

// http://robotsbigdata.com/docs-arduino-timer.html
#include <RBD_Timer.h>

// Waage initialisieren
HX711 scale(A1, A0);

// LCD initialisieren
LiquidCrystal lcd(12, 11, 5, 4, 3, 2);

// Output-Pin fuer die LCD Hintergrundbeleuchtung
int const PIN_LCD_BACKGROUND_LIGHT = 10;

// Timer fuer die Spuehlungen
RBD::Timer timerMS;
RBD::Timer timerSS;
RBD::Timer timerRS;
RBD::Timer timerLCD;

// Skalierungs-Faktor für die Messung (siehe Programm Waage-Scale-Ermittlung)
float const SCALE = 219.5f;

// Gewichtsgrenzen
const int MIN_WEIGHT = -100;
const int START_WEIGHT = 200;
const int STOP_WEIGHT = 1200;

// Timer
const long TIMER_MEMBRAN_SPUEHLUNG = 30000;     // 30 Sekunden
const long TIMER_STEHWASSER_SPEUHLUNG = 120000; // 2 Minuten
const long TIMER_LCD_OFF = 10000;               // 10 Sekunden
const long TIMER_REGEL_SPUEHLUNG = 7200000;     // 2 Stunden

// Relay-Pins
const int relayPin1 = 9;
const int relayPin2 = 8;
const int relayPin3 = 7;
const int relayPin4 = 6;

// aktuelles gewicht in gramm
int currentWeight;

// filter phasen
const int PHASE_PASSIV = 0;
const int PHASE_VORSPUEHLUNG = 1;
const int PHASE_FILTER = 2;
const int PHASE_NACHSPUEHLUNG = 3;
const int PHASE_REGELSPUEHLUNG = 4;
int filterPhase = PHASE_PASSIV;

// lcd status
bool isLcdOn = false;

/*********
 * SETUP
 *********/
void setup() {
  // Initialisierung
  initRelays();
  initLCD();
  initTimer();
  
  // Kalibriere Waage
  calibrate();  
}

void initRelays() {
  pinMode(relayPin1, OUTPUT);
  pinMode(relayPin2, OUTPUT);
  pinMode(relayPin3, OUTPUT);
  pinMode(relayPin4, OUTPUT);
  
  // Prevents relays from starting up engaged
  setRelays(HIGH, HIGH, HIGH, HIGH);
}

void setRelays(uint8_t relay1, uint8_t relay2, uint8_t relay3, uint8_t relay4) {
  digitalWrite(relayPin1, relay1);
  digitalWrite(relayPin2, relay2);
  digitalWrite(relayPin3, relay3);
  digitalWrite(relayPin4, relay4);
}

void initLCD() {
  // Ausgang fuer die Hintergrundbeleuchtung
  pinMode(PIN_LCD_BACKGROUND_LIGHT, OUTPUT);  
  
  // Spalten und Zeilen vom LCD festlegen
  lcd.begin(16, 2); 
  lcdOn();  
}

void initTimer() {
  // Membran-Spuehlung 30 Sekunden
  timerMS.setTimeout(TIMER_MEMBRAN_SPUEHLUNG);
  timerMS.stop();
 
  // Stehwasser-Spuehlung 120 Sekunden
  timerSS.setTimeout(TIMER_STEHWASSER_SPEUHLUNG);
  timerSS.stop();
  
  // LCD ausschalten nach 10 Sekunden
  timerLCD.setTimeout(TIMER_LCD_OFF);
  timerLCD.stop();
  
  // Regel-Spuehlung nach 4 Stunden passiv
  timerRS.setTimeout(TIMER_REGEL_SPUEHLUNG);
  timerRS.stop();  
}

void calibrate() {
  printLcdBoth("Zur Kalibrierung", "Taste-1 druecken");
  
  // Warte bis Taste-1 gedückt wird und starte dann Kalibrierung
  while(readButton() != 1) { }
  printLcdBoth("Kalibrierung", "gestartet");
  
  // warte bis Taste-1 losgelassen wird
  while(readButton() == 1) { }
  
  // Spezifischer Wert für DMS eintragen
  scale.set_scale(SCALE);
  
  // Wagge nullen 
  scale.tare(50);
  
  printLcdBoth("Kalibrierung", "abgeschlossen");  
}

/*********
 * LOOP
 *********/
void loop() {
  readWeight();

  enableLCDOnButton2();
  
  handleLcdOff();
  handleStart();
  handleStop();
  handleTimerMS();
  handleTimerSS();
  handleTimerRS();
}

void readWeight() {
  // Lese Gewicht
  currentWeight = scale.get_units(5);
  
  printLcdFirst("Phase " + String(filterPhase));
  
  if (filterPhase == PHASE_PASSIV || filterPhase == PHASE_FILTER) {
    lcd.setCursor(11, 0);
    lcd.print(fillText(String(currentWeight), 4, true) + String("g"));
  }  
}

void enableLCDOnButton2() {
  if (isLcdOn == false && readButton() == 2) {
    lcdOn();
    timerLCD.restart();
  }  
}

int readButton() {
  int value = analogRead(A2);
  if (value < 10) {
    return 0;  
  } else if (value > 500 && value < 520) {
    return 2;
  } else if (value > 1010 && value < 1030) {
    return 1; 
  }
} 

void stopAllTimer() {
  // alle timer stoppen
  if (timerMS.isActive()) {
    timerMS.stop();
  }
  if (timerSS.isActive()) {
    timerSS.stop();
  }
  if (timerRS.isActive()) {
    timerRS.stop();
  }
  if (timerLCD.isActive()) {
    timerLCD.stop();
  }  
}
 
void handleStart() {
  if (filterPhase == PHASE_PASSIV && currentWeight >= MIN_WEIGHT && currentWeight <= START_WEIGHT) {
    stopAllTimer();

    // Spuehlung und Filter starten
    filterPhase = PHASE_VORSPUEHLUNG;
    timerMS.restart();
  }
}
 
void handleStop() {
  bool karaffeVoll = currentWeight >= STOP_WEIGHT;
  bool karaffeWeg = currentWeight < MIN_WEIGHT;
  
  if (karaffeVoll || karaffeWeg) {
    if (karaffeWeg && filterPhase == PHASE_PASSIV) {
      printLcdSecond("Glas weg");
    }
    else if (karaffeVoll && filterPhase == PHASE_PASSIV) {
      printLcdSecond("Glas voll");
    }     
    
    if (filterPhase == PHASE_VORSPUEHLUNG || filterPhase == PHASE_FILTER) {
      // alle relais schliessen
      setRelays(HIGH, HIGH, HIGH, HIGH);
      
      stopAllTimer();      
      
      if (filterPhase == PHASE_FILTER) {
        // Nachspuehlung starten
        filterPhase = PHASE_NACHSPUEHLUNG;
        timerMS.restart();          
      } else {
        filterPhase = PHASE_PASSIV;
      }
    }
  } else if (filterPhase == PHASE_PASSIV) {
    if (currentWeight > MIN_WEIGHT) {
      printLcdSecond("Glas voll");
    } else {
      printLcdSecond("");
    }
  }

  if (filterPhase == PHASE_PASSIV && isLcdOn && !timerLCD.isActive()) {
    // lcd ausschalten
    timerLCD.restart();
  }   
}
 
void handleTimerMS() {
  if (timerMS.onActive()) {
    lcdOn();
    printLcdSecond("Membran-Sp.");
    setRelays(LOW, HIGH, HIGH, LOW);
  }
  if(timerMS.isActive()) {
    long remainingTime = timerMS.getInverseValue() / 1000;
    lcd.setCursor(12, 1);
    lcd.print(fillText(String(remainingTime), 3, true) + String("s"));
  }
  if(timerMS.onExpired()) {
    if (filterPhase == PHASE_NACHSPUEHLUNG) {
      // keine stehwasser-spuehlung bei der nach-spuehlung
      filterPhase = PHASE_PASSIV;
      setRelays(HIGH, HIGH, HIGH, HIGH);
      
      // starte regelspuehlungs-timer im anschluss an die nachspuehlung
      timerRS.restart();
    }
    else {
      // starte stehwasser-spuehlung
      timerSS.restart();
    }
  }
}

void handleTimerSS() {
  if (timerSS.onActive()) {
    printLcdSecond("Stehwasser");
    setRelays(LOW, HIGH, LOW, HIGH);
  }  
  if(timerSS.isActive()) {
    long remainingTime = timerSS.getInverseValue() / 1000;
    lcd.setCursor(12, 1);
    lcd.print(fillText(String(remainingTime), 3, true) + String("s"));    
  }
  if(timerSS.onExpired()) {
    if (filterPhase == PHASE_VORSPUEHLUNG) {
        filterPhase = PHASE_FILTER;
        printLcdSecond("Filter Wasser");
        setRelays(LOW, LOW, HIGH, HIGH);
    } else {
        filterPhase = PHASE_PASSIV;
        setRelays(HIGH, HIGH, HIGH, HIGH);
        
      // starte regelspuehlungs-timer im anschluss an die regelspuehlung
      timerRS.restart();        
    }
  }
}

void handleTimerRS() {
  if(timerRS.isActive()) {
    long remainingTime = timerRS.getInverseValue() / 1000 / 60;
    lcd.setCursor(12, 1);
    lcd.print(fillText(String(remainingTime), 3, true) + String("m"));
  }  
  if (timerRS.onExpired()) {
    // starte regelspuehlung wenn passiv
    if (filterPhase == PHASE_PASSIV) {
      filterPhase = PHASE_REGELSPUEHLUNG;
      timerMS.restart();
    }
  } 
}

void handleLcdOff() {
  if (timerLCD.onExpired()) {
    lcdOff(); 
  } 
}

void printLcdFirst(String text) {
  lcd.setCursor(0, 0);
  lcd.print(fillText(text, 16, false));
}

void printLcdSecond(String text) {
  lcd.setCursor(0, 1);
  lcd.print(fillText(text, 16, false));
}

void printLcdBoth(String text1, String text2) {
  lcd.setCursor(0, 0);
  lcd.print(fillText(text1, 16, false));
  lcd.setCursor(0, 1);
  lcd.print(fillText(text2, 16, false));
}

void lcdOff() {
  digitalWrite(PIN_LCD_BACKGROUND_LIGHT, LOW);
  //lcd.noDisplay();
  isLcdOn = false;
}

void lcdOn() {
  digitalWrite(PIN_LCD_BACKGROUND_LIGHT, HIGH);
  //lcd.display(); 
  isLcdOn = true;
}

String fillText(String text, int newLength, bool invert) {
 int textLength = text.length();
 int missingBlanks = newLength - textLength;
 for (int i=1; i<=missingBlanks; i++) {
   if (invert) {
     text = String(" ") + text; 
   } else {
     text = text + String(" ");  
   }
 }
 return text;
}
