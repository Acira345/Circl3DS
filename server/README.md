# Circl3DS - Servidor Multijugador Online (Master & Relay)

Este servidor permite conectar partidas multijugador de **Circl3DS** a través de Internet sin requerir que los usuarios abran puertos en sus routers (NAT Bypass).

## ¿Cómo funciona?
1. **Lobby & Rooms:** Las consolas 3DS conectan a este servidor mediante UDP. Cuando un usuario crea una sala en modo **ONLINE**, el servidor la registra y la difunde a los demás jugadores.
2. **Gameplay en vivo:** El progreso de la partida (puntuación, combo, precisión, HP y fallos) se retransmite a través del relay con apenas 30 bytes por paquete.
3. **Transferencia de Beatmaps:** Si a un jugador le falta la canción, el servidor empareja por TCP a la consola anfitriona con la consola invitada para transmitir el `.osu` y el `.mp3`.

---

## Requisitos
- **Python 3.7 o superior** (No requiere instalar librerías con `pip`, usa exclusivamente la biblioteca estándar de Python).

---

## Ejecución Local o en VPS
Para iniciar el servidor, simplemente ejecuta:
```bash
python server.py
```
Por defecto escuchará en:
- **UDP 34567** (Lobby, señalización y gameplay)
- **TCP 34568** (Transferencia de mapas)

Si deseas especificar otro puerto base:
```bash
python server.py 34567
```

---

## Configuración en Circl3DS
En la consola 3DS o emulador:
1. En `sdmc:/3ds/circl3ds/settings.cfg` puedes configurar la IP/dominio de tu servidor:
   ```ini
   onlineServerHost=tudominio-o-ip.com
   onlineServerPort=34567
   ```
2. En el juego, entra en **Multijugador** y pulsa **SELECT** (o toca la pestaña superior) para cambiar entre **UDS (Inalámbrico local)**, **LAN (Wi-Fi Local)** y **ONLINE (Internet)**.
