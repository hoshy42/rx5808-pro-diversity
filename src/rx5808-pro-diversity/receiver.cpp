#include <Arduino.h>
#include <avr/pgmspace.h>

#include "settings.h"
#include "settings_eeprom.h"
#include "receiver.h"
#include "receiver_spi.h"
#include "channels.h"

#include "timer.h"

static void updateRssiLimits();
static void writeSerialData();


namespace Receiver {
    uint8_t activeReceiver = RECEIVER_A;
    uint8_t activeChannel = 0;

    uint8_t rssiA = 0;
    uint16_t rssiARaw = 0;
    uint8_t rssiALast[RECEIVER_LAST_DATA_SIZE] = { 0 };
    #ifdef USE_DIVERSITY
        uint8_t rssiB = 0;
        uint16_t rssiBRaw = 0;
        uint8_t rssiBLast[RECEIVER_LAST_DATA_SIZE] = { 0 };
    #endif

    uint32_t lastChannelSwitchTime = 0;
    static Timer rssiLogTimer = Timer(RECEIVER_LAST_DELAY);
    #ifdef USE_SERIAL_OUT
        static Timer serialLogTimer = Timer(25);
    #endif


    void setChannel(uint8_t channel)
    {
        ReceiverSpi::setSynthRegisterB(Channels::getSynthRegisterB(channel));

        lastChannelSwitchTime = millis();
        activeChannel = channel;
    }

    void setActiveReceiver(uint8_t receiver) {
        #ifdef USE_DIVERSITY
            digitalWrite(PIN_LED_A, receiver == RECEIVER_A);
            digitalWrite(PIN_LED_B, receiver == RECEIVER_B);
        #else
            digitalWrite(PIN_LED_A, HIGH);
        #endif

        activeReceiver = receiver;
    }

    //
    // Blocks until MIN_TUNE_TIME has been reached since last channel switch.
    //
    void waitForStableRssi() {
        uint16_t timeSinceChannelSwitch = millis() - lastChannelSwitchTime;
        if (timeSinceChannelSwitch < MIN_TUNE_TIME) {
            delay(MIN_TUNE_TIME - timeSinceChannelSwitch);
        }
    }

    uint16_t updateRssi() {
        waitForStableRssi();

        analogRead(PIN_RSSI_A); // Fake read to let ADC settle.
        rssiARaw = analogRead(PIN_RSSI_A);
        #ifdef USE_DIVERSITY
            analogRead(PIN_RSSI_B);
            rssiBRaw = analogRead(PIN_RSSI_B);
        #endif

        rssiA = constrain(
            map(
                rssiARaw,
                EepromSettings.rssiAMin,
                EepromSettings.rssiAMax,
                0,
                100
            ),
            0,
            100
        );
        #ifdef USE_DIVERSITY
            rssiB = constrain(
                map(
                    rssiARaw,
                    EepromSettings.rssiBMin,
                    EepromSettings.rssiBMax,
                    0,
                    100
                ),
                0,
                100
            );
        #endif

        if (rssiLogTimer.hasTicked()) {
            for (uint8_t i = 0; i < RECEIVER_LAST_DATA_SIZE - 1; i++) {
                rssiALast[i] = rssiALast[i + 1];
                #ifdef USE_DIVERSITY
                    rssiBLast[i] = rssiBLast[i + 1];
                #endif
            }

            rssiALast[RECEIVER_LAST_DATA_SIZE - 1] = rssiA;
            #ifdef USE_DIVERSITY
                rssiBLast[RECEIVER_LAST_DATA_SIZE - 1] = rssiB;
            #endif

            rssiLogTimer.reset();
        }
    }

    void setDiversityMode(uint8_t mode) {
        EepromSettings.diversityMode = mode;
        switchDiversity();
    }

    void switchDiversity() {
        static uint8_t diversityCheckTick = 0;
        uint8_t bestReceiver = activeReceiver;

        if (EepromSettings.diversityMode == DIVERSITY_AUTO) {
            uint8_t rssiDiff =
                (int) abs(((rssiA - rssiB) / (float) rssiB) * 100.0f);

            if (rssiDiff >= DIVERSITY_CUTOVER) {
                if(rssiA > rssiB && diversityCheckTick > 0)
                    diversityCheckTick--;

                if(rssiA < rssiB && diversityCheckTick < DIVERSITY_MAX_CHECKS)
                    diversityCheckTick++;

                // Have we reached the maximum number of checks to switch
                // receivers?
                if (diversityCheckTick == 0 ||
                    diversityCheckTick >= DIVERSITY_MAX_CHECKS
                ) {
                    bestReceiver =
                        (diversityCheckTick == 0) ?
                        RECEIVER_A :
                        RECEIVER_B;
                }
            }
        } else {
            switch (EepromSettings.diversityMode) {
                case DIVERSITY_FORCE_A:
                    bestReceiver = RECEIVER_A;
                    break;

                case DIVERSITY_FORCE_B:
                    bestReceiver = RECEIVER_B;
                    break;
            }
        }

        setActiveReceiver(bestReceiver);
    }

    void setup() {
        #ifdef DISABLE_AUDIO
            ReceiverSpi::setPowerDownRegister(0b00010000110111110011);
        #endif
    }

    void update() {
        updateRssi();

        #ifdef USE_SERIAL_OUT
            writeSerialData();
        #endif

        #ifdef USE_DIVERSITY
            switchDiversity();
        #endif
    }
}


#ifdef USE_SERIAL_OUT

#include "pstr_helper.h"

static void writeSerialData() {
    if (Receiver::serialLogTimer.hasTicked()) {
        Serial.print(Receiver::activeChannel, DEC);
        Serial.print(PSTR2("\t"));
        Serial.print(Receiver::rssiA, DEC);
        Serial.print(PSTR2("\t"));
        Serial.print(Receiver::rssiARaw, DEC);
        Serial.print(PSTR2("\t"));
        Serial.print(Receiver::rssiB, DEC);
        Serial.print(PSTR2("\t"));
        Serial.println(Receiver::rssiBRaw, DEC);

        Receiver::serialLogTimer.reset();
    }
}
#endif
