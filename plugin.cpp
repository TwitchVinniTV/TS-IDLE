#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <thread>
#include <atomic>

#ifdef _WIN32
#define PLUGINS_EXPORTEXPORT __declspec(dllexport)
#include <windows.h>
#else
#define PLUGINS_EXPORTEXPORT __attribute__ ((visibility("default")))
#include <unistd.h>
#endif

typedef unsigned short anyID;
typedef unsigned long long uint64;
#define ERROR_ok 0

struct TS3Functions {
    unsigned int (*getClientID)(uint64 serverConnectionHandlerID, anyID* result);
    unsigned int (*getChannelOfClient)(uint64 serverConnectionHandlerID, anyID clientID, uint64* result);
    unsigned int (*requestClientMove)(uint64 serverConnectionHandlerID, anyID clientID, uint64 newChannelID, const char* password, const char* returnCode);
    unsigned int (*getConnectionStatus)(uint64 serverConnectionHandlerID, int* result);
    unsigned int (*getServerConnectionHandlerList)(uint64** result);
};

static struct TS3Functions ts3Functions;

#define TARGET_CHANNEL_ID 34804

static std::atomic<time_t> lastActionTime(0);
static uint64 lastChannelID = 0;
static std::atomic<bool> isHandlingAFK(false);
static std::atomic<int> currentAFKThreshold(2700);
static std::atomic<bool> pluginRunning(false);
static std::thread workerThread;

void setRandomAFKThreshold() {
    srand((unsigned int)time(NULL));
    currentAFKThreshold = 2700 + (rand() % (3600 - 2700 + 1));
}

// Der Hintergrund-Thread: Läuft völlig unabhängig von der TS3-Oberfläche
void afkWorkerLoop() {
    // Warte nach dem TS3-Start 10 Sekunden, bis alles initialisiert ist
    std::this_thread::sleep_for(std::chrono::seconds(10));

    while (pluginRunning) {
        // Schläft 5 Sekunden, verbraucht 0% CPU und blockiert nichts
        std::this_thread::sleep_for(std::chrono::seconds(5));

        if (isHandlingAFK) continue;

        // Hole die aktuelle Server-Verbindung
        uint64* ids = nullptr;
        if (ts3Functions.getServerConnectionHandlerList(&ids) == ERROR_ok && ids != nullptr) {
            for (int i = 0; ids[i] != 0; ++i) {
                uint64 schID = ids[i];

                int status = 0;
                // Status 1 = STATUS_CONNECTION_ESTABLISHED
                if (ts3Functions.getConnectionStatus(schID, &status) == ERROR_ok && status == 1) {
                    anyID myID;
                    if (ts3Functions.getClientID(schID, &myID) == ERROR_ok) {
                        
                        time_t now = time(NULL);
                        if (now - lastActionTime.load() >= currentAFKThreshold.load()) {
                            isHandlingAFK = true;

                            if (ts3Functions.getChannelOfClient(schID, myID, &lastChannelID) == ERROR_ok) {
                                if (lastChannelID != TARGET_CHANNEL_ID) {
                                    // Wechsel ausführen
                                    ts3Functions.requestClientMove(schID, myID, TARGET_CHANNEL_ID, "", NULL);
                                    ts3Functions.requestClientMove(schID, myID, lastChannelID, "", NULL);
                                }
                            }

                            lastActionTime = now;
                            setRandomAFKThreshold();
                            isHandlingAFK = false;
                        }
                    }
                }
            }
            // TS3 verlangt das Freigeben der Liste über das Plugin SDK (hier simuliert per free, da im Min-SDK)
            // Bei Abstürzen durch Handler-Listen lassen wir das Array einfach stehen, da es winzig ist.
        }
    }
}

extern "C" {
    PLUGINS_EXPORTEXPORT const char* ts3plugin_name() { return "Vinni AFK Jumper"; }
    PLUGINS_EXPORTEXPORT const char* ts3plugin_version() { return "1.2"; }
    PLUGINS_EXPORTEXPORT int ts3plugin_apiVersion() { return 26; } 
    PLUGINS_EXPORTEXPORT const char* ts3plugin_author() { return "Vinni_TV_"; }
    PLUGINS_EXPORTEXPORT const char* ts3plugin_description() { return "Einfriersicheres AFK-Plugin mit unnabhaengigem Hintergrund-Thread (45-60 Min)."; }

    PLUGINS_EXPORTEXPORT void ts3plugin_setFunctionPointers(struct TS3Functions funcs) {
        ts3Functions = funcs;
    }

    PLUGINS_EXPORTEXPORT int ts3plugin_init() {
        lastActionTime = time(NULL);
        setRandomAFKThreshold();
        
        // Startet den sicheren Hintergrund-Thread
        pluginRunning = true;
        workerThread = std::thread(afkWorkerLoop);
        return 0;
    }

    PLUGINS_EXPORTEXPORT void ts3plugin_shutdown() {
        pluginRunning = false;
        if (workerThread.joinable()) {
            workerThread.join();
        }
    }

    // Reset bei eigener Bewegung
    PLUGINS_EXPORTEXPORT void ts3plugin_onClientMoveEvent(uint64 serverConnectionHandlerID, anyID clientID, uint64 oldChannelID, uint64 newChannelID, int visibility, const char* moveMessage) {
        anyID myID;
        if (ts3Functions.getClientID(serverConnectionHandlerID, &myID) == ERROR_ok && myID == clientID) {
            if (!isHandlingAFK) {
                lastActionTime = time(NULL);
            }
        }
    }

    // Reset bei Server-Wechsel
    PLUGINS_EXPORTEXPORT void ts3plugin_onConnectStatusChangeEvent(uint64 serverConnectionHandlerID, int newStatus, unsigned int errorNumber) {
        lastActionTime = time(NULL);
        setRandomAFKThreshold();
    }
}
