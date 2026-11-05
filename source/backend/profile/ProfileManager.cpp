#include "ProfileManager.hpp"
#include "UiKit.hpp"
#include "OszExtractor.hpp"
#include <3ds/mii.h>
#include <3ds/services/frd.h>
#include <3ds/applets/miiselector.h>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <vector>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>

static bool copyFile(const char* src, const char* dst) {
    FILE* in = std::fopen(src, "rb");
    if (!in) return false;
    FILE* out = std::fopen(dst, "wb");
    if (!out) {
        std::fclose(in);
        return false;
    }
    char buf[8192];
    size_t bytes = 0;
    while ((bytes = std::fread(buf, 1, sizeof(buf), in)) > 0) {
        std::fwrite(buf, 1, bytes, out);
    }
    std::fclose(in);
    std::fclose(out);
    return true;
}

u32 ProfileManager::getMiiColorRgba(u8 colorIdx) {
    // Los 12 colores oficiales de Mii en Nintendo 3DS
    static const u32 s_miiColors[12] = {
        Ui::rgba(215, 38, 38),   // 0: Rojo
        Ui::rgba(245, 124, 0),   // 1: Naranja
        Ui::rgba(255, 214, 0),   // 2: Amarillo
        Ui::rgba(139, 195, 74),  // 3: Verde claro (Lime)
        Ui::rgba(56, 142, 60),   // 4: Verde
        Ui::rgba(25, 118, 210),  // 5: Azul
        Ui::rgba(0, 188, 212),   // 6: Celeste (Cyan)
        Ui::rgba(233, 30, 99),   // 7: Rosa
        Ui::rgba(123, 31, 162),  // 8: Morado
        Ui::rgba(121, 85, 72),   // 9: Marrón
        Ui::rgba(224, 224, 224), // 10: Blanco
        Ui::rgba(55, 71, 79)     // 11: Gris oscuro / Negro
    };
    if (colorIdx < 12) return s_miiColors[colorIdx];
    return Ui::rgba(255, 102, 171); // Rosa osu! estándar
}

u32 ProfileManager::getThemeColor() const {
    if (selectedRivalIndex >= 0 && selectedRivalIndex < static_cast<int>(rivals.size())) {
        return getMiiColorRgba(rivals[selectedRivalIndex].packet.miiShirtColor);
    }
    return getMiiColorRgba(mii.favoriteColor);
}

struct MiiImageCandidate {
    std::string path;
    time_t mtime;
    int w;
    int h;
    bool isSquare;
};

void ProfileManager::loadAvatar() {
    avatarTex.unload();
    avatarPath = "";

    // 1. Comprobar si ya existe una foto o avatar en la carpeta del juego
    static const char* customPaths[] = {
        "sdmc:/3ds/circl3ds/avatar.png",
        "sdmc:/3ds/circl3ds/avatar.jpg",
        "sdmc:/3ds/circl3ds/avatar.jpeg",
        "sdmc:/3ds/circl3ds/avatar.bmp",
        "sdmc:/3ds/circl3ds/mii.png",
        "sdmc:/3ds/circl3ds/mii.jpg",
        "sdmc:/3ds/osu-3ds/avatar.png",
        "sdmc:/3ds/osu-3ds/avatar.jpg",
        "sdmc:/3ds/osu-3ds/avatar.jpeg",
        "sdmc:/3ds/osu-3ds/avatar.bmp",
        "sdmc:/3ds/osu-3ds/mii.png",
        "sdmc:/3ds/osu-3ds/mii.jpg"
    };
    for (const char* p : customPaths) {
        FILE* f = std::fopen(p, "rb");
        if (f) {
            std::fclose(f);
            if (avatarTex.loadFromFile(p, 128, 128)) {
                avatarPath = p;
                return;
            }
        }
    }

    // 2. Si no hay avatar local, buscar automáticamente fotos exportadas desde el Editor de Mii en DCIM
    std::vector<MiiImageCandidate> candidates;

    auto scanDir = [&](const std::string& dirPath) {
        DIR* d = opendir(dirPath.c_str());
        if (!d) return;
        struct dirent* ent = nullptr;
        while ((ent = readdir(d)) != nullptr) {
            if (ent->d_name[0] == '.') continue;
            std::string fname = ent->d_name;
            std::string fpath = dirPath + "/" + fname;

            // Ignorar tarjetas exportadas
            if (fname.find("OSU_") != std::string::npos || fname.find("CIRCL3DS_") != std::string::npos || fname.find("C3DS_") != std::string::npos) continue;

            std::string lower = fname;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            if (lower.length() > 4 && (lower.rfind(".jpg") == lower.length() - 4 ||
                                       lower.rfind(".jpeg") == lower.length() - 5 ||
                                       lower.rfind(".png") == lower.length() - 4)) {
                // Verificar si existe un archivo .mpo con el mismo prefijo (foto 3D de cámara)
                std::string baseWithoutExt = fpath.substr(0, fpath.rfind('.'));
                bool hasMpo = false;
                std::string mpo1 = baseWithoutExt + ".MPO";
                std::string mpo2 = baseWithoutExt + ".mpo";
                FILE* mf = std::fopen(mpo1.c_str(), "rb");
                if (mf) { hasMpo = true; std::fclose(mf); }
                else {
                    mf = std::fopen(mpo2.c_str(), "rb");
                    if (mf) { hasMpo = true; std::fclose(mf); }
                }

                struct stat st;
                time_t mt = 0;
                if (stat(fpath.c_str(), &st) == 0) {
                    mt = st.st_mtime;
                }

                int w = 0, h = 0, comp = 0;
                if (stbi_info(fpath.c_str(), &w, &h, &comp) && w > 0 && h > 0) {
                    bool isCamera = hasMpo || (w == 640 && h == 480);
                    bool isSquare = (w == h && w >= 64);
                    // Aceptar imágenes cuadradas de Mii Maker o imágenes que no sean de cámara
                    if (isSquare || !isCamera) {
                        candidates.push_back({fpath, mt, w, h, isSquare});
                    }
                }
            }
        }
        closedir(d);
    };

    DIR* dcimDir = opendir("sdmc:/DCIM");
    if (dcimDir) {
        struct dirent* ent = nullptr;
        while ((ent = readdir(dcimDir)) != nullptr) {
            if (ent->d_name[0] == '.') continue;
            std::string sub = std::string("sdmc:/DCIM/") + ent->d_name;
            struct stat s;
            if (stat(sub.c_str(), &s) == 0 && S_ISDIR(s.st_mode)) {
                scanDir(sub);
            }
        }
        closedir(dcimDir);
    }
    scanDir("sdmc:/DCIM");

    // Ordenar los candidatos:
    // 1. Prioridad: Imágenes cuadradas (1:1, formato típico de Mii Maker)
    // 2. Más recientes primero
    if (!candidates.empty()) {
        std::sort(candidates.begin(), candidates.end(), [](const MiiImageCandidate& a, const MiiImageCandidate& b) {
            if (a.isSquare != b.isSquare) return a.isSquare > b.isSquare;
            return a.mtime > b.mtime;
        });

        for (const auto& cand : candidates) {
            if (avatarTex.loadFromFile(cand.path, 128, 128)) {
                avatarPath = cand.path;
                // Guardar copia local permanente en sdmc:/3ds/circl3ds/avatar.jpg
                OszExtractor::ensureDirectoryRecursive("sdmc:/3ds/circl3ds");
                copyFile(cand.path.c_str(), "sdmc:/3ds/circl3ds/avatar.jpg");
                return;
            }
        }
    }
}

void ProfileManager::init(const std::string& fallbackUsername) {
    mii.name = fallbackUsername.empty() ? "Player3DS" : fallbackUsername;
    mii.favoriteColor = 6; // Celeste / Cyan (el color de Dig9)
    mii.skinColor = 0;
    mii.isFemale = false;
    mii.hasValidMii = false;
    selectedRivalIndex = -1; // Siempre comenzar en Mi Perfil

    bool miiFound = false;

    // 1. Obtener directamente el Mii con la corona (Mii Principal del sistema) mediante el servicio de amigos (FRD)
    if (R_SUCCEEDED(frdInit(false))) {
        FriendMii myMii;
        std::memset(&myMii, 0, sizeof(myMii));
        if (R_SUCCEEDED(FRD_GetMyMii(&myMii))) {
            char nameUtf8[32] = {0};
            int outIdx = 0;
            for (int i = 0; i < 10 && myMii.miiData.mii_name[i] != 0; ++i) {
                u16 ch = myMii.miiData.mii_name[i];
                if (ch >= 32 && ch < 127) {
                    nameUtf8[outIdx++] = static_cast<char>(ch);
                }
            }
            nameUtf8[outIdx] = '\0';
            if (outIdx > 0) {
                mii.name = nameUtf8;
                mii.favoriteColor = myMii.miiData.mii_details.shirt_color;
                mii.skinColor     = myMii.miiData.face_style.skinColor;
                mii.isFemale      = myMii.miiData.mii_details.sex;
                mii.miiId         = myMii.miiData.mii_id;
                mii.bdayMonth     = myMii.miiData.mii_details.bday_month;
                mii.bdayDay       = myMii.miiData.mii_details.bday_day;
                mii.hasValidMii   = true;
                miiFound          = true;
            }
        }
        frdExit();
    }

    // 2. Si FRD no respondió, consultar el bloque de configuración del sistema CFG (bloque 0x00070001)
    if (!miiFound) {
        u8 miiBuf[0xA0];
        std::memset(miiBuf, 0, sizeof(miiBuf));
        if (R_SUCCEEDED(cfguInit())) {
            Result res = CFGU_GetConfigInfoBlk2(sizeof(miiBuf), 0x00070001, miiBuf);
            cfguExit();
            if (R_SUCCEEDED(res)) {
                const MiiData* pMii = reinterpret_cast<const MiiData*>(miiBuf);
                if (pMii->version == 3) {
                    char nameUtf8[32] = {0};
                    int outIdx = 0;
                    for (int i = 0; i < 10 && pMii->mii_name[i] != 0; ++i) {
                        u16 ch = pMii->mii_name[i];
                        if (ch >= 32 && ch < 127) {
                            nameUtf8[outIdx++] = static_cast<char>(ch);
                        }
                    }
                    nameUtf8[outIdx] = '\0';
                    if (outIdx > 0) {
                        mii.name = nameUtf8;
                    }

                    mii.favoriteColor = pMii->mii_details.shirt_color;
                    mii.skinColor     = pMii->face_style.skinColor;
                    mii.isFemale      = pMii->mii_details.sex;
                    mii.miiId         = pMii->mii_id;
                    mii.bdayMonth     = pMii->mii_details.bday_month;
                    mii.bdayDay       = pMii->mii_details.bday_day;
                    mii.hasValidMii   = true;
                    miiFound          = true;
                }
            }
        }
    }

    loadAvatar();
    scanStreetPassInbox();
}

bool ProfileManager::selectMiiWithApplet() {
    MiiSelectorConf conf;
    miiSelectorInit(&conf);
    miiSelectorSetOptions(&conf, MIISELECTOR_TOP);

    MiiSelectorReturn ret;
    std::memset(&ret, 0, sizeof(ret));
    miiSelectorLaunch(&conf, &ret);

    if (ret.no_mii_selected == 0) {
        char nameUtf8[32] = {0};
        miiSelectorReturnGetName(&ret, nameUtf8, sizeof(nameUtf8));
        if (std::strlen(nameUtf8) > 0) {
            mii.name = nameUtf8;
        }
        mii.favoriteColor = ret.mii.mii_details.shirt_color;
        mii.skinColor     = ret.mii.face_style.skinColor;
        mii.isFemale      = ret.mii.mii_details.sex;
        mii.miiId         = ret.mii.mii_id;
        mii.bdayMonth     = ret.mii.mii_details.bday_month;
        mii.bdayDay       = ret.mii.mii_details.bday_day;
        mii.hasValidMii   = true;
        return true;
    }
    return false;
}

void ProfileManager::refreshStats(const ScoreManager& scoreMgr, const SongManager& songMgr) {
    stats = PlayerProfileStats();
    if (scoreMgr.records.empty()) return;

    stats.totalScores = static_cast<int>(scoreMgr.records.size());
    float accSum = 0.0f;
    int accCount = 0;

    for (const auto& r : scoreMgr.records) {
        bool hasSilverMod = (r.bestMods & (MOD_HD | MOD_FL)) != 0;
        
        if (r.bestGrade == "SSH" || (r.bestGrade == "SS" && hasSilverMod)) {
            stats.countSSH++;
        } else if (r.bestGrade == "SS") {
            stats.countSS++;
        } else if (r.bestGrade == "SH" || (r.bestGrade == "S" && hasSilverMod)) {
            stats.countSH++;
        } else if (r.bestGrade == "S") {
            stats.countS++;
        } else if (r.bestGrade == "A") {
            stats.countA++;
        } else if (r.bestGrade == "B") {
            stats.countB++;
        } else if (r.bestGrade == "C") {
            stats.countC++;
        } else {
            stats.countD++;
        }

        stats.totalScoreSum += static_cast<uint64_t>(std::max(0, r.bestScore));
        stats.maxComboEver = std::max(stats.maxComboEver, r.bestCombo);

        if (r.bestAcc > 0.0f) {
            accSum += r.bestAcc;
            accCount++;
        }
    }

    if (accCount > 0) {
        stats.globalAcc = accSum / static_cast<float>(accCount);
    }

    // Cálculo ponderado de PP (Performance Points) estilo osu! oficial (0.95^i)
    std::vector<ScoreRecord> sortedPP = scoreMgr.records;
    std::sort(sortedPP.begin(), sortedPP.end(), [](const ScoreRecord& a, const ScoreRecord& b) {
        return a.bestPP > b.bestPP;
    });

    float totalWeightedPP = 0.0f;
    float weight = 1.0f;
    for (size_t i = 0; i < sortedPP.size() && i < 100; ++i) {
        totalWeightedPP += sortedPP[i].bestPP * weight;
        weight *= 0.95f;
    }
    stats.totalPP = totalWeightedPP;

    // Asignación de Título de Rango
    if (stats.totalPP >= 5000.0f) {
        stats.rankTitle = "Rhythm God";
    } else if (stats.totalPP >= 3200.0f) {
        stats.rankTitle = "Circle Master";
    } else if (stats.totalPP >= 1800.0f) {
        stats.rankTitle = "Speed Demon";
    } else if (stats.totalPP >= 850.0f) {
        stats.rankTitle = "Flow Specialist";
    } else if (stats.totalPP >= 250.0f) {
        stats.rankTitle = "Skilled Clicker";
    } else {
        stats.rankTitle = "Rhythm Novice";
    }

    // Seleccionar las 3 mejores canciones por récord
    std::vector<ScoreRecord> sortedScores = scoreMgr.records;
    std::sort(sortedScores.begin(), sortedScores.end(), [](const ScoreRecord& a, const ScoreRecord& b) {
        return a.bestScore > b.bestScore;
    });

    stats.topSongs.clear();
    for (size_t i = 0; i < sortedScores.size() && stats.topSongs.size() < 3; ++i) {
        const auto& r = sortedScores[i];
        TopSongRecord top;
        top.score = r.bestScore;
        top.acc = r.bestAcc;
        top.grade = r.bestGrade;
        top.mods = r.bestMods;
        top.pp = r.bestPP;

        bool found = false;
        for (const auto& set : songMgr.sets) {
            for (const auto& diff : set.difficulties) {
                std::string k = ScoreManager::makeKey(diff.osuPath);
                if (k == r.osuKey) {
                    top.title = set.title;
                    top.artist = set.artist;
                    top.version = diff.version;
                    found = true;
                    break;
                }
            }
            if (found) break;
        }

        if (!found) {
            top.title = r.osuKey;
            top.artist = "Circl3DS";
            top.version = "Normal";
        }

        stats.topSongs.push_back(top);
    }
}

bool ProfileManager::exportStreetPassOutbox() {
    OszExtractor::ensureDirectoryRecursive("sdmc:/3ds/circl3ds/streetpass");

    OsuStreetPassPacket packet;
    std::memset(&packet, 0, sizeof(packet));
    packet.magic = 0x50533343; // 'C3SP'
    packet.version = 1;
    packet.miiShirtColor = mii.favoriteColor;

    std::strncpy(packet.playerName, mii.name.c_str(), sizeof(packet.playerName) - 1);
    packet.totalScore = static_cast<u32>(std::min<uint64_t>(stats.totalScoreSum, 0xFFFFFFFFULL));
    packet.totalPP    = stats.totalPP;
    packet.avgAcc     = stats.globalAcc;
    packet.countSSH   = static_cast<u16>(stats.countSSH);
    packet.countSS    = static_cast<u16>(stats.countSS);
    packet.countSH    = static_cast<u16>(stats.countSH);
    packet.countS     = static_cast<u16>(stats.countS);
    packet.countA     = static_cast<u16>(stats.countA);
    packet.countB     = static_cast<u16>(stats.countB);
    packet.countC     = static_cast<u16>(stats.countC);
    packet.countD     = static_cast<u16>(stats.countD);
    packet.totalPlays = static_cast<u16>(stats.totalScores);
    packet.maxCombo   = static_cast<u16>(stats.maxComboEver);

    if (!stats.topSongs.empty()) {
        const auto& t = stats.topSongs[0];
        std::strncpy(packet.topMapTitle, t.title.c_str(), sizeof(packet.topMapTitle) - 1);
        packet.topMapScore = static_cast<u32>(t.score);
        std::strncpy(packet.topMapGrade, t.grade.c_str(), sizeof(packet.topMapGrade) - 1);
    } else {
        std::strncpy(packet.topMapTitle, "Circles!", sizeof(packet.topMapTitle) - 1);
        std::strncpy(packet.topMapGrade, "A", sizeof(packet.topMapGrade) - 1);
    }

    std::strncpy(packet.rankTitle, stats.rankTitle.c_str(), sizeof(packet.rankTitle) - 1);

    FILE* f = std::fopen("sdmc:/3ds/circl3ds/streetpass/outbox.bin", "wb");
    if (!f) return false;
    size_t written = std::fwrite(&packet, 1, sizeof(packet), f);
    std::fclose(f);
    return (written == sizeof(packet));
}

int ProfileManager::scanStreetPassInbox() {
    rivals.clear();
    const std::string inboxDir = "sdmc:/3ds/circl3ds/streetpass/inbox";
    DIR* d = opendir(inboxDir.c_str());
    if (!d) {
        d = opendir("sdmc:/3ds/osu-3ds/streetpass/inbox");
    }
    if (!d) return 0;

    struct dirent* ent = nullptr;
    while ((ent = readdir(d)) != nullptr) {
        if (ent->d_name[0] == '.') continue;
        std::string fname = ent->d_name;
        if (fname.size() > 4 && fname.substr(fname.size() - 4) == ".bin") {
            std::string fullPath = inboxDir + "/" + fname;
            FILE* f = std::fopen(fullPath.c_str(), "rb");
            if (f) {
                OsuStreetPassPacket pkt;
                if (std::fread(&pkt, 1, sizeof(pkt), f) == sizeof(pkt)) {
                    if (pkt.magic == 0x50533343 || pkt.magic == 0x5055534F) {
                        StreetPassRival riv;
                        riv.packet = pkt;
                        riv.receivedTime = fname.substr(0, fname.size() - 4);
                        rivals.push_back(riv);
                    }
                }
                std::fclose(f);
            }
        }
    }
    closedir(d);
    return static_cast<int>(rivals.size());
}

void ProfileManager::clearRivals() {
    rivals.clear();
    selectedRivalIndex = -1;

    // Eliminar los archivos mock de la bandeja de entrada
    const char* inboxes[] = { "sdmc:/3ds/circl3ds/streetpass/inbox", "sdmc:/3ds/osu-3ds/streetpass/inbox" };
    for (const char* inboxDir : inboxes) {
        DIR* d = opendir(inboxDir);
        if (d) {
            struct dirent* ent = nullptr;
            while ((ent = readdir(d)) != nullptr) {
                if (ent->d_name[0] == '.') continue;
                std::string p = std::string(inboxDir) + "/" + ent->d_name;
                std::remove(p.c_str());
            }
            closedir(d);
        }
    }
}

bool ProfileManager::exportCardImage(const std::string& customPath) {
    u16 fbW = 0, fbH = 0;
    u8* fb = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, &fbW, &fbH);
    if (!fb) return false;

    const int outW = 400;
    const int outH = 240;

    #pragma pack(push, 1)
    struct BmpHeader {
        u16 bfType = 0x4D42; // 'BM'
        u32 bfSize = 54 + (outW * outH * 3);
        u16 bfReserved1 = 0;
        u16 bfReserved2 = 0;
        u32 bfOffBits = 54;
        u32 biSize = 40;
        s32 biWidth = outW;
        s32 biHeight = outH;
        u16 biPlanes = 1;
        u16 biBitCount = 24;
        u32 biCompression = 0;
        u32 biSizeImage = outW * outH * 3;
        s32 biXPelsPerMeter = 2835;
        s32 biYPelsPerMeter = 2835;
        u32 biClrUsed = 0;
        u32 biClrImportant = 0;
    } header;
    #pragma pack(pop)

    std::vector<u8> rowBuffer(outW * 3);

    auto writeBmpToFile = [&](const std::string& dest) -> bool {
        FILE* f = std::fopen(dest.c_str(), "wb");
        if (!f) return false;

        std::fwrite(&header, sizeof(header), 1, f);

        for (int y = outH - 1; y >= 0; --y) {
            for (int x = 0; x < outW; ++x) {
                int srcIdx = (x * 240 + (239 - y)) * 3;
                u8 b = fb[srcIdx + 0];
                u8 g = fb[srcIdx + 1];
                u8 r = fb[srcIdx + 2];
                rowBuffer[x * 3 + 0] = b;
                rowBuffer[x * 3 + 1] = g;
                rowBuffer[x * 3 + 2] = r;
            }
            std::fwrite(rowBuffer.data(), 1, rowBuffer.size(), f);
        }
        std::fclose(f);
        return true;
    };

    OszExtractor::ensureDirectoryRecursive("sdmc:/3ds/circl3ds");
    std::string mainPath = customPath.empty() ? "sdmc:/3ds/circl3ds/profile_card.bmp" : customPath;
    bool ok = writeBmpToFile(mainPath);

    // Guardar también copia en la carpeta DCIM de la Nintendo 3DS
    OszExtractor::ensureDirectoryRecursive("sdmc:/DCIM/100NIN03");
    writeBmpToFile("sdmc:/DCIM/100NIN03/CIRCL3DS_CARD.BMP");

    return ok;
}

std::string ProfileManager::generateQrString() const {
    char buf[128];
    std::snprintf(buf, sizeof(buf), "circl3ds:%s|%.1fpp|%.1f%%|SS:%d|S:%d|A:%d",
                  mii.name.c_str(), stats.totalPP, stats.globalAcc,
                  stats.countSSH + stats.countSS, stats.countSH + stats.countS, stats.countA);
    return std::string(buf);
}
