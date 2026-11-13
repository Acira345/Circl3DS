#include "AppUpdater.hpp"
#include "OszExtractor.hpp"
#include "UiKit.hpp"
#include "Draw.hpp"
#include <3ds/env.h>
#include <cstdio>
#include <cstring>
#include <algorithm>

AppUpdater::AppUpdater() {
    LightLock_Init(&m_lock);
    m_isCia = !envIsHomebrew();
}

AppUpdater::~AppUpdater() {
    shutdown();
}

void AppUpdater::init() {
    m_isCia = !envIsHomebrew();
    m_state = UpdateState::IDLE;
    m_progress = 0.0f;
    m_bytesReceived = 0;
    m_totalBytes = 0;
    m_speedKbps = 0.0f;
    m_modalOpen = false;
    m_threadRunning = false;
    m_cancelRequested = false;
}

void AppUpdater::joinFinishedWorker() {
    if (m_workerThread && !m_threadRunning) {
        threadJoin(m_workerThread, 2000000000ULL);
        threadFree(m_workerThread);
        m_workerThread = nullptr;
    }
}

void AppUpdater::shutdown() {
    cancelOperation();
    if (m_workerThread) {
        threadJoin(m_workerThread, 2000000000ULL); // 2 segundos max
        threadFree(m_workerThread);
        m_workerThread = nullptr;
    }
}

void AppUpdater::cancelOperation() {
    m_cancelRequested = true;
}

void AppUpdater::setExecutablePath(const char* argv0) {
    if (!argv0 || argv0[0] == '\0') return;
    std::string p(argv0);
    // Ignorar rutas streaming 3dslink:/ porque no apuntan a un archivo local en SD
    if (p.rfind("3dslink:", 0) == 0) return;
    if (p[0] == '/') p = "sdmc:" + p;
    if (p.rfind("sdmc:/", 0) != 0) return;

    if (p.size() >= 5) {
        std::string ext = p.substr(p.size() - 5);
        for (size_t i = 0; i < ext.size(); ++i) {
            if (ext[i] >= 'A' && ext[i] <= 'Z') ext[i] = static_cast<char>(ext[i] - 'A' + 'a');
        }
        if (ext == ".3dsx") {
            LightLock_Lock(&m_lock);
            m_3dsxTargetPath = p;
            LightLock_Unlock(&m_lock);
        }
    }
}

void AppUpdater::setStatusMessage(const std::string& msg) {
    LightLock_Lock(&m_lock);
    m_statusMessage = msg;
    LightLock_Unlock(&m_lock);
}

std::string AppUpdater::getRemoteVersion() const {
    LightLock_Lock(&m_lock);
    std::string v = m_remoteVersion;
    LightLock_Unlock(&m_lock);
    return v;
}

std::string AppUpdater::getChangelog() const {
    LightLock_Lock(&m_lock);
    std::string c = m_changelog;
    LightLock_Unlock(&m_lock);
    return c;
}

std::string AppUpdater::getStatusMessage() const {
    LightLock_Lock(&m_lock);
    std::string s = m_statusMessage;
    LightLock_Unlock(&m_lock);
    return s;
}

void AppUpdater::checkForUpdatesAsync(const std::string& customUrl) {
    if (m_threadRunning) return;
    joinFinishedWorker();

    m_checkUrl = customUrl.empty()
        ? "https://raw.githubusercontent.com/Acira345/Circl3DS/main/version.json"
        : customUrl;

    m_state = UpdateState::CHECKING;
    setStatusMessage("Buscando actualizaciones...");
    m_cancelRequested = false;
    m_threadRunning = true;

    s32 mainPrio = 0x30;
    svcGetThreadPriority(&mainPrio, CUR_THREAD_HANDLE);
    s32 workerPrio = (mainPrio > 0x18) ? (mainPrio - 1) : mainPrio;

    m_workerThread = threadCreate(checkThreadEntry, this, 64 * 1024, workerPrio, 1, false);
    if (!m_workerThread) {
        m_workerThread = threadCreate(checkThreadEntry, this, 64 * 1024, workerPrio, -2, false);
    }
    if (!m_workerThread) {
        m_threadRunning = false;
        m_state = UpdateState::FAILED;
        setStatusMessage("No se pudo crear hilo de red");
    }
}

void AppUpdater::startInstallAsync() {
    if (m_threadRunning) return;
    if (m_state != UpdateState::AVAILABLE) return;
    joinFinishedWorker();

    m_state = UpdateState::DOWNLOADING;
    setStatusMessage("Iniciando descarga...");
    m_progress = 0.0f;
    m_bytesReceived = 0;
    m_totalBytes = 0;
    m_speedKbps = 0.0f;
    m_cancelRequested = false;
    m_threadRunning = true;

    s32 mainPrio = 0x30;
    svcGetThreadPriority(&mainPrio, CUR_THREAD_HANDLE);
    s32 workerPrio = (mainPrio > 0x18) ? (mainPrio - 1) : mainPrio;

    m_workerThread = threadCreate(installThreadEntry, this, 64 * 1024, workerPrio, 1, false);
    if (!m_workerThread) {
        m_workerThread = threadCreate(installThreadEntry, this, 64 * 1024, workerPrio, -2, false);
    }
    if (!m_workerThread) {
        m_threadRunning = false;
        m_state = UpdateState::FAILED;
        setStatusMessage("No se pudo iniciar hilo instalador");
    }
}

void AppUpdater::checkThreadEntry(void* arg) {
    AppUpdater* self = static_cast<AppUpdater*>(arg);
    if (self) self->runCheck();
}

void AppUpdater::installThreadEntry(void* arg) {
    AppUpdater* self = static_cast<AppUpdater*>(arg);
    if (self) self->runInstall();
}

std::string AppUpdater::extractJsonString(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return "";
    pos += needle.size();

    pos = json.find(':', pos);
    if (pos == std::string::npos) return "";
    pos++;

    size_t start = json.find('\"', pos);
    if (start == std::string::npos) return "";
    start++;

    std::string out;
    out.reserve(64);
    for (size_t i = start; i < json.size(); ++i) {
        char c = json[i];
        if (c == '\"') return out;
        if (c == '\\' && i + 1 < json.size()) {
            char next = json[++i];
            if (next == 'n') out.push_back('\n');
            else if (next == 'r') {}
            else if (next == 't') out.push_back(' ');
            else out.push_back(next);
            continue;
        }
        out.push_back(c);
    }
    return "";
}

int AppUpdater::extractJsonInt(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return 0;
    pos += needle.size();

    pos = json.find(':', pos);
    if (pos == std::string::npos) return 0;
    pos++;

    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\r' || json[pos] == '\n')) {
        pos++;
    }
    if (pos >= json.size()) return 0;

    int val = 0;
    bool neg = false;
    if (json[pos] == '-') { neg = true; pos++; }
    while (pos < json.size() && json[pos] >= '0' && json[pos] <= '9') {
        val = val * 10 + (json[pos] - '0');
        pos++;
    }
    return neg ? -val : val;
}

bool AppUpdater::openHttpGet(const std::string& url, bool noCache, httpcContext& outContext, u32& outContentSize, std::string& errOut) {
    Result res = httpcInit(0);
    if (R_FAILED(res)) {
        errOut = "Error al iniciar HTTP (3DS)";
        return false;
    }

    std::string curUrl = url;
    if (noCache) {
        char tsBuf[32];
        std::snprintf(tsBuf, sizeof(tsBuf), "%ct=%llu", (curUrl.find('?') == std::string::npos) ? '?' : '&', (unsigned long long)osGetTime());
        curUrl += tsBuf;
    }

    u32 statusCode = 0;
    int maxRedirects = 5;
    bool contextOpen = false;

    while (maxRedirects-- > 0 && !m_cancelRequested) {
        res = httpcOpenContext(&outContext, HTTPC_METHOD_GET, curUrl.c_str(), 1);
        if (R_FAILED(res)) {
            httpcExit();
            errOut = "Error de conexion SSL/Red";
            return false;
        }
        contextOpen = true;

        httpcSetSSLOpt(&outContext, SSLCOPT_DisableVerify);
        httpcSetKeepAlive(&outContext, HTTPC_KEEPALIVE_ENABLED);
        httpcAddRequestHeaderField(&outContext, "User-Agent", "Circl3DS-Updater/1.1 (Nintendo 3DS)");
        httpcAddRequestHeaderField(&outContext, "Connection", "Keep-Alive");
        if (noCache) {
            httpcAddRequestHeaderField(&outContext, "Cache-Control", "no-cache");
            httpcAddRequestHeaderField(&outContext, "Pragma", "no-cache");
        }

        res = httpcBeginRequest(&outContext);
        if (R_FAILED(res)) {
            httpcCloseContext(&outContext);
            httpcExit();
            errOut = "Servidor no responde (SSL)";
            return false;
        }

        statusCode = 0;
        res = httpcGetResponseStatusCode(&outContext, &statusCode);
        if (R_FAILED(res)) {
            httpcCloseContext(&outContext);
            httpcExit();
            errOut = "Fallo al leer estado HTTP";
            return false;
        }

        if ((statusCode >= 301 && statusCode <= 303) || (statusCode >= 307 && statusCode <= 308)) {
            char newUrlBuf[2048] = "";
            res = httpcGetResponseHeader(&outContext, "Location", newUrlBuf, sizeof(newUrlBuf));
            httpcCloseContext(&outContext);
            contextOpen = false;
            if (R_SUCCEEDED(res) && newUrlBuf[0] != '\0') {
                curUrl = newUrlBuf;
                continue;
            }
            httpcExit();
            errOut = "Redireccion invalida";
            return false;
        }
        break;
    }

    if (m_cancelRequested) {
        if (contextOpen) httpcCloseContext(&outContext);
        httpcExit();
        errOut = "Operacion cancelada";
        return false;
    }

    if (!contextOpen) {
        httpcExit();
        errOut = "Demasiadas redirecciones";
        return false;
    }

    if (statusCode != 200) {
        httpcCloseContext(&outContext);
        httpcExit();
        char errBuf[64];
        std::snprintf(errBuf, sizeof(errBuf), "HTTP error %lu", (unsigned long)statusCode);
        errOut = errBuf;
        return false;
    }

    outContentSize = 0;
    httpcGetDownloadSizeState(&outContext, NULL, &outContentSize);
    return true;
}

bool AppUpdater::httpDownloadToMemory(const std::string& url, std::string& outData, std::string& errOut) {
    httpcContext context;
    u32 contentSize = 0;
    if (!openHttpGet(url, true, context, contentSize, errOut)) {
        return false;
    }

    outData.clear();
    if (contentSize > 0 && contentSize < 512 * 1024) {
        outData.reserve(contentSize);
    }
    std::vector<u8> buffer(16 * 1024);
    u32 bytesRead = 0;
    Result res = 0;

    do {
        if (m_cancelRequested) {
            httpcCloseContext(&context);
            httpcExit();
            errOut = "Cancelado";
            return false;
        }
        bytesRead = 0;
        res = httpcReceiveData(&context, buffer.data(), buffer.size());
        if (res == (Result)0xD880A7FE) { // HTTPC_RESULTCODE_DOWNLOADPENDING
            httpcGetDownloadSizeState(&context, &bytesRead, NULL);
        } else if (R_FAILED(res)) {
            httpcCloseContext(&context);
            httpcExit();
            errOut = "Error leyendo datos HTTP";
            return false;
        } else {
            httpcGetDownloadSizeState(&context, &bytesRead, NULL);
        }

        if (bytesRead > 0) {
            outData.append(reinterpret_cast<const char*>(buffer.data()), bytesRead);
        }
    } while (res == (Result)0xD880A7FE);

    httpcCloseContext(&context);
    httpcExit();
    return !outData.empty();
}

bool AppUpdater::httpDownloadAndInstall(const std::string& url, std::string& errOut) {
    httpcContext context;
    u32 totalSize = 0;
    if (!openHttpGet(url, false, context, totalSize, errOut)) {
        return false;
    }

    m_totalBytes = totalSize;
    m_bytesReceived = 0;
    m_progress = 0.0f;
    m_speedKbps = 0.0f;

    Handle ciaHandle = 0;
    FILE* fp3dsx = nullptr;
    u64 fileOffset = 0;

    LightLock_Lock(&m_lock);
    std::string target3dsxPath = m_3dsxTargetPath;
    LightLock_Unlock(&m_lock);
    std::string tmp3dsxPath = target3dsxPath + ".tmp";

    if (m_isCia) {
        Result res = amInit();
        if (R_FAILED(res)) {
            httpcCloseContext(&context);
            httpcExit();
            errOut = "Permiso AM no concedido (am:u)";
            return false;
        }

        // Iniciar instalador con sobreescritura nativa del titulo
        res = AM_StartCiaInstallOverwrite(&ciaHandle, MEDIATYPE_SD);
        if (R_FAILED(res)) {
            res = AM_StartCiaInstall(MEDIATYPE_SD, &ciaHandle);
        }

        if (R_FAILED(res)) {
            amExit();
            httpcCloseContext(&context);
            httpcExit();
            char b[64];
            std::snprintf(b, sizeof(b), "Error AM_StartCia: 0x%08lX", (unsigned long)res);
            errOut = b;
            return false;
        }
    } else {
        size_t slashPos = target3dsxPath.find_last_of('/');
        if (slashPos != std::string::npos) {
            OszExtractor::ensureDirectoryRecursive(target3dsxPath.substr(0, slashPos));
        }
        fp3dsx = std::fopen(tmp3dsxPath.c_str(), "wb");
        if (!fp3dsx) {
            httpcCloseContext(&context);
            httpcExit();
            errOut = "No se pudo escribir en SD";
            return false;
        }
    }

    auto abortCleanup = [&](const std::string& reason) -> bool {
        if (m_isCia && ciaHandle != 0) {
            AM_CancelCIAInstall(ciaHandle);
            amExit();
        }
        if (fp3dsx) {
            std::fclose(fp3dsx);
            std::remove(tmp3dsxPath.c_str());
        }
        httpcCloseContext(&context);
        httpcExit();
        errOut = reason;
        return false;
    };

    std::vector<u8> buffer(64 * 1024);
    u32 bytesRead = 0;
    Result res = 0;
    bool headerChecked = false;
    u64 speedSampleStart = osGetTime();
    size_t speedSampleBytes = 0;

    do {
        if (m_cancelRequested) {
            return abortCleanup("Instalacion cancelada");
        }

        bytesRead = 0;
        res = httpcReceiveData(&context, buffer.data(), buffer.size());
        if (res == (Result)0xD880A7FE) {
            httpcGetDownloadSizeState(&context, &bytesRead, NULL);
        } else if (R_FAILED(res)) {
            return abortCleanup("Interrupcion de red durante descarga");
        } else {
            httpcGetDownloadSizeState(&context, &bytesRead, NULL);
        }

        if (bytesRead > 0) {
            if (!headerChecked) {
                headerChecked = true;
                if (!m_isCia && (bytesRead < 4 || std::memcmp(buffer.data(), "3DSX", 4) != 0)) {
                    return abortCleanup("Archivo 3DSX remoto invalido");
                }
            }

            if (m_isCia) {
                u32 written = 0;
                // Sin FS_WRITE_FLUSH por chunk para maximizar velocidad; AM_FinishCiaInstall confirma al final
                Result wRes = FSFILE_Write(ciaHandle, &written, fileOffset, buffer.data(), bytesRead, 0);
                if (R_FAILED(wRes) || written != bytesRead) {
                    return abortCleanup("Fallo de escritura en SD");
                }
                fileOffset += written;
            } else if (fp3dsx) {
                if (std::fwrite(buffer.data(), 1, bytesRead, fp3dsx) != bytesRead) {
                    return abortCleanup("Fallo de escritura en SD");
                }
            }

            m_bytesReceived += bytesRead;
            speedSampleBytes += bytesRead;
            u64 now = osGetTime();
            u64 elapsedMs = now - speedSampleStart;
            if (elapsedMs >= 400) {
                m_speedKbps = (static_cast<float>(speedSampleBytes) * 1000.0f) / (static_cast<float>(elapsedMs) * 1024.0f);
                speedSampleStart = now;
                speedSampleBytes = 0;
            }
            if (m_totalBytes > 0) {
                m_progress = std::min(1.0f, static_cast<float>(m_bytesReceived) / static_cast<float>(m_totalBytes));
            }
        }
    } while (res == (Result)0xD880A7FE);

    // Verificar integridad de descarga antes de aplicar cambios irreversibles
    const size_t MIN_VALID_PACKAGE_BYTES = 64 * 1024;
    if (m_bytesReceived < MIN_VALID_PACKAGE_BYTES || (m_totalBytes > 0 && m_bytesReceived != m_totalBytes)) {
        return abortCleanup("Descarga incompleta, se cancelo por seguridad");
    }

    httpcCloseContext(&context);
    httpcExit();

    if (m_isCia) {
        m_state = UpdateState::INSTALLING;
        setStatusMessage("Finalizando instalacion en la 3DS...");
        res = AM_FinishCiaInstall(ciaHandle);
        amExit();
        if (R_FAILED(res)) {
            char b[64];
            std::snprintf(b, sizeof(b), "Error AM_FinishCia: 0x%08lX", (unsigned long)res);
            errOut = b;
            return false;
        }
    } else {
        if (fp3dsx) {
            std::fflush(fp3dsx);
            std::fclose(fp3dsx);
            fp3dsx = nullptr;
        }
        std::string bakPath = target3dsxPath + ".bak";
        std::remove(bakPath.c_str());
        bool hadOld = (std::rename(target3dsxPath.c_str(), bakPath.c_str()) == 0);
        if (std::rename(tmp3dsxPath.c_str(), target3dsxPath.c_str()) != 0) {
            if (hadOld) std::rename(bakPath.c_str(), target3dsxPath.c_str());
            std::remove(tmp3dsxPath.c_str());
            errOut = "No se pudo reemplazar el archivo .3dsx";
            return false;
        }
        if (hadOld) std::remove(bakPath.c_str());
    }

    return true;
}

void AppUpdater::runCheck() {
    std::string json;
    std::string err;
    bool ok = httpDownloadToMemory(m_checkUrl, json, err);
    if (!ok) {
        m_state = UpdateState::FAILED;
        setStatusMessage(err);
        m_threadRunning = false;
        return;
    }

    std::string rVer = extractJsonString(json, "version");
    int rCode = extractJsonInt(json, "version_code");
    std::string rLog = extractJsonString(json, "changelog");
    std::string rCia = extractJsonString(json, "cia_url");
    std::string r3dsx = extractJsonString(json, "3dsx_url");

    LightLock_Lock(&m_lock);
    m_remoteVersion = rVer;
    m_remoteVersionCode = rCode;
    m_changelog = rLog;
    m_ciaUrl = rCia;
    m_3dsxUrl = r3dsx;
    LightLock_Unlock(&m_lock);

    if (rCode > CURRENT_VERSION_CODE) {
        m_state = UpdateState::AVAILABLE;
        setStatusMessage("Nueva version disponible: v" + rVer);
        m_modalOpen = true; // Abrir modal para avisar al jugador
    } else {
        m_state = UpdateState::UP_TO_DATE;
        setStatusMessage("El juego esta actualizado (v" + std::string(CURRENT_VERSION_STR) + ")");
    }

    m_threadRunning = false;
}

void AppUpdater::runInstall() {
    LightLock_Lock(&m_lock);
    std::string targetUrl = m_isCia ? m_ciaUrl : m_3dsxUrl;
    std::string rVer = m_remoteVersion;
    LightLock_Unlock(&m_lock);

    if (targetUrl.empty()) {
        m_state = UpdateState::FAILED;
        setStatusMessage("URL de descarga no encontrada");
        m_threadRunning = false;
        return;
    }

    std::string err;
    bool ok = httpDownloadAndInstall(targetUrl, err);
    if (ok) {
        m_state = UpdateState::COMPLETE;
        m_progress = 1.0f;
        setStatusMessage("Actualizado con exito a v" + rVer);
    } else {
        m_state = UpdateState::FAILED;
        setStatusMessage(err.empty() ? "Fallo en la instalacion" : err);
    }

    m_threadRunning = false;
}

void AppUpdater::renderTopScreen(C2D_TextBuf textBuf, float slider3D, bool stereoActive, float screenW, float screenH) {
    if (!m_modalOpen && m_state != UpdateState::DOWNLOADING && m_state != UpdateState::INSTALLING) {
        return;
    }

    m_animTick++;

    std::string remoteVer = getRemoteVersion();
    std::string changelog = getChangelog();
    std::string statusMsg = getStatusMessage();

    // Sombra oscura translucida de fondo
    C2D_DrawRectSolid(0.0f, 0.0f, 0.70f, screenW, screenH, Ui::rgba(0, 0, 0, 190));

    float eyeSign = stereoActive ? 1.0f : 0.0f;
    float depthShift = eyeSign * slider3D * 4.0f;

    float cardW = 340.0f;
    float cardH = 190.0f;
    float cardX = (screenW - cardW) * 0.5f + depthShift;
    float cardY = (screenH - cardH) * 0.5f;

    u32 pinkAccent = Ui::rgba(255, 102, 171);
    u32 cyanAccent = Ui::rgba(56, 210, 255);
    u32 greenOk    = Ui::rgba(85, 221, 119);
    u32 cardBg     = Ui::rgba(18, 14, 28, 250);

    // Tarjeta del modal con borde brillante
    Ui::panel(cardX, cardY, cardW, cardH, 0.75f, cardBg, pinkAccent);

    // Cabecera del modal
    drawCenteredTextWithShadow(textBuf, cardX + cardW * 0.5f, cardY + 12.0f, 0.44f,
                               Ui::rgba(255, 255, 255), "ACTUALIZACION CIRCL3DS", 0.85f);

    if (m_state == UpdateState::CHECKING) {
        drawCenteredText(textBuf, cardX + cardW * 0.5f, cardY + 70.0f, 0.38f,
                         cyanAccent, "Comprobando version en linea...", 0.85f);
        drawCenteredText(textBuf, cardX + cardW * 0.5f, cardY + 100.0f, 0.32f,
                         Ui::rgba(180, 180, 200), "Por favor espera un momento", 0.85f);
    }
    else if (m_state == UpdateState::AVAILABLE) {
        // Version actual -> Version nueva
        char verBuf[64];
        std::snprintf(verBuf, sizeof(verBuf), "v%s  ==>  v%s", CURRENT_VERSION_STR, remoteVer.c_str());
        drawCenteredTextWithShadow(textBuf, cardX + cardW * 0.5f, cardY + 45.0f, 0.46f,
                                   greenOk, verBuf, 0.85f);

        // Caja de notas de version / changelog (hasta 3 lineas)
        float logX = cardX + 20.0f;
        float logY = cardY + 76.0f;
        float logW = cardW - 40.0f;
        float logH = 58.0f;
        C2D_DrawRectSolid(logX, logY, 0.78f, logW, logH, Ui::rgba(28, 22, 42, 220));
        drawRectOutline(logX, logY, logW, logH, 1.0f, Ui::rgba(255, 102, 171, 70), 0.79f);

        std::string notes = changelog.empty() ? "Nuevas mejoras de estabilidad y correccion de errores." : changelog;
        const size_t maxCharsPerLine = 44;
        size_t pos = 0;
        for (int lineIdx = 0; lineIdx < 3 && pos < notes.size(); ++lineIdx) {
            while (pos < notes.size() && notes[pos] == ' ') pos++;
            if (pos >= notes.size()) break;

            size_t nlPos = notes.find('\n', pos);
            size_t limit = std::min(notes.size(), pos + maxCharsPerLine);
            size_t endPos = (nlPos != std::string::npos && nlPos < limit) ? nlPos : limit;

            if (endPos < notes.size() && notes[endPos] != '\n' && notes[endPos] != ' ') {
                size_t sp = notes.rfind(' ', endPos);
                if (sp != std::string::npos && sp > pos + 10) {
                    endPos = sp;
                }
            }

            std::string lineStr = notes.substr(pos, endPos - pos);
            if (lineIdx == 2 && endPos < notes.size()) {
                lineStr = truncateString(lineStr, maxCharsPerLine - 3) + "...";
            }
            drawText(textBuf, logX + 8.0f, logY + 7.0f + lineIdx * 15.0f, 0.30f,
                     Ui::rgba(220, 220, 240), lineStr.c_str(), 0.85f);
            pos = endPos;
            if (pos < notes.size() && notes[pos] == '\n') pos++;
        }

        // Indicador de formato (CIA o 3DSX)
        const char* fmtStr = m_isCia ? "[Modo CIA: Instalacion directa al Menu HOME]" : "[Modo 3DSX: Actualizacion en SD]";
        drawCenteredText(textBuf, cardX + cardW * 0.5f, cardY + 148.0f, 0.28f, cyanAccent, fmtStr, 0.85f);
    }
    else if (m_state == UpdateState::DOWNLOADING || m_state == UpdateState::INSTALLING) {
        const char* titleStatus = (m_state == UpdateState::INSTALLING) ? "Instalando en sistema..." : "Descargando actualizacion...";
        drawCenteredTextWithShadow(textBuf, cardX + cardW * 0.5f, cardY + 45.0f, 0.40f, cyanAccent, titleStatus, 0.85f);

        // Barra de progreso animada
        float barX = cardX + 30.0f;
        float barY = cardY + 82.0f;
        float barW = cardW - 60.0f;
        float barH = 18.0f;
        C2D_DrawRectSolid(barX, barY, 0.78f, barW, barH, Ui::rgba(30, 24, 46, 255));
        drawRectOutline(barX, barY, barW, barH, 1.0f, pinkAccent, 0.79f);

        float fillW = barW * m_progress;
        if (fillW > 0.0f) {
            Ui::gradH(barX, barY, fillW, barH, 0.80f, pinkAccent, cyanAccent);
        }

        // Porcentaje, MB y velocidad
        char progBuf[80];
        float recMb = static_cast<float>(m_bytesReceived) / (1024.0f * 1024.0f);
        float totMb = static_cast<float>(m_totalBytes) / (1024.0f * 1024.0f);
        if (m_totalBytes > 0) {
            std::snprintf(progBuf, sizeof(progBuf), "%.1f / %.1f MB (%d%%) - %.0f KB/s",
                          recMb, totMb, static_cast<int>(m_progress * 100.0f), m_speedKbps);
        } else {
            std::snprintf(progBuf, sizeof(progBuf), "%.1f MB descargados - %.0f KB/s", recMb, m_speedKbps);
        }
        drawCenteredTextWithShadow(textBuf, cardX + cardW * 0.5f, barY + 28.0f, 0.33f, Ui::rgba(255, 255, 255), progBuf, 0.85f);

        drawCenteredText(textBuf, cardX + cardW * 0.5f, cardY + 150.0f, 0.30f,
                         Ui::rgba(255, 205, 55), "No apagues la consola ni extraigas la SD", 0.85f);
    }
    else if (m_state == UpdateState::COMPLETE) {
        drawCenteredTextWithShadow(textBuf, cardX + cardW * 0.5f, cardY + 52.0f, 0.46f,
                                   greenOk, "ACTUALIZACION COMPLETA", 0.85f);
        drawCenteredText(textBuf, cardX + cardW * 0.5f, cardY + 88.0f, 0.35f,
                         Ui::rgba(240, 240, 255), "Circl3DS ha sido actualizado correctamente.", 0.85f);
        const char* restartHint = m_isCia
            ? "Pulsa [A] para reiniciar automaticamente"
            : "Pulsa [A] para salir al Menu Homebrew";
        drawCenteredText(textBuf, cardX + cardW * 0.5f, cardY + 115.0f, 0.33f,
                         cyanAccent, restartHint, 0.85f);
    }
    else if (m_state == UpdateState::FAILED) {
        drawCenteredTextWithShadow(textBuf, cardX + cardW * 0.5f, cardY + 48.0f, 0.42f,
                                   Ui::rgba(255, 77, 109), "ERROR EN LA ACTUALIZACION", 0.85f);
        std::string errStr = statusMsg.empty() ? "Error desconocido de red" : statusMsg;
        drawCenteredText(textBuf, cardX + cardW * 0.5f, cardY + 86.0f, 0.32f,
                         Ui::rgba(220, 220, 220), truncateString(errStr, 42).c_str(), 0.85f);
        drawCenteredText(textBuf, cardX + cardW * 0.5f, cardY + 140.0f, 0.30f,
                         Ui::rgba(180, 180, 180), "Pulsa [B] para cerrar", 0.85f);
    }
    else if (m_state == UpdateState::UP_TO_DATE) {
        drawCenteredTextWithShadow(textBuf, cardX + cardW * 0.5f, cardY + 52.0f, 0.42f,
                                   greenOk, "VERSION AL DIA", 0.85f);
        char upBuf[64];
        std::snprintf(upBuf, sizeof(upBuf), "Tienes la ultima version instalada (v%s)", CURRENT_VERSION_STR);
        drawCenteredText(textBuf, cardX + cardW * 0.5f, cardY + 90.0f, 0.33f,
                         Ui::rgba(240, 240, 255), upBuf, 0.85f);
        drawCenteredText(textBuf, cardX + cardW * 0.5f, cardY + 140.0f, 0.30f,
                         Ui::rgba(180, 180, 180), "Pulsa [B] para cerrar", 0.85f);
    }
}

void AppUpdater::renderBottomScreen(C2D_TextBuf textBuf, float screenW, float screenH) {
    if (!m_modalOpen && m_state != UpdateState::DOWNLOADING && m_state != UpdateState::INSTALLING) {
        return;
    }

    C2D_DrawRectSolid(0.0f, 0.0f, 0.70f, screenW, screenH, Ui::rgba(0, 0, 0, 190));

    float cardW = 280.0f;
    float cardH = 170.0f;
    float cardX = (screenW - cardW) * 0.5f;
    float cardY = (screenH - cardH) * 0.5f;

    u32 pinkAccent = Ui::rgba(255, 102, 171);
    u32 cardBg     = Ui::rgba(18, 14, 28, 250);

    Ui::panel(cardX, cardY, cardW, cardH, 0.75f, cardBg, pinkAccent);

    if (m_state == UpdateState::AVAILABLE) {
        // Boton verde [A] ACTUALIZAR AHORA
        float btnW = 240.0f;
        float btnH = 42.0f;
        float btnX = cardX + (cardW - btnW) * 0.5f;
        float btnY1 = cardY + 30.0f;
        Ui::slantButton(btnX, btnY1, btnW, btnH, -5.0f, Ui::rgba(85, 221, 119),
                        "ACTUALIZAR AHORA", "Pulsa [A] o toca aqui", textBuf, true, 0.80f);

        // Boton gris [B] MAS TARDE
        float btnY2 = cardY + 95.0f;
        Ui::slantButton(btnX, btnY2, btnW, 36.0f, -5.0f, Ui::rgba(120, 115, 140),
                        "MAS TARDE", "Pulsa [B] para continuar", textBuf, false, 0.80f);
    }
    else if (m_state == UpdateState::DOWNLOADING) {
        drawCenteredText(textBuf, screenW * 0.5f, cardY + 42.0f, 0.38f,
                         Ui::rgba(255, 102, 171), "Descargando actualizacion...", 0.85f);
        drawCenteredText(textBuf, screenW * 0.5f, cardY + 72.0f, 0.30f,
                         Ui::rgba(200, 200, 220), "Puedes cancelar en cualquier momento.", 0.85f);

        float btnW = 210.0f;
        float btnH = 38.0f;
        float btnX = cardX + (cardW - btnW) * 0.5f;
        float btnY = cardY + 112.0f;
        Ui::slantButton(btnX, btnY, btnW, btnH, -5.0f, Ui::rgba(255, 77, 109),
                        "CANCELAR", "Pulsa [B] para abortar", textBuf, false, 0.80f);
    }
    else if (m_state == UpdateState::INSTALLING) {
        drawCenteredText(textBuf, screenW * 0.5f, cardY + 55.0f, 0.38f,
                         Ui::rgba(56, 210, 255), "Instalando en sistema...", 0.85f);
        drawCenteredText(textBuf, screenW * 0.5f, cardY + 95.0f, 0.32f,
                         Ui::rgba(200, 200, 220), "Controles bloqueados hasta", 0.85f);
        drawCenteredText(textBuf, screenW * 0.5f, cardY + 115.0f, 0.32f,
                         Ui::rgba(200, 200, 220), "finalizar la instalacion.", 0.85f);
    }
    else if (m_state == UpdateState::COMPLETE) {
        float btnW = 240.0f;
        float btnH = 46.0f;
        float btnX = cardX + (cardW - btnW) * 0.5f;
        float btnY = cardY + 60.0f;
        const char* sub = m_isCia ? "Pulsa [A] para relanzar el juego" : "Pulsa [A] para salir al Menu";
        Ui::slantButton(btnX, btnY, btnW, btnH, -5.0f, Ui::rgba(56, 210, 255),
                        "REINICIAR JUEGO", sub, textBuf, true, 0.80f);
    }
    else if (m_state == UpdateState::FAILED || m_state == UpdateState::UP_TO_DATE) {
        float btnW = 200.0f;
        float btnH = 40.0f;
        float btnX = cardX + (cardW - btnW) * 0.5f;
        float btnY = cardY + 70.0f;
        Ui::slantButton(btnX, btnY, btnW, btnH, -5.0f, Ui::rgba(255, 77, 109),
                        "CERRAR", "Pulsa [B] para volver", textBuf, false, 0.80f);
    }
}

bool AppUpdater::handleInput(u32 kDown, const touchPosition& touch, bool& outRequestQuit) {
    if (!m_modalOpen && m_state != UpdateState::DOWNLOADING && m_state != UpdateState::INSTALLING) {
        return false;
    }

    if (m_state == UpdateState::DOWNLOADING) {
        bool touchedCancel = (kDown & KEY_TOUCH) && (touch.px >= 50 && touch.px <= 270 && touch.py >= 145 && touch.py <= 190);
        if ((kDown & KEY_B) || touchedCancel) {
            cancelOperation();
        }
        return true;
    }

    if (m_state == UpdateState::INSTALLING) {
        return true;
    }

    if (m_state == UpdateState::AVAILABLE) {
        bool touchedBtn1 = (kDown & KEY_TOUCH) && (touch.px >= 40 && touch.px <= 280 && touch.py >= 65 && touch.py <= 115);
        if ((kDown & KEY_A) || touchedBtn1) {
            startInstallAsync();
            return true;
        }

        bool touchedBtn2 = (kDown & KEY_TOUCH) && (touch.px >= 40 && touch.px <= 280 && touch.py >= 130 && touch.py <= 175);
        if ((kDown & KEY_B) || touchedBtn2) {
            closeModal();
            return true;
        }
        return true;
    }

    if (m_state == UpdateState::COMPLETE) {
        bool touchedRestart = (kDown & KEY_TOUCH) && (touch.px >= 40 && touch.px <= 280 && touch.py >= 80 && touch.py <= 145);
        if ((kDown & (KEY_A | KEY_START)) || touchedRestart) {
            if (m_isCia) {
                u64 titleId = 0x0004000000503D00ULL;
                APT_GetProgramID(&titleId);
                if (R_SUCCEEDED(APT_PrepareToDoApplicationJump(0, titleId, MEDIATYPE_SD))) {
                    APT_DoApplicationJump(nullptr, 0, nullptr);
                }
            }
            outRequestQuit = true;
            return true;
        }
        return true;
    }

    if (m_state == UpdateState::FAILED || m_state == UpdateState::UP_TO_DATE) {
        bool touchedClose = (kDown & KEY_TOUCH) && (touch.px >= 60 && touch.px <= 260 && touch.py >= 90 && touch.py <= 145);
        if ((kDown & (KEY_B | KEY_A)) || touchedClose) {
            closeModal();
            return true;
        }
        return true;
    }

    return true;
}
