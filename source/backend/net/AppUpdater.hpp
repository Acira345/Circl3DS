#pragma once
#include <3ds.h>
#include <citro2d.h>
#include <string>
#include <vector>

enum class UpdateState {
    IDLE = 0,
    CHECKING,
    UP_TO_DATE,
    AVAILABLE,
    DOWNLOADING,
    INSTALLING,
    COMPLETE,
    FAILED
};

class AppUpdater {
public:
    static constexpr const char* CURRENT_VERSION_STR = "4.1.0";
    static constexpr int CURRENT_VERSION_CODE = 410; // formato: mayor*100 + menor*10 + parche

    AppUpdater();
    ~AppUpdater();

    void init();
    void shutdown();

    // Inicia la comprobacion en segundo plano (no bloquea el juego)
    void checkForUpdatesAsync(const std::string& customUrl = "");

    // Configura la ruta real del ejecutable .3dsx (argv[0]) para actualizar en el sitio correcto
    void setExecutablePath(const char* argv0);

    // Inicia la descarga e instalacion automatica en segundo plano
    void startInstallAsync();

    // Cancelar operacion
    void cancelOperation();

    // Gestion del dialogo/modal visual
    bool isModalOpen() const { return m_modalOpen; }
    void openModal() { m_modalOpen = true; }
    void closeModal() { m_modalOpen = false; }

    // Estado actual
    UpdateState getState() const { return m_state; }
    float getProgress() const { return m_progress; }
    size_t getBytesReceived() const { return m_bytesReceived; }
    size_t getTotalBytes() const { return m_totalBytes; }
    float getSpeedKbps() const { return m_speedKbps; }
    std::string getRemoteVersion() const;
    std::string getChangelog() const;
    std::string getStatusMessage() const;
    bool isCiaMode() const { return m_isCia; }

    // Renderizado del dialogo de actualizacion en ambas pantallas
    void renderTopScreen(C2D_TextBuf textBuf, float slider3D, bool stereoActive, float screenW = 400.0f, float screenH = 240.0f);
    void renderBottomScreen(C2D_TextBuf textBuf, float screenW = 320.0f, float screenH = 240.0f);

    // Procesa pulsaciones fisicas o tactiles del modal. Devuelve true si el modal capturo el evento.
    bool handleInput(u32 kDown, const touchPosition& touch, bool& outRequestQuit);

private:
    static void checkThreadEntry(void* arg);
    static void installThreadEntry(void* arg);

    void joinFinishedWorker();
    void setStatusMessage(const std::string& msg);

    void runCheck();
    void runInstall();

    // HTTP Helper compartido con seguimiento de redirecciones y soporte anti-cache
    bool openHttpGet(const std::string& url, bool noCache, httpcContext& outContext, u32& outContentSize, std::string& errOut);
    bool httpDownloadToMemory(const std::string& url, std::string& outData, std::string& errOut);
    bool httpDownloadAndInstall(const std::string& url, std::string& errOut);

    // Parser JSON ligero y robusto (con unescape basico)
    static std::string extractJsonString(const std::string& json, const std::string& key);
    static int extractJsonInt(const std::string& json, const std::string& key);

    mutable LightLock m_lock;
    Thread m_workerThread = nullptr;
    volatile bool m_threadRunning = false;
    volatile bool m_cancelRequested = false;

    UpdateState m_state = UpdateState::IDLE;
    bool m_modalOpen = false;
    bool m_isCia = true;

    std::string m_3dsxTargetPath = "sdmc:/3ds/circl3ds/circl3ds.3dsx";
    std::string m_checkUrl = "";
    std::string m_remoteVersion = "";
    int m_remoteVersionCode = 0;
    std::string m_changelog = "";
    std::string m_ciaUrl = "";
    std::string m_3dsxUrl = "";
    std::string m_statusMessage = "";

    volatile float m_progress = 0.0f;
    volatile size_t m_bytesReceived = 0;
    volatile size_t m_totalBytes = 0;
    volatile float m_speedKbps = 0.0f;

    int m_animTick = 0;
};
