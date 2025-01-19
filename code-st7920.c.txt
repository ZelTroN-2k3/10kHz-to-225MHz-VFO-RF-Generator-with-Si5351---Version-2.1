/**********************************************************************************************************
  10kHz to 225MHz VFO / RF Generator with Si5351 and Arduino Nano
  + IF offset (+/-), RX/TX selector, mémoires de bandes, et bargraphe S-Meter., 
  sélecteur RX/TX pour transceivers QRP,
  [* Basé sur le code original de J. CesarSound - ver 2.0 - Feb/2021 *]

  Adaptation pour ST7920 (128x64) + U8g2 (page buffer) par [ZelTroN2k3] 18.01.2025.
***********************************************************************************************************/

/**********************************************************************************************************
Le croquis utilise 26162 octets (85%) de l'espace de stockage de programmes. 
  Le maximum est de 30720 octets.
Les variables globales utilisent 1061 octets (51%) de mémoire dynamique, 
  ce qui laisse 987 octets pour les variables locales. Le maximum est de 2048 octets.
**********************************************************************************************************/  

// -------------------------------------------------------------------------------------
//  Librairies
// -------------------------------------------------------------------------------------
#include <Arduino.h>
#include <Wire.h>                 // Librairie I2C standard
#include "Rotary.h"               // Ben Buxton https://github.com/brianlow/Rotary
#include "si5351.h"               // Etherkit https://github.com/etherkit/Si5351Arduino


// -------------------------------------------------------------------------------------
//  Définition du pilote U8g2 (ST7920 128x64)
// -------------------------------------------------------------------------------------
// Exemple en mode SPI logiciel (SW SPI) :
//    U8G2_R0         = pas de rotation (orientation standard)
//    clock = 13, data = 11, cs = 10  (à adapter selon votre câblage !)
//    reset = U8X8_PIN_NONE si vous n'avez pas de reset dédié
// -------------------------------------------------------------------------------------
// U8G2_ST7920_128X64_X_XX_SPI (rotation, clock, data, cs [, reset])
// Display Pin	Arduino / Arduino Nano Pin Number
// E, Clock = 	        (13)
// RW, Data, MOSI =	    (11)
// RS, Chip Select =	  (10)
// RST, Reset =	        (8)
// 


// ---- U8g2 pour ST7920 en mode "page buffer" (1 page) + SW SPI ---
#include <U8g2lib.h>

// Constructeur : ST7920, 128x64, rotation R0, Software SPI
// clock=13, data=11, cs=10, reset=U8X8_PIN_NONE
U8G2_ST7920_128X64_1_SW_SPI u8g2(
  U8G2_R0, /* clock=*/ 13, /* data=*/ 11, /* cs=*/ 10, /* reset=*/ U8X8_PIN_NONE
);


// -------------------------------------------------------------------------------------
//  Paramètres utilisateur
// -------------------------------------------------------------------------------------
// Paramètres utilisateur
#define IF         455       // IF offset (kHz) - 0 si direct
#define BAND_INIT  7         // Bande initiale
#define XT_CAL_F   33000     // Calibration du XO du Si5351
#define S_GAIN     303       // Sensibilité pour S-Meter (analogRead)
#define tunestep   A0        // Pin pour bouton du step
#define band       A1        // Pin pour bouton de bande
#define rx_tx      A2        // Pin pour sélecteur RX/TX
#define adc        A3        // Pin pour S-meter


// -------------------------------------------------------------------------------------
//  Objets / Variables globales
// -------------------------------------------------------------------------------------
Rotary r = Rotary(2, 3);
Si5351 si5351(0x60); // Adresse I2C par défaut 0x60

unsigned long freq, freqold, fstep;
long interfreq = IF, interfreqold = 0;
long cal = XT_CAL_F;
unsigned int smval;
byte encoder = 1;
byte stp, n = 1;
byte count, x, xo;
bool sts = 0;              // 0=RX, 1=TX
unsigned int period = 100; // période pour rafraîchir l'affichage
unsigned long time_now = 0;


// -------------------------------------------------------------------------------------
//  Interruption sur les broches de l'encodeur
// --------------------------------------------------------------------------------------
ISR(PCINT2_vect) {
  char result = r.process();
  if (result == DIR_CW) set_frequency(1);
  else if (result == DIR_CCW) set_frequency(-1);
}


// -------------------------------------------------------------------------------------
//  Fonctions
// -------------------------------------------------------------------------------------

// --- Variation de la fréquence via l'encodeur ---
void set_frequency(short dir) {
  if (encoder == 1) {  
    // Variation freq
    if (dir == 1) freq += fstep;
    if (freq >= 225000000UL) freq = 225000000UL;
    if (dir == -1) freq -= fstep;
    if (fstep == 1000000UL && freq <= 1000000UL) freq = 1000000UL;
    else if (freq < 10000UL) freq = 10000UL;
  }
  // Curseur "n" (pour le bargraphe "TU")
  if (encoder == 1) {
    if (dir == 1) n++;
    if (n > 42) n = 1;
    if (dir == -1) n--;
    if (n < 1) n = 42;
  }
}

// -------------------------------------------------------------------------------------
//  Setup
// ------------------------------------------------------------------------------------
void setup() {
  Wire.begin();

  // Initialisation U8g2 (ST7920)
  u8g2.begin();

  statup_text(); // Affichage initial (optionnel)

  // Broches encodeur et boutons
  pinMode(2, INPUT_PULLUP);
  pinMode(3, INPUT_PULLUP);
  pinMode(tunestep, INPUT_PULLUP);
  pinMode(band, INPUT_PULLUP);
  pinMode(rx_tx, INPUT_PULLUP);

  // Init Si5351
  si5351.init(SI5351_CRYSTAL_LOAD_8PF, 0, 0);
  si5351.set_correction(cal, SI5351_PLL_INPUT_XO);
  si5351.drive_strength(SI5351_CLK0, SI5351_DRIVE_8MA);
  si5351.output_enable(SI5351_CLK0, 1);
  si5351.output_enable(SI5351_CLK1, 0);
  si5351.output_enable(SI5351_CLK2, 0);

  // Interruption pour l'encodeur
  PCICR |= (1 << PCIE2);
  PCMSK2 |= (1 << PCINT18) | (1 << PCINT19);
  sei();

  count = BAND_INIT;
  bandpresets();
  stp = 4;
  setstep();
}

// -------------------------------------------------------------------------------------
//  Boucle principale
// -------------------------------------------------------------------------------------
void loop() {
  // Mise à jour si la fréquence change
  if (freqold != freq) {
    time_now = millis();
    tunegen();
    freqold = freq;
  }
  // Mise à jour si la FI change
  if (interfreqold != interfreq) {
    time_now = millis();
    tunegen();
    interfreqold = interfreq;
  }
  // Mise à jour du S-meter
  if (xo != x) {
    time_now = millis();
    xo = x;
  }

  // Gestion des boutons
  if (digitalRead(tunestep) == LOW) {
    time_now = millis() + 300;
    setstep();
    delay(300);
  }

  if (digitalRead(band) == LOW) {
    time_now = millis() + 300;
    inc_preset();
    delay(300);
  }

  if (digitalRead(rx_tx) == LOW) {
    // TX
    time_now = millis() + 300;
    sts = 1;
  } else {
    // RX
    sts = 0;
  }

  // Affichage toutes les 'period'
  if ((time_now + period) > millis()) {
    // -- Mode page buffer --
    // Tout le dessin se fait dans la boucle do/while.
    u8g2.firstPage();
    do {
      // Dessiner la fréquence
      displayfreq();
      // Dessiner le layout (lignes, step, bargraph…)
      layout();
    } while (u8g2.nextPage());
  }

  // Lecture S-mètre
  sgnalread();
}


// --- Programmation du Si5351 ---
void tunegen() {
  // En Hz × 100 pour le Si5351
  si5351.set_freq((freq + (interfreq * 1000ULL)) * 100ULL, SI5351_CLK0);
}

// -- Affichage de la fréquence --
void displayfreq() 
{
  // Variables pour séparer la partie "chiffres" et "unité"
  char freqDigits[16];
  //unsigned long freq = ...  (votre variable globale)

  // Détermination MHz/kHz/Hz
  unsigned long m = freq / 1000000UL;       // MHz
  unsigned long k = (freq % 1000000UL) / 1000;
  unsigned long h = freq % 1000UL;

  // Choix des coordonnées (exemple)
  int xPos = 5;   // x = Horizontale
  int yPos = 13;  // y = Verticale

  if (m < 1) {
    // Cas < 1 MHz => ex: "455.123" + "kHz"
    sprintf(freqDigits, "%03lu.%03lu", k, h);   // ex: "455.123"

    // 1) Impression de la partie numérique en "8bitclassic"
    u8g2.setFont(u8g2_font_8bitclassic_tr);
    u8g2.setCursor(xPos, yPos);
    u8g2.print(freqDigits);

    // 2) Récupérer la fin du curseur (position X)
    int dx = u8g2.getCursorX();  

    // 3) Basculer sur la petite police pour "kHz"
    u8g2.setFont(u8g2_font_5x8_tr);
    u8g2.setCursor(dx + 2, yPos + 2);  // +2 px de marge
    u8g2.print("kHz");
  }
  else if (m < 100) {
    // Cas < 100 MHz => ex: "7.200.000" + "MHz"
    sprintf(freqDigits, "%2lu.%03lu.%03lu", m, k, h);

    // Partie numérique
    u8g2.setFont(u8g2_font_8bitclassic_tr);
    u8g2.setCursor(0, yPos);
    u8g2.print(freqDigits);

    int dx = u8g2.getCursorX();
    // Unité "MHz" en police 5x8
    u8g2.setFont(u8g2_font_5x8_tr);
    u8g2.setCursor(dx + 2, yPos + 2);
    u8g2.print("MHz");
  }
  else {
    // Cas >= 100 MHz => ex: "100.000.00" + "MHz"
    unsigned long hh = (freq % 1000UL) / 10; 
    // ex: "100.000.00"
    sprintf(freqDigits, "%2lu.%03lu.%02lu", m, k, hh);

    // Partie numérique
    u8g2.setFont(u8g2_font_8bitclassic_tr);
    u8g2.setCursor(0, yPos);
    u8g2.print(freqDigits);

    int dx = u8g2.getCursorX();
    // Unité
    u8g2.setFont(u8g2_font_5x8_tr);
    u8g2.setCursor(dx + 2, yPos + 2);
    u8g2.print("MHz");
  }
}


// --- Sélection du pas de tuning ---
void setstep() {
  switch (stp) {
    case 1:  stp = 2; fstep = 1;       break; // 1 Hz
    case 2:  stp = 3; fstep = 10;      break; // 10 Hz
    case 3:  stp = 4; fstep = 1000;    break; // 1 kHz
    case 4:  stp = 5; fstep = 5000;    break; // 5 kHz
    case 5:  stp = 6; fstep = 10000;   break; // 10 kHz
    case 6:  stp = 1; fstep = 1000000; break; // 1 MHz
  }
}


// --- Incrément mémoire de bande ---
void inc_preset() {
  count++;
  if (count > 21) count = 1;
  bandpresets();
  delay(50);
}


// --- Sélection de bande (préréglages) ---
void bandpresets() {
  switch (count)  {
    case 1:  freq = 100000;    break;  // 100 kHz
    case 2:  freq = 800000;    break;
    case 3:  freq = 1800000;   break;
    case 4:  freq = 3650000;   break;
    case 5:  freq = 4985000;   break;
    case 6:  freq = 6180000;   break;
    case 7:  freq = 7200000;   break;  // 7.2 MHz
    case 8:  freq = 10000000;  break;
    case 9:  freq = 11780000;  break;
    case 10: freq = 13630000;  break;
    case 11: freq = 14100000;  break;  // 14.1 MHz
    case 12: freq = 15000000;  break;
    case 13: freq = 17655000;  break;
    case 14: freq = 21525000;  break;
    case 15: freq = 27015000;  break;
    case 16: freq = 28400000;  break;
    case 17: freq = 50000000;  break;  // 50 MHz
    case 18: freq = 100000000; break;  // 100 MHz
    case 19: freq = 130000000; break;  // 130 MHz
    case 20: freq = 144000000; break;  // 144 MHz
    case 21: freq = 220000000; break;  // 220 MHz
  }
  si5351.pll_reset(SI5351_PLLA);
  stp = 4;
  setstep();
}


// -- Layout principal (cadres, labels, step, etc.) --
void layout() {
  // Dessin des lignes
  // u8g2.drawLine(x1, y1, x2, y2);
  u8g2.drawLine(0, 20, 127, 20);   // barre horizontale
  u8g2.drawLine(0, 43, 127, 43);
  u8g2.drawLine(105, 24, 105, 39);
  u8g2.drawLine(87, 24, 87, 39);
  u8g2.drawLine(87, 48, 87, 63);
  u8g2.drawLine(15, 55, 82, 55);

  // STEP
  u8g2.setFont(u8g2_font_5x8_tr);
  u8g2.setCursor(59, 31);
  u8g2.print("STEP");

  u8g2.setCursor(54, 40);
  switch (stp) {
    case 2: u8g2.print("  1Hz");  break;
    case 3: u8g2.print(" 10Hz");  break;
    case 4: u8g2.print(" 1kHz");  break;
    case 5: u8g2.print(" 5kHz");  break;
    case 6: u8g2.print("10kHz");  break;
    case 1: u8g2.print(" 1MHz");  break;
  }

  // IF
  u8g2.setFont(u8g2_font_5x8_tr);
  u8g2.setCursor(90, 58);
  u8g2.print("IF:");
  u8g2.setCursor(106, 58);
  u8g2.print(interfreq);
  u8g2.print("k");

  // Indicateurs kHz / MHz / VFO / LO
  // On place quelques infos plus bas
  if (!sts) {
    // RX
    u8g2.setCursor(91, 36);
    u8g2.print("RX");
    interfreq = IF;
  } else {
    // TX
    u8g2.setCursor(91, 36);
    u8g2.print("TX");
    interfreq = 0;
  }

  // Indication "VFO" ou "L O" (LO) en dessous
  u8g2.setCursor(109, 36);
  if (interfreq == 0) u8g2.print("VFO");
  else                u8g2.print("LO:");

  // Nom de la bande
  bandlist();

  // Bargraphe "TU" + "SM"
  drawbargraph();
}


// -- Affichage de la bande (40m, 20m, etc.) --
void bandlist() {
  u8g2.setFont(u8g2_font_8bitclassic_tr);
  u8g2.setCursor(6, 36);
  switch (count) {
    case 1:  u8g2.print("GEN"); break;
    case 2:  u8g2.print("MW");  break;
    case 3:  u8g2.print("160m");break;
    case 4:  u8g2.print("80m"); break;
    case 5:  u8g2.print("60m"); break;
    case 6:  u8g2.print("49m"); break;
    case 7:  u8g2.print("40m"); break;
    case 8:  u8g2.print("31m"); break;
    case 9:  u8g2.print("25m"); break;
    case 10: u8g2.print("22m"); break;
    case 11: u8g2.print("20m"); break;
    case 12: u8g2.print("19m"); break;
    case 13: u8g2.print("16m"); break;
    case 14: u8g2.print("13m"); break;
    case 15: u8g2.print("11m"); break;
    case 16: u8g2.print("10m"); break;
    case 17: u8g2.print("6m");  break;
    case 18: u8g2.print("WFM"); break;
    case 19: u8g2.print("AIR"); break;
    case 20: u8g2.print("2m");  break;
    case 21: u8g2.print("1m");  break;
  }
  // Si GEN => interfreq=0 en RX
  if (count == 1) interfreq = 0; 
  else if (!sts)  interfreq = IF;
}


// --- Lecture du signal S-Meter ---
void sgnalread() {
  smval = analogRead(adc); 
  x = map(smval, 0, S_GAIN, 1, 14); 
  if (x > 14) x = 14;
}


// -- Bargraphe "TU" et "SM" --
void drawbargraph() {
  // "TU"
  u8g2.setFont(u8g2_font_5x8_tr);
  u8g2.setCursor(1, 54);
  u8g2.print("TU");

  // n (1..42) => y(1..14)
  byte y = map(n, 1, 42, 1, 14);
  // On dessine le bloc correspondant
  switch (y) {
    case 1:  u8g2.drawBox(15, 48, 2, 6);  break;
    case 2:  u8g2.drawBox(20, 48, 2, 6);  break;
    case 3:  u8g2.drawBox(25, 48, 2, 6);  break;
    case 4:  u8g2.drawBox(30, 48, 2, 6);  break;
    case 5:  u8g2.drawBox(35, 48, 2, 6);  break;
    case 6:  u8g2.drawBox(40, 48, 2, 6);  break;
    case 7:  u8g2.drawBox(45, 48, 2, 6);  break;
    case 8:  u8g2.drawBox(50, 48, 2, 6);  break;
    case 9:  u8g2.drawBox(55, 48, 2, 6);  break;
    case 10: u8g2.drawBox(60, 48, 2, 6);  break;
    case 11: u8g2.drawBox(65, 48, 2, 6);  break;
    case 12: u8g2.drawBox(70, 48, 2, 6);  break;
    case 13: u8g2.drawBox(75, 48, 2, 6);  break;
    case 14: u8g2.drawBox(80, 48, 2, 6);  break;
  }

  // "SM"
  u8g2.setCursor(1, 63);
  u8g2.print("SM");

  // x(1..14). On dessine de droite à gauche.
  // (Même logique que votre code original)
  switch (x) {
    case 14: u8g2.drawBox(80, 58, 2, 6);
    case 13: u8g2.drawBox(75, 58, 2, 6);
    case 12: u8g2.drawBox(70, 58, 2, 6);
    case 11: u8g2.drawBox(65, 58, 2, 6);
    case 10: u8g2.drawBox(60, 58, 2, 6);
    case 9:  u8g2.drawBox(55, 58, 2, 6);
    case 8:  u8g2.drawBox(50, 58, 2, 6);
    case 7:  u8g2.drawBox(45, 58, 2, 6);
    case 6:  u8g2.drawBox(40, 58, 2, 6);
    case 5:  u8g2.drawBox(35, 58, 2, 6);
    case 4:  u8g2.drawBox(30, 58, 2, 6);
    case 3:  u8g2.drawBox(25, 58, 2, 6);
    case 2:  u8g2.drawBox(20, 58, 2, 6);
    case 1:  u8g2.drawBox(15, 58, 2, 6);
  }
}


// -- Écran d’accueil (optionnel) --
void statup_text() {
  // En mode page buffer, on fait :
  u8g2.firstPage();
  do {
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.setCursor(13, 18);
    u8g2.print("Si5351 VFO/RF GEN");
    u8g2.setCursor(6, 40);
    u8g2.print("JCR RADIO - Ver 2.1");
  } while (u8g2.nextPage());
  delay(3000);
}
