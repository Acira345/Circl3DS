#pragma once
#include <3ds.h>
#include <citro2d.h>
#include <string>
#include <vector>
#include <cstdint>
#include "ScoreManager.hpp"
#include "SongManager.hpp"
#include "TextureLoader.hpp"

#pragma pack(push, 1)
// Paquete binario compacto de StreetPass para Circl3DS (192 bytes)
struct OsuStreetPassPacket {
    u32 magic;              // 'C3SP' (0x50533343)
    u16 version;            // 1
    u16 miiShirtColor;      // 0..11 (Color oficial de Nintendo)
    char playerName[24];    // Nombre UTF-8 del jugador
    u32 totalScore;         // Puntuación acumulada
    float totalPP;          // Rendimiento general en PP
    float avgAcc;           // Precisión global media (%)
    u16 countSSH;           // SS Plateada (Hidden / Flashlight)
    u16 countSS;            // SS Dorada
    u16 countSH;            // S Plateada
    u16 countS;             // S Dorada
    u16 countA;             // A
    u16 countB;             // B
    u16 countC;             // C
    u16 countD;             // D
    u16 totalPlays;         // Canciones totales registradas
    u16 maxCombo;           // Mayor combo alcanzado
    char topMapTitle[36];   // Título de la mejor canción
    u32 topMapScore;        // Score en la mejor canción
    char topMapGrade[4];    // Grado en la mejor canción (ej. "SSH", "SS", "S")
    char rankTitle[24];     // Título del jugador (ej. "Circle Master")
    u8 padding[36];         // Espacio reservado para compatibilidad
};
#pragma pack(pop)

struct MiiProfileInfo {
    std::string name = "Player3DS";
    u8 favoriteColor = 7; // Rosa por defecto (7 = Pink)
    u8 skinColor = 0;
    bool isFemale = false;
    u32 miiId = 0;
    u8 bdayMonth = 0;
    u8 bdayDay = 0;
    bool hasValidMii = false;
};

struct TopSongRecord {
    std::string title;
    std::string artist;
    std::string version;
    int score = 0;
    float acc = 0.0f;
    std::string grade = "D";
    uint32_t mods = 0;
    float pp = 0.0f;
};

struct PlayerProfileStats {
    int countSSH = 0; // SS Plateada
    int countSS  = 0; // SS Dorada
    int countSH  = 0; // S Plateada
    int countS   = 0; // S Dorada
    int countA   = 0;
    int countB   = 0;
    int countC   = 0;
    int countD   = 0;

    int totalScores = 0;
    float globalAcc = 0.0f;
    float totalPP = 0.0f;
    int maxComboEver = 0;
    uint64_t totalScoreSum = 0;
    std::string rankTitle = "Rhythm Novice";

    std::vector<TopSongRecord> topSongs;
};

struct StreetPassRival {
    OsuStreetPassPacket packet;
    std::string receivedTime;
    bool isNew = false;
};

class ProfileManager {
public:
    MiiProfileInfo mii;
    PlayerProfileStats stats;
    std::vector<StreetPassRival> rivals;
    int selectedRivalIndex = -1; // -1 = Mi propio perfil, >= 0 = Rival StreetPass

    LoadedTexture avatarTex;
    std::string avatarPath = "";

    void init(const std::string& fallbackUsername);
    void loadAvatar();
    bool selectMiiWithApplet();
    void refreshStats(const ScoreManager& scoreMgr, const SongManager& songMgr);

    // Mii y colores temáticos
    u32 getThemeColor() const;
    static u32 getMiiColorRgba(u8 colorIdx);

    // StreetPass (CECD & SD Exchange)
    bool exportStreetPassOutbox();
    int scanStreetPassInbox();
    void clearRivals();

    // Exportación de Imagen (BMP / DCIM)
    bool exportCardImage(const std::string& customPath = "");

    // Código QR resumen de perfil
    std::string generateQrString() const;
};
