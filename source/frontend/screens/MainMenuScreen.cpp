#include "MainMenuScreen.hpp"

namespace {

// Dibuja un icono vectorial nitido de corchea musical (♪)
void drawMusicNoteIcon(float cx, float cy, float z, u32 color) {
    C2D_DrawCircleSolid(cx - 2.0f, cy + 2.5f, z, 2.3f, color);
    C2D_DrawCircleSolid(cx + 3.0f, cy + 1.5f, z, 2.3f, color);
    C2D_DrawRectSolid(cx - 0.5f, cy - 4.5f, z, 1.3f, 7.0f, color);
    C2D_DrawRectSolid(cx + 4.5f, cy - 5.5f, z, 1.3f, 7.0f, color);
    C2D_DrawLine(cx - 0.5f, cy - 4.0f, color, cx + 5.5f, cy - 5.0f, color, 2.0f, z);
}

// Dibuja una estrella vectorial de 5 puntas (★)
void drawStarIcon(float cx, float cy, float r, float z, u32 color) {
    for (int i = 0; i < 5; ++i) {
        float a0 = -1.5707963f + i * 1.2566371f;
        float a1 = a0 + 0.6283185f;
        float a2 = a0 - 0.6283185f;
        float xTip = cx + std::cos(a0) * r;
        float yTip = cy + std::sin(a0) * r;
        float xR   = cx + std::cos(a1) * (r * 0.42f);
        float yR   = cy + std::sin(a1) * (r * 0.42f);
        float xL   = cx + std::cos(a2) * (r * 0.42f);
        float yL   = cy + std::sin(a2) * (r * 0.42f);
        C2D_DrawTriangle(cx, cy, color, xL, yL, color, xTip, yTip, color, z);
        C2D_DrawTriangle(cx, cy, color, xTip, yTip, color, xR, yR, color, z);
    }
}

// Dibuja la galleta / medallon osu! con anillos, brillo y texto centrado
void drawOsuMedallion(C2D_TextBuf textBuf, float cx, float cy, float radius,
                      float beat, int animTick, bool drawPulseRings, float textScale) {
    u32 pinkMain = Ui::rgba(255, 102, 171); // #FF66AB
    u32 pinkDeep = Ui::rgba(184, 23, 85);   // #B81755
    u32 whiteClr = Ui::rgba(255, 255, 255);

    if (drawPulseRings) {
        for (int k = 0; k < 2; ++k) {
            float phase = std::fmod(animTick * 0.014f + k * 0.5f, 1.0f);
            u8 ringAlpha = static_cast<u8>((1.0f - phase) * 85.0f);
            Ui::ring(cx, cy, 0.18f, radius + 4.0f + phase * 26.0f, 1.8f, Ui::withAlpha(pinkMain, ringAlpha), 36);
        }
    }

    // Anillo exterior pulsante + resplandor suave
    Ui::ring(cx, cy, 0.20f, radius + 8.0f + beat * 4.0f, 1.6f, Ui::withAlpha(pinkMain, 70), 36);
    Ui::glow(cx, cy, 0.22f, radius + 12.0f, Ui::withAlpha(pinkMain, static_cast<u8>(95 + beat * 35)), 4);

    // Sombra suave bajo el medallon
    C2D_DrawCircleSolid(cx + 2.0f, cy + 4.0f, 0.23f, radius, Ui::rgba(0, 0, 0, 120));

    // Borde blanco brillante + cuerpo rosa osu! + centro magenta profundo
    float borderW = std::max(2.5f, radius * 0.065f);
    float innerOffset = std::max(5.0f, radius * 0.14f);
    C2D_DrawCircleSolid(cx, cy, 0.25f, radius, whiteClr);
    C2D_DrawCircleSolid(cx, cy, 0.28f, radius - borderW, pinkMain);
    C2D_DrawCircleSolid(cx, cy, 0.30f, radius - innerOffset, pinkDeep);

    // Reflejo superior sutil estilo cristal
    C2D_DrawCircleSolid(cx - radius * 0.18f, cy - radius * 0.22f, 0.31f, radius * 0.48f, Ui::rgba(255, 255, 255, 22));

    // Texto central "Circl"
    drawCenteredTextWithShadow(textBuf, cx, cy - 2.0f, textScale, whiteClr, "Circl", 0.35f, Ui::rgba(0, 0, 0, 140));
}

} // namespace

void runMainMenuScreen(AppContext& ctx) {
    ctx.audio.update();
    if (ctx.bgLoader.poll(ctx.bgTex)) ctx.bgFade = 0.0f;
    ctx.bgFade = Ui::approach(ctx.bgFade, 255.0f, 0.10f);

    // Continuar reproduciendo musica del menu sin quedarse en silencio
    if (ctx.settings.songPreview && !ctx.audio.isPreviewLoading() && (ctx.audio.isSongFinished() || !ctx.audio.getIsPlaying())) {
        ctx.rotateMenuSong();
    }

    if (ctx.kDown & KEY_START) {
        ctx.shouldQuit = true;
        return;
    }

    // Comprobar automaticamente actualizaciones en segundo plano si hay Wi-Fi activo
    if (ctx.updater.getState() == UpdateState::IDLE) {
        if (osGetWifiStrength() > 0) {
            ctx.updater.checkForUpdatesAsync();
        }
    }

    // Modal de actualizacion: intercepta controles antes que el menu
    bool updaterRequestQuit = false;
    if (ctx.updater.handleInput(ctx.kDown, ctx.touch, updaterRequestQuit)) {
        if (updaterRequestQuit) {
            ctx.shouldQuit = true;
            return;
        }
    }

    bool updaterModalActive = ctx.updater.isModalOpen() ||
                              ctx.updater.getState() == UpdateState::DOWNLOADING ||
                              ctx.updater.getState() == UpdateState::INSTALLING;

    // Tecla [X]: Comprobar o abrir panel de actualizacion
    if (!updaterModalActive && (ctx.kDown & KEY_X)) {
        ctx.audio.playHitSound(2);
        if (ctx.updater.getState() == UpdateState::IDLE ||
            ctx.updater.getState() == UpdateState::FAILED ||
            ctx.updater.getState() == UpdateState::UP_TO_DATE) {
            ctx.updater.checkForUpdatesAsync();
        }
        ctx.updater.openModal();
    }

    if (!updaterModalActive) {
        // Acceso directo rapido a Perfil & StreetPass con tecla Y o SELECT
        if (ctx.kDown & (KEY_Y | KEY_SELECT)) {
            ctx.audio.playHitSound(2);
            ctx.profileMgr.selectedRivalIndex = -1;
            ctx.profileMgr.loadAvatar();
            ctx.profileMgr.refreshStats(ctx.scoreMgr, ctx.songMgr);
            ctx.appState = STATE_PROFILE;
            return;
        }

        // Cambiar cancion del menu manualmente con L/R o Cruceta Izq/Der
        if (!ctx.songMgr.sets.empty() && (ctx.kDown & (KEY_L | KEY_R | KEY_DLEFT | KEY_DRIGHT))) {
            int dir = (ctx.kDown & (KEY_R | KEY_DRIGHT)) ? 1 : -1;
            int nSets = static_cast<int>(ctx.songMgr.sets.size());
            ctx.selectedSet = (ctx.selectedSet + dir + nSets) % nSets;
            ctx.selectedDiff = 0;
            ctx.carouselPos = static_cast<float>(ctx.selectedSet);
            ctx.audio.playHitSound(0);
            ctx.updateSongPreview();
        }

        bool activateOption = false;

        if (!ctx.mainMenuExpanded) {
            // ESTADO 1 (Contraido): Tocar el circulo central o pulsar A despliega las opciones
            if (ctx.kDown & KEY_A) {
                ctx.mainMenuExpanded = true;
                ctx.audio.playHitSound(2);
            } else if (ctx.touchJustDown) {
                float dx = static_cast<float>(ctx.touch.px) - 160.0f;
                float dy = static_cast<float>(ctx.touch.py) - 112.0f;
                if (dx * dx + dy * dy <= 76.0f * 76.0f) {
                    ctx.mainMenuExpanded = true;
                    ctx.audio.playHitSound(2);
                }
            }
        } else {
            // ESTADO 2 (Desplegado): Navegacion por las 6 opciones (5 principales + Salir)
            if (ctx.kDown & KEY_B) {
                ctx.mainMenuExpanded = false;
                ctx.audio.playHitSound(0);
            } else {
                if (ctx.kDown & KEY_DUP) {
                    ctx.mainMenuOption = (ctx.mainMenuOption + 5) % 6;
                    ctx.audio.playHitSound(0);
                }
                if (ctx.kDown & KEY_DDOWN) {
                    ctx.mainMenuOption = (ctx.mainMenuOption + 1) % 6;
                    ctx.audio.playHitSound(0);
                }
                if (ctx.kDown & KEY_A) {
                    activateOption = true;
                }

                if (ctx.touchJustDown) {
                    // Toque en el circulo ancla izquierdo
                    float dx = static_cast<float>(ctx.touch.px) - 46.0f;
                    float dy = static_cast<float>(ctx.touch.py) - 120.0f;
                    if (dx * dx + dy * dy <= 42.0f * 42.0f) {
                        ctx.mainMenuExpanded = false;
                        ctx.audio.playHitSound(0);
                    } else if (ctx.touch.px >= 92 && ctx.touch.px <= 316) {
                        // 5 botones principales (y: 12 + i * 35)
                        for (int i = 0; i < 5; ++i) {
                            float by = 12.0f + i * 35.0f;
                            if (ctx.touch.py >= by && ctx.touch.py <= by + 32.0f) {
                                ctx.mainMenuOption = i;
                                activateOption = true;
                                break;
                            }
                        }
                        // 6to boton Salir (y: 190..222)
                        if (!activateOption && ctx.touch.py >= 190 && ctx.touch.py <= 222) {
                            ctx.mainMenuOption = 5;
                            activateOption = true;
                        }
                    }
                }
            }
        }

        if (activateOption) {
            ctx.audio.playHitSound(2);
            if (ctx.mainMenuOption == 0) {
                ctx.appState = STATE_SONG_SELECT;
                ctx.updateSongPreview();
            } else if (ctx.mainMenuOption == 1) {
                ctx.profileMgr.selectedRivalIndex = -1;
                ctx.profileMgr.loadAvatar();
                ctx.profileMgr.refreshStats(ctx.scoreMgr, ctx.songMgr);
                ctx.appState = STATE_PROFILE;
            } else if (ctx.mainMenuOption == 2) {
                ctx.multiMgr.startBrowser(ctx.sysUser);
                ctx.appState = STATE_MULTI_BROWSER;
            } else if (ctx.mainMenuOption == 3) {
                ctx.audio.stopMusic();
                ctx.multiMgr.stop();
                ctx.wifiReturnState = STATE_MAIN_MENU;
                ctx.wifiServer.start();
                ctx.appState = STATE_WIFI_TRANSFER;
            } else if (ctx.mainMenuOption == 4) {
                ctx.settingsReturnState = STATE_MAIN_MENU;
                ctx.calibrationMode = false;
                ctx.appState = STATE_SETTINGS;
            } else if (ctx.mainMenuOption == 5) {
                ctx.shouldQuit = true;
                return;
            }
        }
    }

    ctx.mainMenuExpandAnim = Ui::approach(ctx.mainMenuExpandAnim, ctx.mainMenuExpanded ? 1.0f : 0.0f, 0.22f);

    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);

    float beat = ctx.getBpmPulse();
    u32 pinkAccent = Ui::rgba(255, 102, 171); // #FF66AB

    // =========================================================================
    // PANTALLA SUPERIOR (400x240, 3D Estereoscopico)
    // =========================================================================
    for (int eye = 0; eye < (ctx.stereoActive ? 2 : 1); ++eye) {
        C3D_RenderTarget* curTop = (eye == 0) ? ctx.topScreen : ctx.topScreenRight;
        float eyeSign = ctx.stereoActive ? ((eye == 0) ? 1.0f : -1.0f) : 0.0f;
        C2D_TargetClear(curTop, Ui::rgba(10, 7, 20));
        C2D_SceneBegin(curTop);
        auto depthShift = [&](float px) { return eyeSign * ctx.slider3D * px; };

        // 1. Fondo estilo osu!lazer con degradado oscuro y geometria flotante
        float dBg = depthShift(-3.0f);
        Ui::backdrop(400.0f, 240.0f, static_cast<float>(ctx.animTick),
                     Ui::rgba(20, 13, 36), Ui::rgba(10, 7, 20), pinkAccent, 0.01f);
        if (ctx.bgTex.loaded) {
            ctx.bgTex.drawRect(dBg - beat * 2.0f, -beat * 1.5f,
                               400.0f + beat * 4.0f, 240.0f + beat * 3.0f,
                               0.02f, static_cast<u8>(ctx.bgFade * 0.45f));
            C2D_DrawRectSolid(0.0f, 0.0f, 0.03f, 400.0f, 240.0f, Ui::rgba(14, 9, 24, 175));
        }

        // 2. Cabecera superior estilo osu!lazer
        char fpsHdr[16];
        std::snprintf(fpsHdr, sizeof(fpsHdr), "%d FPS", std::max(1, std::min(60, static_cast<int>(std::round(ctx.currentFps)))));
        Ui::lazerHeader(400.0f, 0.30f, "Circl3DS", fpsHdr, pinkAccent, ctx.textBuf, true);

        if (ctx.updater.getState() == UpdateState::AVAILABLE) {
            char upBadge[48];
            std::snprintf(upBadge, sizeof(upBadge), "[X] v%s DISPONIBLE", ctx.updater.getRemoteVersion().c_str());
            drawTextWithShadow(ctx.textBuf, 220.0f + depthShift(1.0f), 7.0f, 0.33f, Ui::rgba(255, 205, 55), upBadge, 0.35f);
        }

        // 3. Medallon central osu! pulsante en 3D
        float dLogo = depthShift(3.5f + beat * 1.2f);
        float topCx = 200.0f + dLogo;
        float topCy = 100.0f;
        float topRadius = 46.0f + beat * 4.0f;
        drawOsuMedallion(ctx.textBuf, topCx, topCy, topRadius, beat, ctx.animTick, true, 0.96f + beat * 0.04f);

        // 4. Lema y subtitulo inferior bajo el medallon
        float dText = depthShift(2.0f);
        drawCenteredTextWithShadow(ctx.textBuf, 200.0f + dText, 164.0f, 0.42f,
                                   ctx.clrWhite, "Rhythm is just a tap away!", 0.35f);
        drawCenteredTextWithShadow(ctx.textBuf, 200.0f + dText, 181.0f, 0.34f,
                                   pinkAccent, ctx.mainMenuExpanded ? LOC(STR_MENU_PROMPT_EXPANDED) : LOC(STR_MENU_PROMPT_COLLAPSED), 0.35f);

        // 5. Barra inferior de cancion en reproduccion
        float barY = 212.0f;
        C2D_DrawRectSolid(0.0f, barY, 0.30f, 400.0f, 28.0f, Ui::rgba(14, 10, 24, 240));
        C2D_DrawRectSolid(0.0f, barY, 0.31f, 400.0f, 1.0f, Ui::withAlpha(pinkAccent, 85));

        std::string songTitle  = "Circles!";
        std::string songArtist = "Nekodex";
        int songBpm = 175;
        float songStars = 2.40f;

        if (!ctx.songMgr.sets.empty() && ctx.selectedSet >= 0 && ctx.selectedSet < static_cast<int>(ctx.songMgr.sets.size())) {
            const auto& curSet = ctx.songMgr.sets[ctx.selectedSet];
            songTitle  = curSet.title;
            songArtist = curSet.artist;
            if (!curSet.difficulties.empty()) {
                int dIdx = std::max(0, std::min(ctx.selectedDiff, static_cast<int>(curSet.difficulties.size()) - 1));
                const auto& diff = curSet.difficulties[dIdx];
                songBpm   = static_cast<int>(std::round(diff.getBPM()));
                songStars = diff.starRating;
            }
        }

        float dBar = depthShift(1.2f);
        drawMusicNoteIcon(14.0f + dBar, barY + 14.0f, 0.35f, pinkAccent);

        std::string truncTitle = truncateString(songTitle, 22);
        drawTextWithShadow(ctx.textBuf, 25.0f + dBar, barY + 7.5f, 0.37f, ctx.clrWhite, truncTitle.c_str(), 0.35f);

        // Medir el ancho del titulo para colocar (Artist) justo al lado
        C2D_Text tmpTitle;
        C2D_TextParse(&tmpTitle, ctx.textBuf, truncTitle.c_str());
        float titleW = 0.0f, titleH = 0.0f;
        C2D_TextGetDimensions(&tmpTitle, 0.37f, 0.37f, &titleW, &titleH);

        std::string artistParen = "(" + truncateString(songArtist, 22) + ")";
        drawText(ctx.textBuf, 30.0f + titleW + dBar, barY + 8.0f, 0.34f, Ui::rgba(160, 155, 185), artistParen.c_str(), 0.35f);

        // BPM en cian + Estrella dorada a la derecha
        char bpmBuf[24];
        std::snprintf(bpmBuf, sizeof(bpmBuf), "%d BPM", songBpm);
        drawText(ctx.textBuf, 294.0f + dBar, barY + 8.0f, 0.34f, Ui::rgba(56, 210, 255), bpmBuf, 0.35f);

        C2D_DrawCircleSolid(344.0f + dBar, barY + 14.0f, 0.35f, 1.3f, Ui::rgba(130, 125, 155));

        drawStarIcon(354.0f + dBar, barY + 14.0f, 4.2f, 0.35f, Ui::rgba(255, 195, 40));
        char starBuf[16];
        std::snprintf(starBuf, sizeof(starBuf), "%.2f", songStars);
        drawTextWithShadow(ctx.textBuf, 361.0f + dBar, barY + 8.0f, 0.35f, Ui::rgba(255, 195, 40), starBuf, 0.35f);

        // Renderizado del modal de actualizacion en la pantalla superior
        ctx.updater.renderTopScreen(ctx.textBuf, ctx.slider3D, ctx.stereoActive);

        Ui::drawTransition(400.0f, 240.0f, ctx.screenFade, ctx.clrPrimary);
    }

    // =========================================================================
    // PANTALLA INFERIOR (320x240, Tactil: Estado 1 Circulo / Estado 2 Desplegado)
    // =========================================================================
    C2D_TargetClear(ctx.bottomScreen, Ui::rgba(10, 6, 20));
    C2D_SceneBegin(ctx.bottomScreen);

    Ui::backdrop(320.0f, 240.0f, static_cast<float>(ctx.animTick),
                 Ui::rgba(16, 10, 30), Ui::rgba(10, 6, 20), pinkAccent, 0.01f);

    float tExp = ctx.mainMenuExpandAnim;

    // Interpolacion suave del circulo osu! entre Estado 1 y Estado 2
    float botCx     = 160.0f * (1.0f - tExp) + 46.0f * tExp;
    float botCy     = 112.0f * (1.0f - tExp) + 120.0f * tExp;
    float botRadius = (54.0f + beat * 4.5f) * (1.0f - tExp) + (35.0f + beat * 2.0f) * tExp;
    float botTxtScl = 1.10f * (1.0f - tExp) + 0.72f * tExp;

    drawOsuMedallion(ctx.textBuf, botCx, botCy, botRadius, beat, ctx.animTick, !ctx.mainMenuExpanded, botTxtScl);

    if (tExp < 0.5f) {
        // Texto inferior del Estado 1 (Contraido)
        u8 promptAlpha = static_cast<u8>((1.0f - tExp * 2.0f) * (195.0f + beat * 60.0f));
        drawCenteredTextWithShadow(ctx.textBuf, 160.0f, 186.0f, 0.40f,
                                   Ui::rgba(255, 255, 255, promptAlpha),
                                   LOC(STR_MENU_TAP_CIRCLE), 0.35f);

        if (ctx.updater.getState() == UpdateState::AVAILABLE) {
            char botPrompt[64];
            std::snprintf(botPrompt, sizeof(botPrompt), "[X] ACTUALIZAR A v%s", ctx.updater.getRemoteVersion().c_str());
            drawCenteredText(ctx.textBuf, 160.0f, 218.0f, 0.32f,
                             Ui::rgba(255, 205, 55, static_cast<u8>((1.0f - tExp * 2.0f) * 245.0f)),
                             botPrompt, 0.35f);
        } else {
            drawCenteredText(ctx.textBuf, 160.0f, 218.0f, 0.30f,
                             Ui::rgba(255, 205, 55, static_cast<u8>((1.0f - tExp * 2.0f) * 220.0f)),
                             LOC(STR_MENU_HINT_PROFILE), 0.35f);
        }
    }

    if (tExp > 0.05f) {
        // Estado 2 (Desplegado): Botones inclinados estilo osu!lazer
        float slideOffset = (1.0f - tExp) * 180.0f;
        float bx = 98.0f + slideOffset;
        float bw = 212.0f;
        float slant = -5.0f;

        const char* btnTitles[5] = {
            LOC(STR_MENU_PLAY),
            LOC(STR_MENU_PROFILE_TITLE),
            LOC(STR_MENU_MULTI),
            LOC(STR_MENU_WIFI),
            LOC(STR_MENU_SETTINGS)
        };
        const char* btnSubs[5] = {
            LOC(STR_MENU_SUB_PLAY),
            LOC(STR_MENU_PROFILE_SUB),
            LOC(STR_MENU_SUB_MULTI),
            LOC(STR_MENU_SUB_WIFI),
            LOC(STR_MENU_SUB_SETTINGS)
        };
        u32 btnColors[5] = {
            Ui::rgba(255, 102, 171), // #FF66AB Rosa osu!
            Ui::rgba(255, 195, 45),  // #FFC32D Dorado StreetPass
            Ui::rgba(66, 144, 251),  // #4290FB Azul multijugador
            Ui::rgba(85, 221, 119),  // #55DD77 Verde descargas
            Ui::rgba(147, 85, 232)   // #9355E8 Morado ajustes
        };

        for (int i = 0; i < 5; ++i) {
            float by = 12.0f + i * 35.0f;
            bool sel = (ctx.mainMenuOption == i);
            Ui::slantButton(bx, by, bw, 32.0f, slant, btnColors[i],
                            btnTitles[i], btnSubs[i], ctx.textBuf, sel, 0.25f);
        }

        // 6to boton compacto inclinado: SALIR
        bool exitSel = (ctx.mainMenuOption == 5);
        Ui::slantButton(bx, 190.0f, bw, 28.0f, slant, Ui::rgba(255, 77, 109),
                        LOC(STR_MENU_EXIT), LOC(STR_MENU_SUB_EXIT), ctx.textBuf, exitSel, 0.25f);
    }

    if (ctx.settings.showFps) {
        Ui::drawFpsCounter(320.0f, 240.0f, ctx.currentFps, ctx.currentFrameMs, ctx.textBuf);
    }

    // Renderizado del modal interactivo en pantalla inferior
    ctx.updater.renderBottomScreen(ctx.textBuf);

    Ui::drawTransition(320.0f, 240.0f, ctx.screenFade, ctx.clrPrimary);

    C3D_FrameEnd(0);
}
