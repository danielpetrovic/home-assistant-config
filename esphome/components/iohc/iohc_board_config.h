#pragma once

// Board pin configuration for LilyGO TTGO T3 LoRa32 868MHz V1.6.1 - renamed
// from upstream's "board-config.h" (too generic a name to risk colliding
// with another library's own header once everything sits flat in one
// ESPHome external_component directory - see iohc.h for why it's flat at
// all). Content is NOT copied from upstream's file's LILYGO block, which
// lists RADIO_RST_PIN=14. That's wrong for this exact board: GPIO23 is confirmed
// correct two ways - (1) the official schematic
// (Xinyuan-LilyGO/LilyGo-LoRa-Series/schematic/T3_V1.6.1.pdf) explicitly
// labels "IO23=RESET", and (2) empirically, reading the SX1276 version
// register via GPIO23 as reset returns the correct 0x12 on real hardware.
// DIO0/DIO1/DIO2 do match upstream's LILYGO block and our own schematic
// read/hands-on confirmation (DIO0=26 used by the RTS bridge's sibling
// board, DIO2=32 already proven working for RTS).
//
// Protocol constants (preamble, sync bytes, channel plan) are unchanged from
// upstream - those are properties of io-homecontrol itself, not the board.

#define RADIO_SX127X
#define Regulatory_Domain_EU_868

#define RADIO_SCLK_PIN 5
#define RADIO_MISO_PIN 19
#define RADIO_MOSI_PIN 27
#define RADIO_CS_PIN 18
#define RADIO_RST_PIN 23
#define RADIO_DIO0_PIN 26
#define RADIO_DIO1_PIN 33
#define RADIO_DIO2_PIN 32
#define BOARD_LED_PIN 25

#define I2C_SDA_PIN 21
#define I2C_SCL_PIN 22
#define DISPLAY_OLED_RST_PIN -1

#define RADIO_MOSI RADIO_MOSI_PIN
#define RADIO_MISO RADIO_MISO_PIN
#define RADIO_SCLK RADIO_SCLK_PIN
#define RADIO_RESET RADIO_RST_PIN
#define RADIO_NSS RADIO_CS_PIN

#define RADIO_DIO_0 RADIO_DIO0_PIN
#define RADIO_DIO_4 RADIO_DIO2_PIN

#define RADIO_PACKET_AVAIL RADIO_DIO_0   // Packet Received / CRC ok from Radio
#define RADIO_PREAMBLE_DETECTED RADIO_DIO_4  // Preamble detected from Radio

#define SPI_CLK_FRQ 10000000

#define BOARD_TCXO_WAKEUP_TIME 0
#define BOARD_READY_AFTER_POR 10000

#define PREAMBLE_MSB 0x00
#define PREAMBLE_LSB 52  // ~13.5ms of 0xAA preamble

#define SYNC_BYTE_1 0xff
#define SYNC_BYTE_2 0x33

#define CHANNEL1 868250000  // 2W
#define CHANNEL2 868950000  // 1W 2W
#define CHANNEL3 869850000  // 2W

#define FREQS2SCAN {CHANNEL2, CHANNEL1, CHANNEL3}
// iohcRadio's own ISR-driven auto-hop (tickerCounter()/num_freqs) stays
// permanently disabled - it's a genuine hardware interrupt and can preempt
// mid-instruction, which would race against the retune()-then-send()
// sequence every TX call site relies on. 3-channel RX coverage (Finding 31)
// instead comes from esphome::iohc::IOHCComponent::maybe_hop_(), a
// cooperative (non-interrupt) hop checked every loop() tick - see that
// function's own comment for why this is the safe way to do it.
#define MAX_FREQS 1

#define SCAN_LED BOARD_LED_PIN
#define RX_LED SCAN_LED
