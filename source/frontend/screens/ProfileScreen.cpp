#include "ProfileScreen.hpp"
#include "QrRenderer.hpp"
#include "Draw.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <algorithm>

namespace {

static bool s_showQr = false;
static std::string s_toastMessage = "";
static int s_toastTimer = 0;
static bool s_needExportImage = false;

// Dibuja la corona dorada de Mii Principal (igual a la de Mii Maker)
void drawCrownIcon(float cx, float cy, float z, u32 goldClr) {
    C2D_DrawRectSolid(cx - 8.0f, cy + 2.0f, z, 16.0f, 2.5f, goldClr);
    C2D_DrawTriangle(cx - 8.0f, cy + 2.0f, goldClr, cx - 2.0f, cy + 2.0f, goldClr, cx - 6.0f, cy - 5.0f, goldClr, z);
    C2D_DrawTriangle(cx - 4.0f, cy + 2.0f, goldClr, cx + 4.0f, cy + 2.0f, goldClr, cx, cy - 8.0f, goldClr, z);
    C2D_DrawTriangle(cx + 2.0f, cy + 2.0f, goldClr, cx + 8.0f, cy + 2.0f, goldClr, cx + 6.0f, cy - 5.0f, goldClr, z);
    // Joyas brillantes en las puntas de la corona
    C2D_DrawCircleSolid(cx - 6.0f, cy - 5.0f, z + 0.01f, 1.2f, Ui::rgba(255, 255, 255));
    C2D_DrawCircleSolid(cx, cy - 8.0f, z + 0.01f, 1.5f, Ui::rgba(255, 255, 255));
    C2D_DrawCircleSolid(cx + 6.0f, cy - 5.0f, z + 0.01f, 1.2f, Ui::rgba(255, 255, 255));
}

// Dibuja el marco y el avatar (usando la foto real del Mii si existe, o el emblema coronado de Dig9)
void drawPlayerAvatar(AppContext& ctx, float cx, float cy, float radius, u8 shirtColorIdx, u32 accentClr, float z = 0.20f) {
    u32 shirtClr = ProfileManager::getMiiColorRgba(shirtColorIdx);

    // Resplandor exterior
    Ui::glow(cx, cy, z, radius + 8.0f, Ui::withAlpha(accentClr, 110), 4);
    C2D_DrawCircleSolid(cx, cy, z + 0.01f, radius, Ui::rgba(16, 12, 26));

    if (ctx.profileMgr.avatarTex.loaded) {
        // Si el usuario colocó avatar.png / mii.png, renderizar su foto real
        float imgSize = radius * 1.85f;
        ctx.profileMgr.avatarTex.drawRect(cx - imgSize * 0.5f, cy - imgSize * 0.5f, imgSize, imgSize, z + 0.02f, 255);
    } else {
        // Emblema limpio estilo Mii con corona dorada
        float headY = cy - radius * 0.14f;
        float headR = radius * 0.38f;
        float bodyY = cy + radius * 0.52f;
        float bodyR = radius * 0.60f;

        // Torso / camiseta del color favorito del Mii (celeste en Dig9)
        C2D_DrawCircleSolid(cx, bodyY, z + 0.02f, bodyR, shirtClr);

        // Cabeza estilizada
        C2D_DrawCircleSolid(cx, headY, z + 0.03f, headR, Ui::rgba(255, 224, 189));

        // Cabello negro estilizado con mechones
        u32 hairClr = Ui::rgba(28, 24, 30);
        C2D_DrawCircleSolid(cx, headY - 4.0f, z + 0.04f, headR * 0.95f, hairClr);
        C2D_DrawTriangle(cx - 9.0f, headY - 2.0f, hairClr, cx - 2.0f, headY - 2.0f, hairClr, cx - 7.0f, headY + 3.0f, hairClr, z + 0.05f);
        C2D_DrawTriangle(cx + 2.0f, headY - 2.0f, hairClr, cx + 9.0f, headY - 2.0f, hairClr, cx + 7.0f, headY + 3.0f, hairClr, z + 0.05f);
        C2D_DrawTriangle(cx - 3.0f, headY - 3.0f, hairClr, cx + 3.0f, headY - 3.0f, hairClr, cx - 1.0f, headY + 2.0f, hairClr, z + 0.05f);

        // Pequeño detalle de perilla/barba bajo la boca (como Dig9)
        C2D_DrawLine(cx - 2.5f, headY + headR * 0.65f, hairClr, cx + 2.5f, headY + headR * 0.65f, hairClr, 1.4f, z + 0.05f);

        // Corona dorada de Mii Maker encima del marco
        drawCrownIcon(cx, cy - radius - 1.0f, z + 0.08f, Ui::rgba(255, 215, 45));
    }

    // Anillo exterior brillante
    Ui::ring(cx, cy, z + 0.06f, radius, 2.4f, accentClr, 36);
    Ui::ring(cx, cy, z + 0.07f, radius - 2.0f, 1.0f, Ui::withAlpha(ctx.clrWhite, 70), 32);
}

// Dibuja una placa de rango con su contador (Plata u Oro)
void drawGradePill(C2D_TextBuf buf, float x, float y, float w, float h,
                   const char* grade, int count, bool isSilver, bool isGold, u32 baseClr, float z = 0.25f) {
    u32 bgPill = Ui::rgba(22, 16, 34, 230);
    u32 borderClr = baseClr;

    if (isSilver) {
        borderClr = Ui::rgba(215, 225, 240); // Plata pulida
    } else if (isGold) {
        borderClr = Ui::rgba(255, 215, 50);  // Oro brillante
    }

    Ui::panel(x, y, w, h, z, bgPill, borderClr);

    float tagW = 24.0f;
    u32 tagBg = Ui::withAlpha(borderClr, 45);
    C2D_DrawRectSolid(x + 2.0f, y + 2.0f, z + 0.01f, tagW, h - 4.0f, tagBg);

    drawCenteredTextWithShadow(buf, x + 2.0f + tagW * 0.5f, y + 2.2f, 0.35f, borderClr, grade, z + 0.02f);

    char cntStr[16];
    std::snprintf(cntStr, sizeof(cntStr), "%d", count);
    drawTextWithShadow(buf, x + tagW + 6.0f, y + 2.2f, 0.36f, Ui::rgba(255, 255, 255), cntStr, z + 0.02f);
}

} // namespace

void runProfileScreen(AppContext& ctx) {
    ctx.audio.update();

    if (s_toastTimer > 0) {
        s_toastTimer--;
    }

    // Salir con tecla B
    if (ctx.kDown & KEY_B) {
        ctx.audio.playHitSound(0);
        ctx.appState = STATE_MAIN_MENU;
        s_showQr = false;
        return;
    }

    // Auto-detección: si aún no está cargada la foto, intentar buscarla periódicamente
    if (!ctx.profileMgr.avatarTex.loaded) {
        static int s_autoScanTicks = 0;
        if ((s_autoScanTicks++ % 60) == 0) {
            ctx.profileMgr.loadAvatar();
        }
    }

    // Cambiar entre Mi Perfil y Rivales con L/R o Cruceta
    int totalRivals = static_cast<int>(ctx.profileMgr.rivals.size());
    if (totalRivals > 0 && (ctx.kDown & (KEY_L | KEY_R | KEY_DLEFT | KEY_DRIGHT))) {
        int dir = (ctx.kDown & (KEY_R | KEY_DRIGHT)) ? 1 : -1;
        ctx.profileMgr.selectedRivalIndex += dir;
        if (ctx.profileMgr.selectedRivalIndex >= totalRivals) {
            ctx.profileMgr.selectedRivalIndex = -1; // Vuelve a Mi Perfil
        } else if (ctx.profileMgr.selectedRivalIndex < -1) {
            ctx.profileMgr.selectedRivalIndex = totalRivals - 1;
        }
        ctx.audio.playHitSound(0);
    }

    // [X]: Exportar Imagen a SD y Galería DCIM
    if (ctx.kDown & KEY_X) {
        s_needExportImage = true;
    }

    // [Y]: Exportar StreetPass Outbox
    if (ctx.kDown & KEY_Y) {
        if (ctx.profileMgr.exportStreetPassOutbox()) {
            s_toastMessage = "StreetPass: Buzon Outbox guardado!";
            s_toastTimer = 140;
            ctx.audio.playHitSound(2);
        }
    }

    // [SELECT]: Abrir Selector Oficial de Miis de Nintendo 3DS
    if (ctx.kDown & KEY_SELECT) {
        if (ctx.profileMgr.selectMiiWithApplet()) {
            ctx.profileMgr.loadAvatar();
            s_toastMessage = "Mii cargado: " + ctx.profileMgr.mii.name;
            s_toastTimer = 140;
            ctx.audio.playHitSound(2);
        }
    }

    // Interacción táctil en la pantalla inferior
    if (ctx.touchJustDown) {
        // Toque en el panel del Mii / Perfil para abrir el Selector de Miis
        if (!s_showQr && ctx.touch.py >= 36 && ctx.touch.py <= 130 && ctx.touch.px >= 12 && ctx.touch.px <= 308) {
            if (ctx.profileMgr.selectMiiWithApplet()) {
                ctx.profileMgr.loadAvatar();
                s_toastMessage = "Mii cargado: " + ctx.profileMgr.mii.name;
                s_toastTimer = 140;
                ctx.audio.playHitSound(2);
            }
        }
        // [X] Guardar Imagen (y: 138..168, x: 12..154)
        else if (ctx.touch.py >= 138 && ctx.touch.py <= 168 && ctx.touch.px >= 12 && ctx.touch.px <= 154) {
            s_needExportImage = true;
        }
        // [Y] Buzón StreetPass (y: 138..168, x: 166..308)
        else if (ctx.touch.py >= 138 && ctx.touch.py <= 168 && ctx.touch.px >= 166 && ctx.touch.px <= 308) {
            if (ctx.profileMgr.exportStreetPassOutbox()) {
                s_toastMessage = "StreetPass: Buzon Outbox guardado!";
                s_toastTimer = 140;
                ctx.audio.playHitSound(2);
            }
        }
        // [QR] Ver / Ocultar Código QR (y: 174..202, x: 12..154)
        else if (ctx.touch.py >= 174 && ctx.touch.py <= 202 && ctx.touch.px >= 12 && ctx.touch.px <= 154) {
            s_showQr = !s_showQr;
            ctx.audio.playHitSound(0);
        }
        // Botón derecho fila 2: Limpiar Rivales o Selector de Mii
        else if (ctx.touch.py >= 174 && ctx.touch.py <= 202 && ctx.touch.px >= 166 && ctx.touch.px <= 308) {
            if (totalRivals > 0) {
                ctx.profileMgr.clearRivals();
                s_toastMessage = "Rivales de prueba borrados";
                s_toastTimer = 140;
                ctx.audio.playHitSound(0);
            } else {
                if (ctx.profileMgr.selectMiiWithApplet()) {
                    ctx.profileMgr.loadAvatar();
                    s_toastMessage = "Mii cargado: " + ctx.profileMgr.mii.name;
                    s_toastTimer = 140;
                    ctx.audio.playHitSound(2);
                }
            }
        }
        // [B] Volver al Menú (y: 208..234, x: 12..308)
        else if (ctx.touch.py >= 208 && ctx.touch.py <= 234 && ctx.touch.px >= 12 && ctx.touch.px <= 308) {
            ctx.audio.playHitSound(0);
            ctx.appState = STATE_MAIN_MENU;
            s_showQr = false;
            return;
        }
    }

    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);

    // Datos a mostrar: ¿Perfil propio o Rival recibido?
    bool isRival = (ctx.profileMgr.selectedRivalIndex >= 0 &&
                    ctx.profileMgr.selectedRivalIndex < static_cast<int>(ctx.profileMgr.rivals.size()));

    std::string displayName;
    std::string displayRankTitle;
    u8 shirtColorIdx = ctx.profileMgr.mii.favoriteColor;
    float displayPP = 0.0f;
    float displayAcc = 0.0f;
    int cSSH = 0, cSS = 0, cSH = 0, cS = 0;
    int cA = 0, cB = 0, cC = 0, cD = 0;
    int totalPlays = 0;
    int maxCombo = 0;
    std::string topSongTitle = "";
    int topSongScore = 0;
    std::string topSongGrade = "D";

    if (!isRival) {
        displayName      = ctx.profileMgr.mii.name.empty() ? ctx.sysUser : ctx.profileMgr.mii.name;
        displayRankTitle = ctx.profileMgr.stats.rankTitle;
        shirtColorIdx    = ctx.profileMgr.mii.favoriteColor;
        displayPP        = ctx.profileMgr.stats.totalPP;
        displayAcc       = ctx.profileMgr.stats.globalAcc;
        cSSH             = ctx.profileMgr.stats.countSSH;
        cSS              = ctx.profileMgr.stats.countSS;
        cSH              = ctx.profileMgr.stats.countSH;
        cS               = ctx.profileMgr.stats.countS;
        cA               = ctx.profileMgr.stats.countA;
        cB               = ctx.profileMgr.stats.countB;
        cC               = ctx.profileMgr.stats.countC;
        cD               = ctx.profileMgr.stats.countD;
        totalPlays       = ctx.profileMgr.stats.totalScores;
        maxCombo         = ctx.profileMgr.stats.maxComboEver;
        if (!ctx.profileMgr.stats.topSongs.empty()) {
            topSongTitle = ctx.profileMgr.stats.topSongs[0].title;
            topSongScore = ctx.profileMgr.stats.topSongs[0].score;
            topSongGrade = ctx.profileMgr.stats.topSongs[0].grade;
        }
    } else {
        const auto& pkt  = ctx.profileMgr.rivals[ctx.profileMgr.selectedRivalIndex].packet;
        displayName      = pkt.playerName;
        displayRankTitle = pkt.rankTitle;
        shirtColorIdx    = static_cast<u8>(pkt.miiShirtColor);
        displayPP        = pkt.totalPP;
        displayAcc       = pkt.avgAcc;
        cSSH             = pkt.countSSH;
        cSS              = pkt.countSS;
        cSH              = pkt.countSH;
        cS               = pkt.countS;
        cA               = pkt.countA;
        cB               = pkt.countB;
        cC               = pkt.countC;
        cD               = pkt.countD;
        totalPlays       = pkt.totalPlays;
        maxCombo         = pkt.maxCombo;
        topSongTitle     = pkt.topMapTitle;
        topSongScore     = pkt.topMapScore;
        topSongGrade     = pkt.topMapGrade;
    }

    u32 accentClr = ProfileManager::getMiiColorRgba(shirtColorIdx);

    // =========================================================================
    // PANTALLA SUPERIOR (400x240, 3D Estereoscópico)
    // =========================================================================
    for (int eye = 0; eye < (ctx.stereoActive ? 2 : 1); ++eye) {
        C3D_RenderTarget* curTop = (eye == 0) ? ctx.topScreen : ctx.topScreenRight;
        float eyeSign = ctx.stereoActive ? ((eye == 0) ? 1.0f : -1.0f) : 0.0f;
        auto depthShift = [&](float px) { return eyeSign * ctx.slider3D * px; };

        C2D_TargetClear(curTop, Ui::rgba(10, 7, 20));
        C2D_SceneBegin(curTop);

        Ui::backdrop(400.0f, 240.0f, static_cast<float>(ctx.animTick),
                     Ui::rgba(22, 14, 38), Ui::rgba(10, 6, 20), accentClr, 0.01f);

        // Cabecera superior
        std::string hdrTag = isRival ? "RIVAL STREETPASS" : "MI PERFIL 3DS";
        Ui::lazerHeader(400.0f, 0.15f, "Circl3DS Card", hdrTag.c_str(), accentClr, ctx.textBuf, true);

        float dCard = depthShift(2.5f);
        float cardX = 10.0f + dCard;
        float cardY = 22.0f;
        float cardW = 380.0f;
        float cardH = 210.0f;

        // Marco principal
        Ui::panel(cardX, cardY, cardW, cardH, 0.20f, Ui::rgba(15, 11, 26, 245), accentClr);

        // ---------------------------------------------------------------------
        // COLUMNA 1: AVATAR MII, NOMBRE Y BADGES (x: cardX + 8 .. cardX + 104)
        // ---------------------------------------------------------------------
        float dMii = depthShift(3.5f);
        float miiCx = cardX + 54.0f + dMii;
        float miiCy = cardY + 46.0f;
        drawPlayerAvatar(ctx, miiCx, miiCy, 30.0f, shirtColorIdx, accentClr, 0.20f);

        // Nombre de usuario en texto claro y nítido
        std::string trName = truncateString(displayName, 14);
        drawCenteredTextWithShadow(ctx.textBuf, cardX + 54.0f + dMii, cardY + 84.0f, 0.42f,
                                   Ui::rgba(255, 255, 255), trName.c_str(), 0.26f);

        // Título de rango (ej. "Rhythm Novice", "Speed Demon")
        Ui::slantRect(cardX + 8.0f, cardY + 102.0f, 92.0f, 16.0f, -4.0f, 0.24f, Ui::withAlpha(accentClr, 200));
        drawCenteredTextWithShadow(ctx.textBuf, cardX + 54.0f, cardY + 103.5f, 0.29f,
                                   Ui::rgba(255, 255, 255), displayRankTitle.c_str(), 0.26f);

        // Badge secundario (separado limpiamente)
        if (isRival) {
            Ui::slantRect(cardX + 8.0f, cardY + 124.0f, 92.0f, 15.0f, -4.0f, 0.24f, Ui::rgba(66, 144, 251, 200));
            drawCenteredText(ctx.textBuf, cardX + 54.0f, cardY + 125.0f, 0.27f, Ui::rgba(255, 255, 255), "ENCONTRADO", 0.26f);
        } else {
            char lvlBuf[24];
            int lvl = 1 + static_cast<int>(std::sqrt(displayPP * 0.5f));
            std::snprintf(lvlBuf, sizeof(lvlBuf), "Nivel %d", lvl);
            Ui::slantRect(cardX + 8.0f, cardY + 124.0f, 92.0f, 15.0f, -4.0f, 0.24f, Ui::rgba(45, 38, 65, 200));
            drawCenteredText(ctx.textBuf, cardX + 54.0f, cardY + 125.0f, 0.28f, Ui::rgba(210, 205, 230), lvlBuf, 0.26f);
        }

        // ---------------------------------------------------------------------
        // COLUMNA 2: MEDALLAS DE RANGOS (SS/S Plata y Oro, A, B, C, D) (x: 112..234)
        // ---------------------------------------------------------------------
        float pillW = 56.0f;
        float pillH = 17.0f;
        float colX1 = cardX + 110.0f;
        float colX2 = colX1 + pillW + 6.0f;
        float startPillY = cardY + 10.0f;

        // Fila 1: SS Plateada vs SS Dorada
        drawGradePill(ctx.textBuf, colX1, startPillY, pillW, pillH, "SS", cSSH, true, false, Ui::rgba(215, 225, 240));
        drawGradePill(ctx.textBuf, colX2, startPillY, pillW, pillH, "SS", cSS, false, true, Ui::rgba(255, 215, 50));

        // Fila 2: S Plateada vs S Dorada
        drawGradePill(ctx.textBuf, colX1, startPillY + 22.0f, pillW, pillH, "S", cSH, true, false, Ui::rgba(215, 225, 240));
        drawGradePill(ctx.textBuf, colX2, startPillY + 22.0f, pillW, pillH, "S", cS, false, true, Ui::rgba(255, 215, 50));

        // Fila 3: A y B
        drawGradePill(ctx.textBuf, colX1, startPillY + 44.0f, pillW, pillH, "A", cA, false, false, Ui::rgba(46, 213, 115));
        drawGradePill(ctx.textBuf, colX2, startPillY + 44.0f, pillW, pillH, "B", cB, false, false, Ui::rgba(66, 144, 251));

        // Fila 4: C y D
        drawGradePill(ctx.textBuf, colX1, startPillY + 66.0f, pillW, pillH, "C", cC, false, false, Ui::rgba(180, 105, 255));
        drawGradePill(ctx.textBuf, colX2, startPillY + 66.0f, pillW, pillH, "D", cD, false, false, Ui::rgba(255, 77, 109));

        // ---------------------------------------------------------------------
        // COLUMNA 3: PERFORMANCE, PRECISIÓN Y COMBO (x: 240..370)
        // ---------------------------------------------------------------------
        float dStats = depthShift(1.8f);
        float statsX = cardX + 238.0f + dStats;
        float statsY = cardY + 9.0f;

        Ui::panel(statsX, statsY, 132.0f, 35.0f, 0.22f, Ui::rgba(20, 14, 32, 220), accentClr);
        drawText(ctx.textBuf, statsX + 8.0f, statsY + 3.0f, 0.27f, Ui::rgba(165, 160, 190), "PERFORMANCE", 0.24f);
        char ppBuf[32];
        std::snprintf(ppBuf, sizeof(ppBuf), "%.1f pp", displayPP);
        drawTextWithShadow(ctx.textBuf, statsX + 8.0f, statsY + 14.5f, 0.44f, Ui::rgba(56, 210, 255), ppBuf, 0.25f);

        float myY = statsY + 42.0f;
        char accBuf[32];
        std::snprintf(accBuf, sizeof(accBuf), "Precision:  %.2f%%", displayAcc);
        drawTextWithShadow(ctx.textBuf, statsX + 4.0f, myY, 0.33f, Ui::rgba(235, 235, 250), accBuf, 0.25f);

        char playBuf[32];
        std::snprintf(playBuf, sizeof(playBuf), "Partidas:   %d", totalPlays);
        drawTextWithShadow(ctx.textBuf, statsX + 4.0f, myY + 15.0f, 0.33f, Ui::rgba(235, 235, 250), playBuf, 0.25f);

        char comboBuf[32];
        std::snprintf(comboBuf, sizeof(comboBuf), "Max Combo:  %dx", maxCombo);
        drawTextWithShadow(ctx.textBuf, statsX + 4.0f, myY + 30.0f, 0.33f, Ui::rgba(255, 210, 50), comboBuf, 0.25f);

        // ---------------------------------------------------------------------
        // PANEL INFERIOR: MEJOR CANCIÓN (TOP PLAY)
        // ---------------------------------------------------------------------
        float botY = cardY + 158.0f;
        Ui::panel(cardX + 6.0f, botY, cardW - 12.0f, 44.0f, 0.23f, Ui::rgba(18, 13, 28, 235), accentClr);
        drawText(ctx.textBuf, cardX + 14.0f, botY + 4.0f, 0.28f, accentClr, "MEJOR RECORD (TOP SONG):", 0.25f);

        if (!topSongTitle.empty()) {
            std::string mapName = truncateString(topSongTitle, 24);
            drawTextWithShadow(ctx.textBuf, cardX + 14.0f, botY + 20.0f, 0.38f, Ui::rgba(255, 255, 255), mapName.c_str(), 0.25f);

            char scBuf[32];
            std::snprintf(scBuf, sizeof(scBuf), "%'d pts", topSongScore);
            drawTextWithShadow(ctx.textBuf, cardX + 215.0f, botY + 20.0f, 0.38f, Ui::rgba(56, 210, 255), scBuf, 0.25f);

            u32 gClr = getScoreGradeColor(topSongGrade, ctx.clrCursor);
            if (topSongGrade == "SSH" || topSongGrade == "SH") gClr = Ui::rgba(220, 230, 245);
            drawCenteredTextWithShadow(ctx.textBuf, cardX + 348.0f, botY + 20.0f, 0.44f, gClr, topSongGrade.c_str(), 0.25f);
        } else {
            drawText(ctx.textBuf, cardX + 14.0f, botY + 20.0f, 0.34f, Ui::rgba(150, 145, 175), "Juega una cancion para registrar tu primer record local!", 0.25f);
        }

        // Toast de notificación
        if (s_toastTimer > 0) {
            float tw = 250.0f;
            float th = 24.0f;
            float tx = 200.0f - tw * 0.5f;
            float ty = 204.0f;
            Ui::panel(tx, ty, tw, th, 0.80f, Ui::rgba(14, 10, 24, 250), Ui::rgba(85, 221, 119));
            drawCenteredTextWithShadow(ctx.textBuf, 200.0f, ty + 5.0f, 0.34f, Ui::rgba(255, 255, 255), s_toastMessage.c_str(), 0.85f);
        }
    }

    // =========================================================================
    // PANTALLA INFERIOR (320x240, Táctil)
    // =========================================================================
    C2D_TargetClear(ctx.bottomScreen, Ui::rgba(10, 6, 20));
    C2D_SceneBegin(ctx.bottomScreen);

    Ui::backdrop(320.0f, 240.0f, static_cast<float>(ctx.animTick),
                 Ui::rgba(18, 12, 30), Ui::rgba(10, 6, 20), accentClr, 0.01f);

    Ui::panel(8.0f, 6.0f, 304.0f, 26.0f, 0.20f, Ui::rgba(20, 14, 32, 230), accentClr);
    drawCenteredTextWithShadow(ctx.textBuf, 160.0f, 9.0f, 0.38f, Ui::rgba(255, 255, 255), "PERFIL & STREETPASS", 0.25f);

    if (s_showQr) {
        std::string qrStr = ctx.profileMgr.generateQrString();
        renderQrCode(qrStr.c_str(), 110.0f, 38.0f, 96.0f);
        drawCenteredTextWithShadow(ctx.textBuf, 160.0f, 144.0f, 0.34f, Ui::rgba(255, 255, 255), "Escanea con la camara de tu movil", 0.25f);
        drawCenteredText(ctx.textBuf, 160.0f, 160.0f, 0.28f, Ui::rgba(160, 155, 185), qrStr.c_str(), 0.25f);
    } else {
        Ui::panel(12.0f, 36.0f, 296.0f, 94.0f, 0.20f, Ui::rgba(20, 14, 34, 220), accentClr);

        char cardHeader[64];
        if (!isRival) {
            std::snprintf(cardHeader, sizeof(cardHeader), "Mi Tarjeta Principal: %s", displayName.c_str());
        } else {
            std::snprintf(cardHeader, sizeof(cardHeader), "Rival StreetPass (%s)", displayName.c_str());
        }
        drawCenteredTextWithShadow(ctx.textBuf, 160.0f, 42.0f, 0.38f, accentClr, cardHeader, 0.25f);

        if (!ctx.profileMgr.avatarPath.empty()) {
            std::string dispFname = ctx.profileMgr.avatarPath;
            size_t slashPos = dispFname.rfind('/');
            if (slashPos != std::string::npos) dispFname = dispFname.substr(slashPos + 1);
            char okBuf[64];
            std::snprintf(okBuf, sizeof(okBuf), "● Foto de Mii cargada: %s", dispFname.c_str());
            drawCenteredText(ctx.textBuf, 160.0f, 60.0f, 0.28f, Ui::rgba(85, 221, 119), okBuf, 0.25f);
        } else {
            drawCenteredText(ctx.textBuf, 160.0f, 60.0f, 0.28f, Ui::rgba(255, 210, 50), "Editor de Mii -> Opciones -> Guardar como imagen", 0.25f);
        }

        drawCenteredText(ctx.textBuf, 160.0f, 78.0f, 0.29f, Ui::rgba(215, 215, 235),
                         "Guarda tu Outbox para intercambiar con otras consolas", 0.25f);

        if (totalRivals > 0) {
            char rivMsg[64];
            std::snprintf(rivMsg, sizeof(rivMsg), "[L / R] Cambiar Rival (Tienes %d guardado%s)", totalRivals, totalRivals > 1 ? "s" : "");
            drawCenteredText(ctx.textBuf, 160.0f, 95.0f, 0.30f, Ui::rgba(56, 210, 255), rivMsg, 0.25f);
        } else {
            drawCenteredText(ctx.textBuf, 160.0f, 95.0f, 0.28f, Ui::rgba(150, 145, 175),
                             "Cruza tu 3DS con otros jugadores para recibir tarjetas", 0.25f);
        }

        drawCenteredText(ctx.textBuf, 160.0f, 112.0f, 0.27f, Ui::rgba(255, 205, 55),
                         "[SELECT o Tocar panel] Elegir Mii de tu consola", 0.25f);
    }

    float bW = 142.0f;
    float bH = 30.0f;

    // Fila 1: [X] Guardar Imagen y [Y] Buzón StreetPass
    Ui::slantButton(12.0f, 138.0f, bW, bH, -4.0f, Ui::rgba(255, 102, 171),
                    "[X] Guardar Imagen", "SD & Camara 3DS", ctx.textBuf, false, 0.25f);
    Ui::slantButton(166.0f, 138.0f, bW, bH, -4.0f, Ui::rgba(85, 221, 119),
                    "[Y] Buzon StreetPass", "Actualizar Outbox", ctx.textBuf, false, 0.25f);

    // Fila 2: [QR] Código QR y [👑] Elegir Mii / Borrar Rivales
    Ui::slantButton(12.0f, 174.0f, bW, 26.0f, -4.0f, Ui::rgba(56, 210, 255),
                    s_showQr ? "Ocultar QR" : "Ver Codigo QR", "Compartir movil", ctx.textBuf, false, 0.25f);

    u32 rBtnClr = (totalRivals > 0) ? Ui::rgba(255, 77, 109) : Ui::rgba(255, 205, 55);
    const char* rBtnTitle = (totalRivals > 0) ? "Borrar Rivales" : "[SELECT] Elegir Mii";
    const char* rBtnSub   = (totalRivals > 0) ? "Limpiar tarjetas" : "Selector oficial";
    Ui::slantButton(166.0f, 174.0f, bW, 26.0f, -4.0f, rBtnClr,
                    rBtnTitle, rBtnSub, ctx.textBuf, false, 0.25f);

    // Fila 3: Volver al Menú
    Ui::slantButton(12.0f, 208.0f, 296.0f, 24.0f, -3.0f, Ui::rgba(180, 175, 205),
                    "[B] Volver al Menu", "", ctx.textBuf, false, 0.25f);

    C3D_FrameEnd(0);

    if (s_needExportImage) {
        s_needExportImage = false;
        if (ctx.profileMgr.exportCardImage()) {
            s_toastMessage = "Guardado en SD y Camara 3DS!";
            s_toastTimer = 140;
            ctx.audio.playHitSound(2);
        } else {
            s_toastMessage = "Error al exportar imagen";
            s_toastTimer = 140;
        }
    }
}
