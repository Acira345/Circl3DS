#include "AppUpdater.hpp"
#include "OszExtractor.hpp"
#include "UiKit.hpp"
#include "Draw.hpp"
#include <3ds/env.h>
#include <cstdio>
#include <cstring>
#include <algorithm>

AppUpdater::AppUpdater() {
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
    m_modalOpen = false;
    m_threadRunning = false;
    m_cancelRequested = false;
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

void AppUpdater::checkForUpdatesAsync(const std::string& customUrl) {
    if (m_threadRunning) return;

    m_checkUrl = customUrl.empty()
        ? "https://raw.githubusercontent.com/Acira345/Circl3DS/main/version.json"
        : customUrl;

    m_state = UpdateState::CHECKING;
    m_statusMessage = "Buscando actualizaciones...";
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
        m_statusMessage = "No se pudo crear hilo de red";
    }
}

void AppUpdater::startInstallAsync() {
    if (m_threadRunning) return;
    if (m_state != UpdateState::AVAILABLE) return;

    m_state = UpdateState::DOWNLOADING;
    m_statusMessage = "Iniciando descarga...";
    m_progress = 0.0f;
    m_bytesReceived = 0;
    m_totalBytes = 0;
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
        m_statusMessage = "No se pudo iniciar hilo instalador";
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

    size_t end = start;
    while (end < json.size()) {
        if (json[end] == '\"' && json[end - 1] != '\\') break;
        end++;
    }
    if (end > json.size()) return "";
    return json.substr(start, end - start);
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

bool AppUpdater::httpDownloadToMemory(const std::string& url, std::string& outData, std::string& errOut) {
    Result res = httpcInit(0);
    if (R_FAILED(res)) {
        errOut = "Error al iniciar HTTP (3DS)";
        return false;
    }

    std::string curUrl = url;
    httpcContext context;
    u32 statusCode = 0;
    int maxRedirects = 5;

    while (maxRedirects-- > 0 && !m_cancelRequested) {
        res = httpcOpenContext(&context, HTTPC_METHOD_GET, curUrl.c_str(), 1);
        if (R_FAILED(res)) {
            httpcExit();
            errOut = "Error de conexion SSL/Red";
            return false;
        }

        httpcSetSSLOpt(&context, SSLCOPT_DisableVerify);
        httpcSetKeepAlive(&context, HTTPC_KEEPALIVE_ENABLED);
        httpcAddRequestHeaderField(&context, "User-Agent", "Circl3DS-Updater/1.0 (Nintendo 3DS)");
        httpcAddRequestHeaderField(&context, "Connection", "Keep-Alive");

        res = httpcBeginRequest(&context);
        if (R_FAILED(res)) {
            httpcCloseContext(&context);
            httpcExit();
            errOut = "Servidor no responde (SSL)";
            return false;
        }

        statusCode = 0;
        res = httpcGetResponseStatusCode(&context, &statusCode);
        if (R_FAILED(res)) {
            httpcCloseContext(&context);
            httpcExit();
            errOut = "Fallo al leer estado HTTP";
            return false;
        }

        if ((statusCode >= 301 && statusCode <= 303) || (statusCode >= 307 && statusCode <= 308)) {
            char newUrlBuf[2048] = "";
            res = httpcGetResponseHeader(&context, "Location", newUrlBuf, sizeof(newUrlBuf));
            httpcCloseContext(&context);
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
        httpcCloseContext(&context);
        httpcExit();
        errOut = "Operacion cancelada";
        return false;
    }

    if (statusCode != 200) {
        httpcCloseContext(&context);
        httpcExit();
        char errBuf[64];
        std::snprintf(errBuf, sizeof(errBuf), "HTTP error %lu", (unsigned long)statusCode);
        errOut = errBuf;
        return false;
    }

    u32 contentSize = 0;
    httpcGetDownloadSizeState(&context, NULL, &contentSize);

    outData.clear();
    std::vector<u8> buffer(16 * 1024);
    u32 bytesRead = 0;

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
    Result res = httpcInit(0);
    if (R_FAILED(res)) {
        errOut = "Error al iniciar HTTP (3DS)";
        return false;
    }

    std::string curUrl = url;
    httpcContext context;
    u32 statusCode = 0;
    int maxRedirects = 5;

    while (maxRedirects-- > 0 && !m_cancelRequested) {
        res = httpcOpenContext(&context, HTTPC_METHOD_GET, curUrl.c_str(), 1);
        if (R_FAILED(res)) {
            httpcExit();
            errOut = "Error de conexion";
            return false;
        }

        httpcSetSSLOpt(&context, SSLCOPT_DisableVerify);
        httpcSetKeepAlive(&context, HTTPC_KEEPALIVE_ENABLED);
        httpcAddRequestHeaderField(&context, "User-Agent", "Circl3DS-Updater/1.0 (Nintendo 3DS)");
        httpcAddRequestHeaderField(&context, "Connection", "Keep-Alive");

        res = httpcBeginRequest(&context);
        if (R_FAILED(res)) {
            httpcCloseContext(&context);
            httpcExit();
            errOut = "Fallo handshake SSL";
            return false;
        }

        statusCode = 0;
        res = httpcGetResponseStatusCode(&context, &statusCode);
        if (R_FAILED(res)) {
            httpcCloseContext(&context);
            httpcExit();
            errOut = "Fallo al leer cabecera HTTP";
            return false;
        }

        if ((statusCode >= 301 && statusCode <= 303) || (statusCode >= 307 && statusCode <= 308)) {
            char newUrlBuf[2048] = "";
            res = httpcGetResponseHeader(&context, "Location", newUrlBuf, sizeof(newUrlBuf));
            httpcCloseContext(&context);
            if (R_SUCCEEDED(res) && newUrlBuf[0] != '\0') {
                curUrl = newUrlBuf;
                continue;
            }
            httpcExit();
            errOut = "Redireccion fallida";
            return false;
        }
        break;
    }

    if (m_cancelRequested) {
        httpcCloseContext(&context);
        httpcExit();
        errOut = "Descarga cancelada";
        return false;
    }

    if (statusCode != 200) {
        httpcCloseContext(&context);
        httpcExit();
        char errBuf[64];
        std::snprintf(errBuf, sizeof(errBuf), "Servidor respondio %lu", (unsigned long)statusCode);
        errOut = errBuf;
        return false;
    }

    u32 totalSize = 0;
    httpcGetDownloadSizeState(&context, NULL, &totalSize);
    m_totalBytes = totalSize;
    m_bytesReceived = 0;
    m_progress = 0.0f;

    Handle ciaHandle = 0;
    FILE* fp3dsx = nullptr;
    u64 fileOffset = 0;

    if (m_isCia) {
        res = amInit();
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
        OszExtractor::ensureDirectoryRecursive("sdmc:/3ds/circl3ds");
        fp3dsx = std::fopen("sdmc:/3ds/circl3ds/circl3ds.3dsx.tmp", "wb");
        if (!fp3dsx) {
            httpcCloseContext(&context);
            httpcExit();
            errOut = "No se pudo escribir en SD";
            return false;
        }
    }

    std::vector<u8> buffer(32 * 1024);
    u32 bytesRead = 0;

    do {
        if (m_cancelRequested) {
            if (m_isCia && ciaHandle != 0) {
                AM_CancelCIAInstall(ciaHandle);
                amExit();
            }
            if (fp3dsx) {
                std::fclose(fp3dsx);
                std::remove("sdmc:/3ds/circl3ds/circl3ds.3dsx.tmp");
            }
            httpcCloseContext(&context);
            httpcExit();
            errOut = "Instalacion cancelada";
            return false;
        }

        bytesRead = 0;
        res = httpcReceiveData(&context, buffer.data(), buffer.size());
        if (res == (Result)0xD880A7FE) {
            httpcGetDownloadSizeState(&context, &bytesRead, NULL);
        } else if (R_FAILED(res)) {
            if (m_isCia && ciaHandle != 0) {
                AM_CancelCIAInstall(ciaHandle);
                amExit();
            }
            if (fp3dsx) {
                std::fclose(fp3dsx);
                std::remove("sdmc:/3ds/circl3ds/circl3ds.3dsx.tmp");
            }
            httpcCloseContext(&context);
            httpcExit();
            errOut = "Interrupcion de red durante descarga";
            return false;
        } else {
            httpcGetDownloadSizeState(&context, &bytesRead, NULL);
        }

        if (bytesRead > 0) {
            if (m_isCia) {
                u32 written = 0;
                Result wRes = FSFILE_Write(ciaHandle, &written, fileOffset, buffer.data(), bytesRead, FS_WRITE_FLUSH);
                if (R_FAILED(wRes)) {
                    AM_CancelCIAInstall(ciaHandle);
                    amExit();
                    httpcCloseContext(&context);
                    httpcExit();
                    errOut = "Fallo de escritura en SD";
                    return false;
                }
                fileOffset += written;
            } else if (fp3dsx) {
                std::fwrite(buffer.data(), 1, bytesRead, fp3dsx);
            }
            m_bytesReceived += bytesRead;
            if (m_totalBytes > 0) {
                m_progress = std::min(1.0f, static_cast<float>(m_bytesReceived) / static_cast<float>(m_totalBytes));
            }
        }
    } while (res == (Result)0xD880A7FE);

    httpcCloseContext(&context);
    httpcExit();

    if (m_isCia) {
        m_state = UpdateState::INSTALLING;
        m_statusMessage = "Finalizando instalacion en la 3DS...";
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
            std::fclose(fp3dsx);
            std::remove("sdmc:/3ds/circl3ds/circl3ds.3dsx");
            std::rename("sdmc:/3ds/circl3ds/circl3ds.3dsx.tmp", "sdmc:/3ds/circl3ds/circl3ds.3dsx");
        }
    }

    return true;
}

void AppUpdater::runCheck() {
    std::string json;
    std::string err;
    bool ok = httpDownloadToMemory(m_checkUrl, json, err);
    if (!ok) {
        m_state = UpdateState::FAILED;
        m_statusMessage = err;
        m_threadRunning = false;
        return;
    }

    m_remoteVersion = extractJsonString(json, "version");
    m_remoteVersionCode = extractJsonInt(json, "version_code");
    m_changelog = extractJsonString(json, "changelog");
    m_ciaUrl = extractJsonString(json, "cia_url");
    m_3dsxUrl = extractJsonString(json, "3dsx_url");

    if (m_remoteVersionCode > CURRENT_VERSION_CODE) {
        m_state = UpdateState::AVAILABLE;
        m_statusMessage = "¡Nueva version disponible: v" + m_remoteVersion + "!";
        m_modalOpen = true; // Abrir modal para avisar al jugador
    } else {
        m_state = UpdateState::UP_TO_DATE;
        m_statusMessage = "El juego esta actualizado (v" + std::string(CURRENT_VERSION_STR) + ")";
    }

    m_threadRunning = false;
}

void AppUpdater::runInstall() {
    std::string targetUrl = m_isCia ? m_ciaUrl : m_3dsxUrl;
    if (targetUrl.empty()) {
        m_state = UpdateState::FAILED;
        m_statusMessage = "URL de descarga no encontrada";
        m_threadRunning = false;
        return;
    }

    std::string err;
    bool ok = httpDownloadAndInstall(targetUrl, err);
    if (ok) {
        m_state = UpdateState::COMPLETE;
        m_progress = 1.0f;
        m_statusMessage = "¡Actualizado con exito a v" + m_remoteVersion + "!";
    } else {
        m_state = UpdateState::FAILED;
        m_statusMessage = err.empty() ? "Fallo en la instalacion" : err;
    }

    m_threadRunning = false;
}

void AppUpdater::renderTopScreen(C2D_TextBuf textBuf, float slider3D, bool stereoActive, float screenW, float screenH) {
    if (!m_modalOpen && m_state != UpdateState::DOWNLOADING && m_state != UpdateState::INSTALLING) {
        return;
    }

    m_animTick++;

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
        std::snprintf(verBuf, sizeof(verBuf), "v%s  ==>  v%s", CURRENT_VERSION_STR, m_remoteVersion.c_str());
        drawCenteredTextWithShadow(textBuf, cardX + cardW * 0.5f, cardY + 45.0f, 0.46f,
                                   greenOk, verBuf, 0.85f);

        // Caja de notas de version / changelog
        float logX = cardX + 20.0f;
        float logY = cardY + 76.0f;
        float logW = cardW - 40.0f;
        float logH = 58.0f;
        C2D_DrawRectSolid(logX, logY, 0.78f, logW, logH, Ui::rgba(28, 22, 42, 220));
        drawRectOutline(logX, logY, logW, logH, 1.0f, Ui::rgba(255, 102, 171, 70), 0.79f);

        std::string notes = m_changelog.empty() ? "Nuevas mejoras de estabilidad y correccion de errores." : m_changelog;
        drawText(textBuf, logX + 8.0f, logY + 8.0f, 0.31f, Ui::rgba(220, 220, 240), truncateString(notes, 42).c_str(), 0.85f);

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

        // Porcentaje y MB
        char progBuf[64];
        float recMb = static_cast<float>(m_bytesReceived) / (1024.0f * 1024.0f);
        float totMb = static_cast<float>(m_totalBytes) / (1024.0f * 1024.0f);
        std::snprintf(progBuf, sizeof(progBuf), "%.1f MB / %.1f MB  (%d%%)", recMb, totMb, static_cast<int>(m_progress * 100.0f));
        drawCenteredTextWithShadow(textBuf, cardX + cardW * 0.5f, barY + 28.0f, 0.34f, Ui::rgba(255, 255, 255), progBuf, 0.85f);

        drawCenteredText(textBuf, cardX + cardW * 0.5f, cardY + 150.0f, 0.30f,
                         Ui::rgba(255, 205, 55), "No apagues la consola ni extraigas la SD", 0.85f);
    }
    else if (m_state == UpdateState::COMPLETE) {
        drawCenteredTextWithShadow(textBuf, cardX + cardW * 0.5f, cardY + 52.0f, 0.46f,
                                   greenOk, "LISTO PARA REINICIAR", 0.85f);
        drawCenteredText(textBuf, cardX + cardW * 0.5f, cardY + 88.0f, 0.35f,
                         Ui::rgba(240, 240, 255), "Circl3DS ha sido actualizado correctamente.", 0.85f);
        drawCenteredText(textBuf, cardX + cardW * 0.5f, cardY + 115.0f, 0.33f,
                         cyanAccent, "Pulsa [A] para salir al Menu HOME y disfrutar", 0.85f);
    }
    else if (m_state == UpdateState::FAILED) {
        drawCenteredTextWithShadow(textBuf, cardX + cardW * 0.5f, cardY + 48.0f, 0.42f,
                                   Ui::rgba(255, 77, 109), "ERROR EN LA ACTUALIZACION", 0.85f);
        std::string errStr = m_statusMessage.empty() ? "Error desconocido de red" : m_statusMessage;
        drawCenteredText(textBuf, cardX + cardW * 0.5f, cardY + 86.0f, 0.32f,
                         Ui::rgba(220, 220, 220), truncateString(errStr, 40).c_str(), 0.85f);
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
    else if (m_state == UpdateState::DOWNLOADING || m_state == UpdateState::INSTALLING) {
        drawCenteredText(textBuf, screenW * 0.5f, cardY + 50.0f, 0.38f,
                         Ui::rgba(255, 102, 171), "Descargando actualizacion...", 0.85f);
        drawCenteredText(textBuf, screenW * 0.5f, cardY + 95.0f, 0.32f,
                         Ui::rgba(200, 200, 220), "Los controles estan bloqueados", 0.85f);
        drawCenteredText(textBuf, screenW * 0.5f, cardY + 115.0f, 0.32f,
                         Ui::rgba(200, 200, 220), "hasta finalizar la instalacion.", 0.85f);
    }
    else if (m_state == UpdateState::COMPLETE) {
        float btnW = 240.0f;
        float btnH = 46.0f;
        float btnX = cardX + (cardW - btnW) * 0.5f;
        float btnY = cardY + 60.0f;
        Ui::slantButton(btnX, btnY, btnW, btnH, -5.0f, Ui::rgba(56, 210, 255),
                        "REINICIAR JUEGO", "Pulsa [A] para salir al Menu HOME", textBuf, true, 0.80f);
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

    if (m_state == UpdateState::DOWNLOADING || m_state == UpdateState::INSTALLING) {
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
