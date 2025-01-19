/**********************************************************************************************************
  10kHz to 225MHz VFO / RF Generator with Si5351 and Arduino Nano, 
  avec décalage de fréquence FI (+ ou -), sélecteur RX/TX pour transceivers QRP,
  mémoires de bandes et bargraphe S-Meter.
  Basé sur le code original de J. CesarSound - ver 2.0 - Feb/2021.
  Adaptation pour ST7920 (128x64) + U8g2 par [votre nom/alias].
***********************************************************************************************************/
/*
U8G2_ST7920_128X64_ F _SW_SPI(rotation, clock, data, cs [, reset])
Display Pin	Arduino Pin Number
E, Clock	        13
RW, Data, MOSI	  11
RS, Chip Select	  10
RST, Reset	      8
*//

// -------------------------------------------------------------------------------------
//  Librairies
// -------------------------------------------------------------------------------------
#include <Arduino.h>
#include <Wire.h>                 // Librairie I2C standard
#include "Rotary.h"               // Ben Buxton https://github.com/brianlow/Rotary
#include "si5351.h"               // Etherkit https://github.com/etherkit/Si5351Arduino

// ---- Librairie U8g2 pour l'écran ST7920 ----
#include <U8g2lib.h>

// -------------------------------------------------------------------------------------
//  Définition du pilote U8g2 (ST7920 128x64)
// -------------------------------------------------------------------------------------
// Exemple en mode SPI logiciel (SW SPI) :
//    U8G2_R0         = pas de rotation (orientation standard)
//    clock = 13, data = 11, cs = 10  (à adapter selon votre câblage !)
//    reset = U8X8_PIN_NONE si vous n'avez pas de reset dédié
// -------------------------------------------------------------------------------------
U8G2_ST7920_128X64_F_SW_SPI u8g2( 
  U8G2_R0,   // rotation
  /* E, Clock=*/ 13, 
  /* RW, Data, MOSI=*/  11, 
  /* RS, Chip Select=*/    10, 
  /* RST, Reset=*/ U8X8_PIN_NONE 
);

// -------------------------------------------------------------------------------------
//  Paramètres utilisateur
// -------------------------------------------------------------------------------------
#define IF         455       // Fréquence FI (455kHz, 10.7MHz, etc.) - 0 si pas de FI
#define BAND_INIT  7         // Mémoire de bande initiale au démarrage
#define XT_CAL_F   33000     // Calibration XO (pour ajuster la fréquence de référence du Si5351)
#define S_GAIN     303       // Sensibilité de l’entrée S-Meter : ajuster selon votre tension max
#define tunestep   A0        // Pin du bouton permettant de changer la résolution de tuning
#define band       A1        // Pin du bouton permettant de changer de bande
#define rx_tx      A2        // Pin du sélecteur RX/TX (RX = contact ouvert, TX = GND)
#define adc        A3        // Pin pour l’entrée analogique du S-Meter

// -------------------------------------------------------------------------------------
//  Objets / Variables globales
// -------------------------------------------------------------------------------------
Rotary r = Rotary(2, 3);
Si5351 si5351(0x60); // Adresse I2C du Si5351

unsigned long freq, freqold, fstep;
long interfreq = IF, interfreqold = 0;
long cal = XT_CAL_F;
unsigned int smval;
byte encoder = 1;
byte stp, n = 1;
byte count, x, xo;
bool sts = 0;                  // 0 = RX, 1 = TX
unsigned int period = 100;     // Période de rafraîchissement d'affichage
unsigned long time_now = 0;

// -------------------------------------------------------------------------------------
//  Interruption sur les broches de l'encodeur
// -------------------------------------------------------------------------------------
ISR(PCINT2_vect) {
  char result = r.process();
  if (result == DIR_CW) set_frequency(1);
  else if (result == DIR_CCW) set_frequency(-1);
}

// -------------------------------------------------------------------------------------
//  Fonctions
// -------------------------------------------------------------------------------------
void set_frequency(short dir) {
  if (encoder == 1) { // Variation Up/Down de freq
    if (dir == 1) freq = freq + fstep;
    if (freq >= 225000000UL) freq = 225000000UL;
    if (dir == -1) freq = freq - fstep;
    if (fstep == 1000000UL && freq <= 1000000UL) freq = 1000000UL;
    else if (freq < 10000UL) freq = 10000UL;
  }
  // Gestion du "curseur" n (bargraphe "TU") Up/Down graph tune pointer
  if (encoder == 1) {
    if (dir == 1) n++;
    if (n > 42) n = 1;
    if (dir == -1) n--;
    if (n < 1) n = 42;
  }
}

// -------------------------------------------------------------------------------------
//  Setup
// -------------------------------------------------------------------------------------
void setup() {
  Wire.begin();

  // Initialisation U8g2
  // (Le backlight du ST7920 doit être alimenté séparément si nécessaire)
  u8g2.begin();
  u8g2.clearBuffer();
  u8g2.sendBuffer();  // on envoie l’effacement

  pinMode(2, INPUT_PULLUP);
  pinMode(3, INPUT_PULLUP);
  pinMode(tunestep, INPUT_PULLUP);
  pinMode(band, INPUT_PULLUP);
  pinMode(rx_tx, INPUT_PULLUP);

  //statup_text();  // Décommentez si vous voulez l'écran d'accueil

  si5351.init(SI5351_CRYSTAL_LOAD_8PF, 0, 0);
  si5351.set_correction(cal, SI5351_PLL_INPUT_XO);
  si5351.drive_strength(SI5351_CLK0, SI5351_DRIVE_8MA);
  si5351.output_enable(SI5351_CLK0, 1); // 1 - Enable / 0 - Disable CLK
  si5351.output_enable(SI5351_CLK1, 0);
  si5351.output_enable(SI5351_CLK2, 0);

  // Activation de l’interruption sur l’encodeur
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
  // Mise à jour si le S-mètre a changé
  if (xo != x) {
    time_now = millis();
    xo = x;
  }

  // Bouton de changement de pas de tuning
  if (digitalRead(tunestep) == LOW) {
    time_now = millis() + 300;
    setstep();
    delay(300);
  }

  // Bouton de changement de bande
  if (digitalRead(band) == LOW) {
    time_now = millis() + 300;
    inc_preset();
    delay(300);
  }

  // Commutateur RX/TX
  if (digitalRead(rx_tx) == LOW) {
    time_now = millis() + 300;
    sts = 1;   // TX
  } else {
    sts = 0;   // RX
  }

  // Rafraîchissement de l’affichage
  if ((time_now + period) > millis()) {
    u8g2.clearBuffer(); // On efface le buffer de l’écran
    displayfreq();      // On dessine la fréquence
    layout();           // On dessine le “cadre” (layout + bargraph + etc.)
    u8g2.sendBuffer();  // On envoie tout à l’écran
  }
  // Lecture du S-mètre
  sgnalread();
}

void tunegen() {
  // On règle la sortie CLK0 du Si5351 sur (freq + FI) en kHz × 100
  si5351.set_freq((freq + (interfreq * 1000ULL)) * 100ULL, SI5351_CLK0);
}

void displayfreq() {
  // Conversion en MHz / kHz / Hz
  unsigned long m = freq / 1000000UL;           // MHz
  unsigned long k = (freq % 1000000UL) / 1000;  // kHz
  unsigned long h = freq % 1000UL;             	// Hz

  // Pour simuler la "taille 2" de l'OLED original, on utilise une police plus grande.
  // Ici, on prépare la chaîne formatée.
  char buffer[20];

  // On distingue plusieurs cas (comme dans votre code d’origine) :
  //   - freq < 1 MHz
  //   - freq < 100 MHz
  //   - freq >= 100 MHz
  // et on formate différemment.
  if (m < 1) {
    // < 1 MHz : ex: 455.123
    // On place le texte à (x=41, y=1) dans l’original, mais 
    // la position change en U8g2 car l’origine du texte est la ligne de base.
    //sprintf(buffer, "%03lu.%03lu", k, h);
    // Ici, on dessine plus grand, donc on ajuste la position
    //drawLargeText(40, 20, buffer);
    u8g2.setCursor(41, 1); sprintf(buffer, "%003d.%003d", k, h);
  } 
  else if (m < 100) {
    // < 100 MHz : ex: 7.200.000
    //sprintf(buffer, "%2lu.%03lu.%03lu", m, k, h);
    //drawLargeText(5, 20, buffer);
    u8g2.setCursor(5, 1); sprintf(buffer, "%2d.%003d.%003d", m, k, h);
  } 
  else {
    // >= 100 MHz : ex: 100.000.00
    // (Remarque : on a un petit ajustement sur les Hz)
    unsigned long hh = (freq % 1000UL) / 10; // deux décimales
    //sprintf(buffer, "%2lu.%03lu.%02lu", m, k, hh);
    //drawLargeText(5, 20, buffer);
    u8g2.setCursor(5, 1); sprintf(buffer, "%2d.%003d.%02d", m, k, h);
  }
}

void setstep() {
  switch (stp) {
    case 1: stp = 2; fstep = 1;       break; // 1 Hz
    case 2: stp = 3; fstep = 10;      break; // 10 Hz
    case 3: stp = 4; fstep = 1000;    break; // 1 kHz
    case 4: stp = 5; fstep = 5000;    break; // 5 kHz
    case 5: stp = 6; fstep = 10000;   break; // 10 kHz
    case 6: stp = 1; fstep = 1000000; break; // 1 MHz
  }
}

void inc_preset() {
  count++;
  if (count > 21) count = 1;
  bandpresets();
  delay(50);
}

void bandpresets() {
  switch (count)  {
    case 1: freq = 100000;    break;  // 100 kHz
    case 2: freq = 800000;    break;  // 800 kHz
    case 3: freq = 1800000;   break;  // 1.8 MHz
    case 4: freq = 3650000;   break;  
    case 5: freq = 4985000;   break;
    case 6: freq = 6180000;   break;
    case 7: freq = 7200000;   break;  // 7.2 MHz
    case 8: freq = 10000000;  break;
    case 9: freq = 11780000;  break;
    case 10: freq = 13630000; break;
    case 11: freq = 14100000; break;  // 14.1 MHz
    case 12: freq = 15000000; break;
    case 13: freq = 17655000; break;
    case 14: freq = 21525000; break;
    case 15: freq = 27015000; break;
    case 16: freq = 28400000; break;
    case 17: freq = 50000000; break;  // 50 MHz
    case 18: freq = 100000000;break;  // 100 MHz
    case 19: freq = 130000000;break;  // 130 MHz
    case 20: freq = 144000000;break;  // 144 MHz
    case 21: freq = 220000000;break;  // 220 MHz
  }
  si5351.pll_reset(SI5351_PLLA);
  stp = 4;
  setstep();
}

void layout() {
  // L'écran ST7920 fait 128×64, comme l’OLED SSD1306 128×64,
  // donc on peut garder les mêmes coordonnées, 
  // mais attention aux polices textuelles plus grandes.

  // ----------- Lignes horizontales -----------
  // drawLine(x1, y1, x2, y2)
  u8g2.drawLine(0, 20, 127, 20);   // ligne en haut (sous la fréquence)
  u8g2.drawLine(0, 43, 127, 43);   // ligne
  // ----------- Lignes verticales -------------
  u8g2.drawLine(105, 24, 105, 39);
  u8g2.drawLine(87, 24, 87, 39);
  u8g2.drawLine(87, 48, 87, 63);
  // ----------- Ligne horizontale bargraph ----
  u8g2.drawLine(15, 55, 82, 55);

  // -- STEP --
  u8g2.setFont(u8g2_font_6x10_tr);  // Petite police
  u8g2.setCursor(59, 33); // position du texte "STEP"
  u8g2.print("STEP");
  
  // Affichage de la valeur du step
  u8g2.setCursor(54, 42);
  if (stp == 2) u8g2.print("  1Hz");
  if (stp == 3) u8g2.print(" 10Hz");
  if (stp == 4) u8g2.print(" 1kHz");
  if (stp == 5) u8g2.print(" 5kHz");
  if (stp == 6) u8g2.print("10kHz");
  if (stp == 1) u8g2.print(" 1MHz");

  // -- IF --
  u8g2.setCursor(92, 48);
  u8g2.print("IF:");
  u8g2.setCursor(92, 57);
  u8g2.print(interfreq);
  u8g2.print("k");

  // -- kHz / MHz / RX-TX --
  u8g2.setCursor(110, 33);
  if (freq < 1000000UL) u8g2.print("kHz");
  else                  u8g2.print("MHz");

  u8g2.setCursor(110, 43);
  if (interfreq == 0) u8g2.print("VFO");
  else                u8g2.print("L O");

  u8g2.setCursor(91, 28);
  // if (!sts) display.print("RX"); if (!sts) interfreq = IF;
  // if (sts) display.print("TX"); if (sts) interfreq = 0;
  if (!sts) {
    u8g2.print("RX"); interfreq = IF;
	} else {
    u8g2.print("TX"); interfreq = 0;
  }

  // Nom de la bande (ex: "40m", "20m", etc.)
  bandlist();

  // Bargraph "TU" (pour le curseur d’accord) et "SM" (S-Meter)
  drawbargraph();
}

void bandlist() {
  // Police un peu plus grande pour la bande
  u8g2.setFont(u8g2_font_6x10_tr);
  // On la place à (0, 38) par exemple
  u8g2.setCursor(0, 38);

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

  // Si bande = 1 => GEN => interfreq = 0 (pas de FI)
  // if (count == 1) interfreq = 0; else if (!sts) interfreq = IF;
  if (count == 1) interfreq = 0; 
  else if (!sts)  interfreq = IF;
}

void sgnalread() {
  smval = analogRead(adc); 
  x = map(smval, 0, S_GAIN, 1, 14); 
  if (x > 14) x = 14;
}


void drawbargraph() 
{
  // On convertit la valeur 'n' (1..42) en un index de 1..14
  byte y = map(n, 1, 42, 1, 14);

  // Choisir une petite police pour l'affichage texte
  u8g2.setFont(u8g2_font_6x10_tr);

  // --- Affichage du label "TU" ---
  //   Remarque : l'origine du texte est la baseline sous U8g2.
  //   Donc "0,48" peut être à ajuster légèrement si le texte
  //   semble trop bas ou trop haut.
  u8g2.setCursor(0, 48);
  u8g2.print("TU");

  // --- Curseur de tuning (pointer) ---
  //   Équivalent à display.fillRect( X, Y, W, H, WHITE ) => u8g2.drawBox(X, Y, W, H).
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

  // --- Affichage du label "SM" ---
  u8g2.setCursor(0, 57);
  u8g2.print("SM");

  // --- Bargraphe S-Meter ---
  //   Même logique : on dessine des rectangles successifs
  //   en fonction de la valeur 'x' (1..14).
  //   Le `case` s’enchaîne, comme dans votre code original.
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

/*
// -------------------------------------------------------------------------------------
//  Affichage (remplace l'Adafruit_SSD1306 par U8g2)
// -------------------------------------------------------------------------------------

// -- Petit utilitaire pour dessiner du texte en "taille 2" --
//   Avec U8g2 on utilise des polices plus grandes ou plus petites
//   plutôt que setTextSize(). Adaptez-les selon le rendu désiré.
//
//   Par exemple : u8g2_font_6x10_tr pour du texte standard petit,
//                 u8g2_font_helvB14_tf ou autre pour du plus grand.
//   Voir : https://github.com/olikraus/u8g2/wiki/fntlistall
// 
void drawLargeText(uint8_t x, uint8_t y, const char *msg) {
  // Exemple : police 14 pixels de haut
  u8g2.setFont(u8g2_font_helvB14_tf);
  u8g2.drawStr(x, y, msg);
  // On peut revenir à une police plus petite ensuite si besoin
  u8g2.setFont(u8g2_font_6x10_tf);
}

void drawbargraph() {
  // "TU" = bargraphe d’accord
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.setCursor(0, 48);
  u8g2.print("TU");

  // On mappe n (1..42) sur 1..14 cases
  byte y = map(n, 1, 42, 1, 14);

  // On trace un petit rectangle pour la position
  // Coordonnées de départ : (15, 48), dimension 2×6
  int xpos_tu = 15 + (y - 1)*5;  // chaque "barre" fait 5 px d'écart environ
  u8g2.drawBox(xpos_tu, 48, 2, 6);

  // "SM" = bargraphe S-mètre
  u8g2.setCursor(0, 59);
  u8g2.print("SM");

  // x (1..14)
  // On dessine un bloc de la gauche vers la droite pour chaque niveau
  for (int i = 1; i <= x; i++) {
    int xpos_sm = 15 + (i - 1)*5; 
    u8g2.drawBox(xpos_sm, 58, 2, 6);
  }
}
*/

// Optionnel, si vous voulez un écran d’accueil au démarrage
void statup_text() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tr);
  
  u8g2.setCursor(13, 18);
  u8g2.print("Si5351 VFO/RF GEN");
  u8g2.setCursor(6, 40);
  u8g2.print("JCR RADIO - Ver 2.0");

  u8g2.sendBuffer();
  delay(2000);
}
