#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#define PLUGINS_EXPORTEXPORT __declspec(dllexport)
#else
#define PLUGINS_EXPORTEXPORT __attribute__ ((visibility("default")))
#endif

typedef unsigned short anyID;
typedef unsigned long long uint64;
#define ERROR_ok 0

struct TS3Functions {
    unsigned int (*getClientID)(uint64 serverConnectionHandlerID, anyID* result);
    unsigned int (*getChannelOfClient)(uint64 serverConnectionHandlerID, anyID clientID, uint64* result);
    unsigned int (*requestClientMove)(uint64 serverConnectionHandlerID, anyID clientID, uint64 newChannelID, const char* password, const char* returnCode);
    unsigned int (*getConnectionStatus)(uint64 serverConnectionHandlerID, int* result);
};

static struct TS3Functions ts3Functions;

#define TARGET_CHANNEL_ID 34804

static time_t lastActionTime = 0;
static uint64 lastChannelID = 0;
static int isHandlingAFK = 0;
static int currentAFKThreshold = 2700; // Startwert: 45 Minuten

// Funktion um eine Zufallszeit zwischen 45 Min (2700s) und 60 Min (3600s) zu generieren
void setRandomAFKThreshold() {
    srand((unsigned int)time(NULL));
    currentAFKThreshold = 2700 + (rand() % (3600 - 2700 + 1));
}

extern "C" {
    PLUGINS_EXPORTEXPORT const char* ts3plugin_name() { return "Vinni AFK Jumper"; }
    PLUGINS_EXPORTEXPORT const char* ts3plugin_version() { return "1.1"; }
    PLUGINS_EXPORTEXPORT int ts3plugin_apiVersion() { return 26; } 
    PLUGINS_EXPORTEXPORT const char* ts3plugin_author() { return "Vinni_TV_"; }
    PLUGINS_EXPORTEXPORT const char* ts3plugin_description() { return "Wechselt nach einer zufaelligen Zeit (45-60 Min) AFK kurz den Channel und springt sofort zurueck."; }

    PLUGINS_EXPORTEXPORT void ts3plugin_setFunctionPointers(struct TS3Functions funcs) {
        ts3Functions = funcs;
    }

    PLUGINS_EXPORTEXPORT int ts3plugin_init() {
        lastActionTime = time(NULL);
        setRandomAFKThreshold();
        return 0;
    }

    PLUGINS_EXPORTEXPORT void ts3plugin_shutdown() {}

    // Setzt den Timer zurück, wenn du dich aktiv bewegst
    PLUGINS_EXPORTEXPORT void ts3plugin_onClientMoveEvent(uint64 serverConnectionHandlerID, anyID clientID, uint64 oldChannelID, uint64 newChannelID, int visibility, const char* moveMessage) {
        anyID myID;
        if (ts3Functions.getClientID(serverConnectionHandlerID, &myID) == ERROR_ok && myID == clientID) {
            if (!isHandlingAFK) {
                lastActionTime = time(NULL);
            }
        }
    }

    // Server-Wechsel oder Connect -> Timer resetten & neue Random Zeit auswürfeln
    PLUGINS_EXPORTEXPORT void ts3plugin_onConnectStatusChangeEvent(uint64 serverConnectionHandlerID, int newStatus, unsigned int errorNumber) {
        lastActionTime = time(NULL);
        setRandomAFKThreshold();
    }

    PLUGINS_EXPORTEXPORT void ts3plugin_onUpdateClientEvent(uint64 serverConnectionHandlerID, anyID clientID) {
        // 1. Prüfen ob wir überhaupt voll verbunden sind (Status 1 = STATUS_CONNECTION_ESTABLISHED)
        int status = 0;
        if (ts3Functions.getConnectionStatus(serverConnectionHandlerID, &status) != ERROR_ok || status != 1) return;

        anyID myID;
        if (ts3Functions.getClientID(serverConnectionHandlerID, &myID) != ERROR_ok || myID != clientID) return;
        if (isHandlingAFK) return;

        time_t now = time(NULL);
        if (now - lastActionTime >= currentAFKThreshold) {
            isHandlingAFK = 1;
            
            if (ts3Functions.getChannelOfClient(serverConnectionHandlerID, myID, &lastChannelID) == ERROR_ok) {
                if (lastChannelID != TARGET_CHANNEL_ID) {
                    // In den AFK-Channel springen
                    ts3Functions.requestClientMove(serverConnectionHandlerID, myID, TARGET_CHANNEL_ID, "", NULL);
                    // Sofort wieder zurück
                    ts3Functions.requestClientMove(serverConnectionHandlerID, myID, lastChannelID, "", NULL);
                }
            }
            
            // Nach dem Sprung: Timer resetten und NEUE Zufallszeit für das nächste Mal auswürfeln
            lastActionTime = now;
            setRandomAFKThreshold();
            isHandlingAFK = 0;
        }
    }
}
