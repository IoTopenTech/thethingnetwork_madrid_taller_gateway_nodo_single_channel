/*******************************************************************************
   Copyright (c) 2015 Thomas Telkamp and Matthijs Kooijman

   Permission is hereby granted, free of charge, to anyone
   obtaining a copy of this document and accompanying files,
   to do whatever they want with them without any restriction,
   including, but not limited to, copying, modification and redistribution.
   NO WARRANTY OF ANY KIND IS PROVIDED.

   This example sends a valid LoRaWAN packet with payload "Hello,
   world!", using frequency and encryption settings matching those of
   the The Things Network.

   This uses ABP (Activation-by-personalisation), where a DevAddr and
   Session keys are preconfigured (unlike OTAA, where a DevEUI and
   application key is configured, while the DevAddr and session keys are
   assigned/generated in the over-the-air-activation procedure).

   Note: LoRaWAN per sub-band duty-cycle limitation is enforced (1% in
   g1, 0.1% in g2), but not the TTN fair usage policy (which is probably
   violated by this sketch when left running for longer)!

   To use this sketch, first register your application and device with
   the things network, to set or generate a DevAddr, NwkSKey and
   AppSKey. Each device should have their own unique values for these
   fields.

   Do not forget to define the radio type correctly in config.h.

 *******************************************************************************/
#include <lmic.h>
#include <hal/hal.h>
#include <SPI.h>
#include <LowPower.h>
#include  "adcvcc.h"

#define NORMALINTERVAL 900 // 15 minutes (normal) 
int interval = NORMALINTERVAL;

byte LMIC_transmitted = 0;
int LMIC_event_Timeout = 0;


// LoRaWAN NwkSKey, network session key (formato MSB)
// This is the default Semtech key, which is used by the early prototype TTN
// network.
static const PROGMEM u1_t NWKSKEY[16] = { };

// LoRaWAN AppSKey, application session key (formato MSB)
// This is the default Semtech key, which is used by the early prototype TTN
// network.
static const u1_t PROGMEM APPSKEY[16] = { };

// LoRaWAN end-device address (DevAddr) (formato MSB)
static const u4_t DEVADDR = 0x0000 ; // <-- Change this address for every node!

// These callbacks are only used in over-the-air activation, so they are
// left empty here (we cannot leave them out completely unless
// DISABLE_JOIN is set in config.h, otherwise the linker will complain).
void os_getArtEui (u1_t* buf) { }
void os_getDevEui (u1_t* buf) { }
void os_getDevKey (u1_t* buf) { }

static uint8_t mydata[] = "Hello, world!";
static osjob_t sendjob;

// Schedule TX every this many seconds (might become longer due to duty
// cycle limitations).
const unsigned TX_INTERVAL = 60;

// Pin mapping
const lmic_pinmap lmic_pins = {
  .nss = 10,
  .rxtx = LMIC_UNUSED_PIN,
  .rst = 9,
  .dio = {2, 7, 8},
};




// Define the single channel and data rate (SF) to use
int channel = 0;
int dr = DR_SF7;

// Disables all channels, except for the one defined above, and sets the
// data rate (SF). This only affects uplinks; for downlinks the default
// channels or the configuration from the OTAA Join Accept are used.
//
// Not LoRaWAN compliant; FOR TESTING ONLY!
//
void forceTxSingleChannelDr() {
  for (int i = 0; i < 9; i++) { // For EU; for US use i<71
    if (i != channel) {
      LMIC_disableChannel(i);
    }
  }
  // Set data rate (SF) and transmit power for uplink
  LMIC_setDrTxpow(dr, 14);
}

/* ======================================================================
  Function: ADC_vect
  Purpose : IRQ Handler for ADC
  Input   : -
  Output  : -
  Comments: used for measuring 8 samples low power mode, ADC is then in
          free running mode for 8 samples
  ====================================================================== */
ISR(ADC_vect)
{
  // Increment ADC counter
  _adc_irq_cnt++;
}

void onEvent (ev_t ev) {
  Serial.print(os_getTime());
  Serial.print(": ");
  switch (ev) {
    case EV_SCAN_TIMEOUT:
      Serial.println(F("EV_SCAN_TIMEOUT"));
      break;
    case EV_BEACON_FOUND:
      Serial.println(F("EV_BEACON_FOUND"));
      break;
    case EV_BEACON_MISSED:
      Serial.println(F("EV_BEACON_MISSED"));
      break;
    case EV_BEACON_TRACKED:
      Serial.println(F("EV_BEACON_TRACKED"));
      break;
    case EV_JOINING:
      Serial.println(F("EV_JOINING"));
      break;
    case EV_JOINED:
      Serial.println(F("EV_JOINED"));
      break;
    case EV_RFU1:
      Serial.println(F("EV_RFU1"));
      break;
    case EV_JOIN_FAILED:
      Serial.println(F("EV_JOIN_FAILED"));
      break;
    case EV_REJOIN_FAILED:
      Serial.println(F("EV_REJOIN_FAILED"));
      break;
    case EV_TXCOMPLETE:
      Serial.println(F("EV_TXCOMPLETE (includes waiting for RX windows)"));
      if (LMIC.txrxFlags & TXRX_ACK)
        Serial.println(F("Received ack"));

      if (LMIC.dataLen) {
        Serial.println(F("Received "));
        Serial.println(LMIC.dataLen);
        Serial.println(F(" bytes of payload"));
      }

      /*
        if (LMIC.dataLen) { // data received in rx slot after tx
        fprintf(stdout, "Received %d bytes of payload: 0x", LMIC.dataLen);
        for (int i = 0; i < LMIC.dataLen; i++) {
          fprintf(stdout, "%02X", LMIC.frame[LMIC.dataBeg + i]);
        }
        fprintf(stdout, "\n");
        }
      */
      // Schedule next transmission
      //os_setTimedCallback(&sendjob, os_getTime() + sec2osticks(TX_INTERVAL), do_send);
      LMIC_transmitted = 1;
      break;
    case EV_LOST_TSYNC:
      Serial.println(F("EV_LOST_TSYNC"));
      break;
    case EV_RESET:
      Serial.println(F("EV_RESET"));
      break;
    case EV_RXCOMPLETE:
      // data received in ping slot
      Serial.println(F("EV_RXCOMPLETE"));
      break;
    case EV_LINK_DEAD:
      Serial.println(F("EV_LINK_DEAD"));
      break;
    case EV_LINK_ALIVE:
      Serial.println(F("EV_LINK_ALIVE"));
      break;
    default:
      Serial.println(F("Unknown event"));
      break;
  }
}

void do_send(osjob_t* j) {
  // Check if there is not a current TX/RX job running
  if (LMIC.opmode & OP_TXRXPEND) {
    Serial.println(F("OP_TXRXPEND, not sending"));
  } else {
    // Prepare upstream data transmission at the next possible time.
    int batt = (int)(readVcc() / 100);  // readVCC returns  mVolt need just 100mVolt steps
    byte batvalue = (byte)batt; // no problem putting it into a int.
    unsigned char carga_de_pago[4];
    carga_de_pago[0] = 0x03;
    carga_de_pago[1] = 0x02;
    int16_t help;
    help = batvalue * 10;
    carga_de_pago[2] = help >> 8;
    carga_de_pago[3] = help;

    LMIC_setTxData2(1, carga_de_pago, sizeof(carga_de_pago), 0);
    Serial.println(F("Packet queued"));
  }
  // Next TX is scheduled after TX_COMPLETE event.
}
void do_send_abierta(osjob_t* j) {
  // Check if there is not a current TX/RX job running
  if (LMIC.opmode & OP_TXRXPEND) {
    Serial.println(F("OP_TXRXPEND, not sending"));
  } else {
    // Prepare upstream data transmission at the next possible time.
    int batt = (int)(readVcc() / 100);  // readVCC returns  mVolt need just 100mVolt steps
    byte batvalue = (byte)batt; // no problem putting it into a int.
    unsigned char carga_de_pago[7];
    carga_de_pago[0] = 0x03;
    carga_de_pago[1] = 0x02;//Entrada analógica
    int16_t help;
    help = batvalue * 10;
    carga_de_pago[2] = help >> 8;
    carga_de_pago[3] = help;
    carga_de_pago[4] = 0x04;
    carga_de_pago[5] = 0x00;//Entrada digital
    carga_de_pago[6] = 0x01;

    LMIC_setTxData2(1, carga_de_pago, sizeof(carga_de_pago), 0);
    Serial.println(F("Packet queued"));
  }
}
void do_send_cerrada(osjob_t* j) {
  // Check if there is not a current TX/RX job running
  if (LMIC.opmode & OP_TXRXPEND) {
    Serial.println(F("OP_TXRXPEND, not sending"));
  } else {
    // Prepare upstream data transmission at the next possible time.
    int batt = (int)(readVcc() / 100);  // readVCC returns  mVolt need just 100mVolt steps
    byte batvalue = (byte)batt; // no problem putting it into a int.
    unsigned char carga_de_pago[7];
    carga_de_pago[0] = 0x03;
    carga_de_pago[1] = 0x02;//Entrada analógica
    int16_t help;
    help = batvalue * 10;
    carga_de_pago[2] = help >> 8;
    carga_de_pago[3] = help;
    carga_de_pago[4] = 0x04;
    carga_de_pago[5] = 0x00;//Entrada digital
    carga_de_pago[6] = 0x00;

    LMIC_setTxData2(1, carga_de_pago, sizeof(carga_de_pago), 0);
    Serial.println(F("Packet queued"));
  }
}
void puertaAbierta() {
  os_setCallback (&sendjob, do_send_abierta);
}
void puertaCerrada() {
  os_setCallback (&sendjob, do_send_cerrada);

}
bool bootFromBrownOut = false;

// shows the bootstatus on serial output and set bootFromBrownout flag.
void showBootStatus(uint8_t _mcusr)
{
  Serial.print(F("mcusr = "));
  Serial.print(_mcusr, HEX);
  Serial.print(F(" > "));
  if (_mcusr & (1 << WDRF))
  {
    Serial.print(F(" WDR"));
    _mcusr &= ~(1 << WDRF);
  }
  if (_mcusr & (1 << BORF))
  {
    Serial.print(F(" BOR"));
    _mcusr &= ~(1 << BORF);
    bootFromBrownOut = true;
  }
  if (_mcusr & (1 << EXTRF))
  {
    Serial.print(F(" EXTF"));
    _mcusr &= ~(1 << EXTRF);
  }
  if (_mcusr & (1 << PORF))
  {
    Serial.print(F(" POR"));
    _mcusr &= ~(1 << PORF);
  }
  if (_mcusr != 0x00)
  {
    // It should never enter here
    Serial.print(F(" ??"));
  }
  Serial.println("");
}
void setup() {
  uint8_t mcusr = MCUSR;
  MCUSR = 0;

  Serial.begin(115200);
  Serial.println(F("Boot"));

  showBootStatus(mcusr);


  pinMode(3, INPUT_PULLUP);

  // LMIC init
  os_init();
  // Reset the MAC state. Session and pending data transfers will be discarded.
  LMIC_reset();

  // Set static session parameters. Instead of dynamically establishing a session
  // by joining the network, precomputed session parameters are be provided.
#ifdef PROGMEM
  // On AVR, these values are stored in flash and only copied to RAM
  // once. Copy them to a temporary buffer here, LMIC_setSession will
  // copy them into a buffer of its own again.
  uint8_t appskey[sizeof(APPSKEY)];
  uint8_t nwkskey[sizeof(NWKSKEY)];
  memcpy_P(appskey, APPSKEY, sizeof(APPSKEY));
  memcpy_P(nwkskey, NWKSKEY, sizeof(NWKSKEY));
  LMIC_setSession (0x1, DEVADDR, nwkskey, appskey);
#else
  // If not running an AVR with PROGMEM, just use the arrays directly
  LMIC_setSession (0x1, DEVADDR, NWKSKEY, APPSKEY);
#endif

#if defined(CFG_eu868)
  // Set up the channels used by the Things Network, which corresponds
  // to the defaults of most gateways. Without this, only three base
  // channels from the LoRaWAN specification are used, which certainly
  // works, so it is good for debugging, but can overload those
  // frequencies, so be sure to configure the full frequency range of
  // your network here (unless your network autoconfigures them).
  // Setting up channels should happen after LMIC_setSession, as that
  // configures the minimal channel set.
  // NA-US channels 0-71 are configured automatically
  LMIC_setupChannel(0, 868100000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_CENTI);      // g-band
  LMIC_setupChannel(1, 868300000, DR_RANGE_MAP(DR_SF12, DR_SF7B), BAND_CENTI);      // g-band
  LMIC_setupChannel(2, 868500000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_CENTI);      // g-band
  LMIC_setupChannel(3, 867100000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_CENTI);      // g-band
  LMIC_setupChannel(4, 867300000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_CENTI);      // g-band
  LMIC_setupChannel(5, 867500000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_CENTI);      // g-band
  LMIC_setupChannel(6, 867700000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_CENTI);      // g-band
  LMIC_setupChannel(7, 867900000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_CENTI);      // g-band
  LMIC_setupChannel(8, 868800000, DR_RANGE_MAP(DR_FSK,  DR_FSK),  BAND_MILLI);      // g2-band
  // TTN defines an additional channel at 869.525Mhz using SF9 for class B
  // devices' ping slots. LMIC does not have an easy way to define set this
  // frequency and support for class B is spotty and untested, so this
  // frequency is not configured here.
#elif defined(CFG_us915)
  // NA-US channels 0-71 are configured automatically
  // but only one group of 8 should (a subband) should be active
  // TTN recommends the second sub band, 1 in a zero based count.
  // https://github.com/TheThingsNetwork/gateway-conf/blob/master/US-global_conf.json
  LMIC_selectSubBand(1);
#endif

  // Disable link check validation
  LMIC_setLinkCheckMode(0);

  // TTN uses SF9 for its RX2 window.
  LMIC.dn2Dr = DR_SF9;

  // Set data rate and transmit power for uplink (note: txpow seems to be ignored by the library)
  //LMIC_setDrTxpow(DR_SF7,14);

  forceTxSingleChannelDr();
  LMIC_setClockError(MAX_CLOCK_ERROR * 1 / 100);

}

void loop() {
  // Allow wake up pin to trigger interrupt on low.
  //usar uno u otro en función del tipo de reed (mejor el segundo porque consume menos)
  //attachInterrupt(digitalPinToInterrupt(3), puertaAbierta, RISING);
  attachInterrupt(digitalPinToInterrupt(3), puertaAbierta, FALLING);

  // Enter power down state with ADC disabled and BOD enabled.
  // Wake up when wake up pin is rising.
  Serial.println("A dormir");
  delay(100);
  LowPower.powerDown(SLEEP_FOREVER, ADC_OFF, BOD_ON);
  Serial.println("Puerta abierta");
  // Disable external pin interrupt on wake up pin.
  detachInterrupt(0);

  // Do something here
  if (!bootFromBrownOut)  {
    // Wait for response of the queued message (check if message is send correctly)
    os_runloop_once();
    // Continue until message is transmitted correctly
    LMIC_event_Timeout = 60 * 100; // 60 * 100 times 10mSec = 60 seconds
    while (LMIC_transmitted != 1)    {
      os_runloop_once();
      // Add timeout counter when nothing happens:
      delay(10);
      if (LMIC_event_Timeout-- == 0)      {
        // Timeout when there's no "EV_TXCOMPLETE" event after 60 seconds
        Serial.println(F("ETO, msg not tx"));
        break;
      }
    }
    LMIC_transmitted = 0;
    //Espero a que se cierre la puerta
    //while (digitalRead(3) == 1) {}
    while (digitalRead(3) == 0) {}
    Serial.println("Puerta cerada");
    puertaCerrada();
    os_runloop_once();
    // Continue until message is transmitted correctly
    LMIC_event_Timeout = 60 * 100; // 60 * 100 times 10mSec = 60 seconds
    while (LMIC_transmitted != 1)    {
      os_runloop_once();
      // Add timeout counter when nothing happens:
      delay(10);
      if (LMIC_event_Timeout-- == 0)      {
        // Timeout when there's no "EV_TXCOMPLETE" event after 60 seconds
        Serial.println(F("ETO, msg not tx"));
        break;
      }
    }
    LMIC_transmitted = 0;
  }  else  {
    Serial.println("X");
  }
  bootFromBrownOut = false;
  /*
    for (int i = 0; i < interval; i++)
    {
      i += 8 ; // no normal 1 second run but 8 second loops m.
      // Enter power down state for 8 s with ADC and BOD module enabled
      LowPower.powerDown(SLEEP_8S, ADC_OFF, BOD_ON);
    }
  */
  Serial.println("-");
}
