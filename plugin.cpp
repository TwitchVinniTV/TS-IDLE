#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#define PLUGINS_EXPORTEXPORT __declspec(dllexport)
#else
#define PLUGINS_EXPORTEXPORT __attribute__ ((visibility("default")))
#endif

// TeamSpeak 3 SDK Minimal-Definitionen, damit wir keine externen Header brauchen
typedef unsigned short anyID;
typedef unsigned long long uint64;
#define ERROR_ok 0

struct TS3Functions {
    unsigned int (*getClientID)(uint64 serverConnectionHandlerID, anyID* result);
    unsigned int (*getChannelOfClient)(uint64 serverConnectionHandlerID, anyID clientID, uint64* result);
    unsigned int (*requestClientMove)(uint64 serverConnectionHandlerID, anyID clientID, uint64 newChannelID, const char* password, const char* returnCode);
};

static struct TS3Functions ts3Functions;

// Deine Einstellungen
#define TARGET_CHANNEL_ID 34804
#define AFK_THRESHOLD_SECONDS 2700 // 45 Minuten in Sekunden

static time_t lastActionTime = 0;
static uint64 lastChannelID = 0;
static int isHandlingAFK = 0;

extern "C" {
    PLUGINS_EXPORTEXPORT const char* ts3plugin_name() { return "Vinni AFK Jumper"; }
    PLUGINS_EXPORTEXPORT const char* ts3plugin_version() { return "1.0"; }
    PLUGINS_EXPORTEXPORT int ts3plugin_apiVersion() { return 26; } 
    PLUGINS_EXPORTEXPORT const char* ts3plugin_author() { return "Vinni_TV_"; }
    PLUGINS_EXPORTEXPORT const char* ts3plugin_description() { return "Wechselt nach 45 Min AFK kurz den Channel und springt sofort zurueck."; }

    PLUGINS_EXPORTEXPORT void ts3plugin_setFunctionPointers(struct TS3Functions funcs) {
        ts3Functions = funcs;
    }

    PLUGINS_EXPORTEXPORT int ts3plugin_init() {
        lastActionTime = time(NULL);
        return 0;
    }

    PLUGINS_EXPORTEXPORT void ts3plugin_shutdown() {}

    PLUGINS_EXPORTEXPORT void ts3plugin_onClientMoveEvent(uint64 serverConnectionHandlerID, anyID clientID, uint64 oldChannelID, uint64 newChannelID, int visibility, const char* moveMessage) {
        anyID myID;
        if (ts3Functions.getClientID(serverConnectionHandlerID, &myID) == ERROR_ok && myID == clientID) {
            if (!isHandlingAFK) {
                lastActionTime = time(NULL); // Reset bei aktiver Bewegung
            }
        }
    }

    PLUGINS_EXPORTEXPORT void ts3plugin_onUpdateClientEvent(uint64 serverConnectionHandlerID, anyID clientID) {
        anyID myID;
        if (ts3Functions.getClientID(serverConnectionHandlerID, &myID) != ERROR_ok || myID != clientID) return;
        if (isHandlingAFK) return;

        time_t now = time(NULL);
        if (now - lastActionTime >= AFK_THRESHOLD_SECONDS) {
            isHandlingAFK = 1;
            if (ts3Functions.getChannelOfClient(serverConnectionHandlerID, myID, &lastChannelID) == ERROR_ok) {
                if (lastChannelID != TARGET_CHANNEL_ID) {
                    // 1. In den AFK-Channel springen
                    ts3Functions.requestClientMove(serverConnectionHandlerID, myID, TARGET_CHANNEL_ID, "", NULL);
                    // 2. Sofort wieder zurück in den alten Channel springen
                    ts3Functions.requestClientMove(serverConnectionHandlerID, myID, lastChannelID, "", NULL);
                }
            }
            lastActionTime = now;
            isHandlingAFK = 0;
        }
    }
}
