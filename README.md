# 10kHz-to-225MHz-VFO-RF-Generator-with-Si5351---Version-2.1
  10kHz to 225MHz VFO / RF Generator with Si5351 and Arduino Nano   + IF offset (+/-), RX/TX selector, mémoires de bandes, et bargraphe S-Meter.,    sélecteur RX/TX pour transceivers QRP,   [* Basé sur le code original de J. CesarSound - ver 2.0 - Feb/2021 *]    Adaptation pour ST7920 (128x64) + U8g2 (page buffer) par [ZelTroN2k3] 18.01.2025.

# Story
This is a project of a VFO (variable frequency oscillator) for use in DIY homebrew radio equipement such as Superheterodyne Single / Double Conversion Receivers, DCR, SDR or Ham QRP Transceivers. It has a Bargraph indicator for Signal Strenght (S-Meter) and 20 Band presets. Can be used as RF/Clock generator too. This is the new version (V.2), I updated the previous project and it includes new features.

# Features:

Operation range from 10kHz to 225MHz.
Tuning steps of 1Hz, 10Hz, 1kHz, 5kHz, 10kHz and 1MHz.
Intermediate Frequency (IF) offset (+ or -) adjustable.
20 Band Presets (shortcuts) to the BCB and HAM frequencies.
Generator funcion mode.
RX / TX Mode Selector for use in Homebrew QRP Transceivers.
Bargraph for Signal Meter through the analog input (ADC).
For use as Local Oscillator on Homebrew radio receivers such as Superheterodyne, SDR, Direct Conversion and Homebrew QRP Transceivers.
For use in Double Conversion / Air Band Superhet Receivers in conjunction with Si4735 or Si4732 DSP radio chip.
For use as a simple RF/Clock generator for calibration reference or clock generation.
Works with Arduino Uno, Nano and Pro Mini.
Uses a common 128x64 I2C OLED SSD1306 display and Si5351 module.
I2C data transfer, only 2 wires to connect the display / Si5351 and arduino.
High stability and precision for frequency generation.
Simple yet very efficient and free.
Update Jun, 2022: added an Alternative Version with support to bigger OLED SH1106 1.3" display and CW Keying input, see more details on text.
