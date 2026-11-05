#!/usr/bin/env python3
"""
Circl3DS Online Master & Relay Server
Servidor Maestro y Relay ligero para multijugador en línea de Circl3DS.
Totalmente autónomo con Python 3 estándar (sin dependencias externas).

Puertos por defecto:
- UDP 34567: Señalización de salas, estado de lobby y gameplay en vivo.
- TCP 34568: Relay de descarga de Beatmaps entre consolas conectadas a través de Internet.
"""

import sys
import socket
import select
import struct
import time
import threading

MULTI_UDP_PORT = 34567
MULTI_TCP_PORT = 34568
MULTI_MAGIC_UDP = 0x43334453  # "C3DS"
MULTI_MAGIC_TCP = 0x43334454  # "C3DT"

# Packet Types
PKT_DISCOVER_PING  = 1
PKT_LOBBY_STATE    = 2
PKT_PLAYER_UPDATE  = 3
PKT_START_MATCH    = 4
PKT_LIVE_GAMEPLAY  = 5
PKT_QUEUE_ADD      = 6
PKT_MATCH_FINISH   = 7
PKT_FILE_REQ       = 8
PKT_FILE_HEADER    = 9
PKT_FILE_CHUNK     = 10
PKT_FILE_ACK       = 11
PKT_ROOM_CLOSED    = 12
PKT_PLAYER_LEAVE   = 13
PKT_RETURN_LOBBY   = 14
PKT_MATCH_LOADED   = 15
PKT_MATCH_GO       = 16

class Room:
    def __init__(self, room_id, host_addr):
        self.room_id = room_id
        self.host_addr = host_addr
        self.last_seen = time.time()
        self.last_lobby_state = None
        self.clients = set() # conjunto de (ip, port)
        self.room_title = "Online Room"
        self.host_name = "Host"
        self.player_count = 1

class Circl3dsRelayServer:
    def __init__(self, host="0.0.0.0", udp_port=MULTI_UDP_PORT, tcp_port=MULTI_TCP_PORT):
        self.host = host
        self.udp_port = udp_port
        self.tcp_port = tcp_port
        self.rooms = {}        # host_addr -> Room
        self.client_to_room = {} # client_addr -> Room
        self.running = True
        self.lock = threading.Lock()

    def run_udp(self):
        udp_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        udp_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        udp_sock.bind((self.host, self.udp_port))
        print(f"[UDP] Servidor de señalización y relay activo en {self.host}:{self.udp_port}")

        while self.running:
            try:
                data, addr = udp_sock.recvfrom(2048)
                if len(data) < 8:
                    continue

                magic, pkt_type, sender_slot, seq = struct.unpack_from("<IBBH", data, 0)
                if magic != MULTI_MAGIC_UDP:
                    continue

                now = time.time()

                with self.lock:
                    # 1. Petición del buscador de salas (Browser Discover Ping)
                    if pkt_type == PKT_DISCOVER_PING:
                        # Si es query del navegador de salas (senderSlot == 0xFF)
                        if sender_slot == 0xFF:
                            # Enviar a este cliente el estado de todas las salas activas registradas
                            active_rooms = []
                            for h_addr, r in list(self.rooms.items()):
                                if now - r.last_seen < 6.0 and r.last_lobby_state:
                                    active_rooms.append(r)
                                else:
                                    # Limpiar sala expirada
                                    if now - r.last_seen >= 6.0:
                                        self._cleanup_room(h_addr)

                            for r in active_rooms:
                                udp_sock.sendto(r.last_lobby_state, addr)
                            continue

                        # Si es un ping de un cliente que intenta unirse al host
                        # o registrarse en una sala existente:
                        room = self.client_to_room.get(addr)
                        if not room:
                            # Ver si existe una sala cuyo host sea el destinatario o vincularlo
                            pass

                    # 2. El Host envía estado del Lobby (LOBBY_STATE)
                    elif pkt_type == PKT_LOBBY_STATE:
                        room = self.rooms.get(addr)
                        if not room:
                            # Registrar nueva sala con este Host
                            room = Room(len(self.rooms) + 1, addr)
                            self.rooms[addr] = room
                            print(f"[LOBBY] Nueva sala creada por {addr[0]}:{addr[1]}")

                        room.last_seen = now
                        room.last_lobby_state = data

                        # Reenviar estado a todos los clientes unidos a esta sala
                        for client_addr in list(room.clients):
                            if client_addr != addr:
                                try:
                                    udp_sock.sendto(data, client_addr)
                                except Exception:
                                    pass

                    # 3. Paquete proveniente de un Cliente hacia el Host
                    elif pkt_type in (PKT_PLAYER_UPDATE, PKT_QUEUE_ADD, PKT_MATCH_LOADED, PKT_PLAYER_LEAVE):
                        room = self.client_to_room.get(addr)
                        if not room:
                            # Intentar asociar cliente con la sala activa más reciente o la primera disponible
                            if self.rooms:
                                # Si solo hay una sala o busca asociar
                                room = next(iter(self.rooms.values()))
                                self.client_to_room[addr] = room
                                room.clients.add(addr)
                                print(f"[CLIENT] Jugador {addr[0]}:{addr[1]} unido a sala de {room.host_addr[0]}")

                        if room:
                            room.clients.add(addr)
                            if pkt_type == PKT_PLAYER_LEAVE:
                                room.clients.discard(addr)
                                self.client_to_room.pop(addr, None)
                                print(f"[CLIENT] Jugador {addr[0]}:{addr[1]} salió de la sala")

                            # Reenviar al Host de la sala
                            try:
                                udp_sock.sendto(data, room.host_addr)
                            except Exception:
                                pass

                    # 4. Paquete de inicio / control / cierre emitido por el Host
                    elif pkt_type in (PKT_START_MATCH, PKT_MATCH_GO, PKT_ROOM_CLOSED, PKT_RETURN_LOBBY):
                        room = self.rooms.get(addr)
                        if room:
                            room.last_seen = now
                            # Reenviar a todos los clientes de la sala
                            for client_addr in list(room.clients):
                                try:
                                    udp_sock.sendto(data, client_addr)
                                except Exception:
                                    pass

                            if pkt_type == PKT_ROOM_CLOSED:
                                print(f"[LOBBY] Sala cerrada por el Host {addr[0]}:{addr[1]}")
                                self._cleanup_room(addr)

                    # 5. Sincronización en vivo de Gameplay (LIVE_GAMEPLAY)
                    elif pkt_type == PKT_LIVE_GAMEPLAY:
                        # Si lo envía el Host, reenviar a todos los clientes
                        if addr in self.rooms:
                            room = self.rooms[addr]
                            room.last_seen = now
                            for client_addr in list(room.clients):
                                try:
                                    udp_sock.sendto(data, client_addr)
                                except Exception:
                                    pass
                        else:
                            # Si lo envía un cliente, reenviar al Host y al resto de participantes
                            room = self.client_to_room.get(addr)
                            if room:
                                try:
                                    udp_sock.sendto(data, room.host_addr)
                                except Exception:
                                    pass
                                for other_client in list(room.clients):
                                    if other_client != addr:
                                        try:
                                            udp_sock.sendto(data, other_client)
                                        except Exception:
                                            pass

                    # Limpieza periódica de salas zombies
                    for h_addr, r in list(self.rooms.items()):
                        if now - r.last_seen > 10.0:
                            self._cleanup_room(h_addr)

            except Exception as e:
                if self.running:
                    print(f"[UDP Error] {e}")

        udp_sock.close()

    def _cleanup_room(self, host_addr):
        if host_addr in self.rooms:
            room = self.rooms[host_addr]
            for c in list(room.clients):
                self.client_to_room.pop(c, None)
            del self.rooms[host_addr]
            print(f"[CLEANUP] Sala de {host_addr[0]}:{host_addr[1]} eliminada")

    def run_tcp(self):
        """
        Relay TCP para descargas de canciones entre consolas a través del servidor.
        Permite que cuando una 3DS actúe de Host sirviendo un archivo y otra descargándolo,
        ambas se conecten al servidor y éste canalice los bytes.
        """
        tcp_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        tcp_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        tcp_sock.bind((self.host, self.tcp_port))
        tcp_sock.listen(8)
        tcp_sock.setblocking(False)
        print(f"[TCP] Relay de canciones activo en {self.host}:{self.tcp_port}")

        active_transfers = [] # lista de [sock1, sock2] emparejados
        pending_host = None

        while self.running:
            try:
                rlist = [tcp_sock]
                for pair in active_transfers:
                    rlist.extend(pair)
                if pending_host:
                    rlist.append(pending_host)

                readable, _, exceptional = select.select(rlist, [], rlist, 0.5)

                for s in readable:
                    if s is tcp_sock:
                        client_s, c_addr = tcp_sock.accept()
                        client_s.setblocking(False)
                        print(f"[TCP Connect] Conexión entrante desde {c_addr}")
                        if pending_host is None:
                            pending_host = client_s
                        else:
                            # Emparejar servidor (Host) con cliente descargador
                            active_transfers.append([pending_host, client_s])
                            print("[TCP Relay] Emparejada transferencia de beatmap entre dos consolas.")
                            pending_host = None
                    else:
                        # Buscar a qué par pertenece este socket y transferir datos al otro
                        for pair in list(active_transfers):
                            if s in pair:
                                other = pair[1] if s is pair[0] else pair[0]
                                try:
                                    chunk = s.recv(16384)
                                    if not chunk:
                                        # Conexión cerrada
                                        pair[0].close()
                                        pair[1].close()
                                        active_transfers.remove(pair)
                                        print("[TCP Relay] Transferencia finalizada.")
                                        break
                                    other.sendall(chunk)
                                except Exception:
                                    pair[0].close()
                                    pair[1].close()
                                    if pair in active_transfers:
                                        active_transfers.remove(pair)
                                    break

                for s in exceptional:
                    if s is pending_host:
                        pending_host.close()
                        pending_host = None
                    for pair in list(active_transfers):
                        if s in pair:
                            pair[0].close()
                            pair[1].close()
                            active_transfers.remove(pair)
                            break

            except Exception as e:
                if self.running:
                    print(f"[TCP Error] {e}")

        tcp_sock.close()

    def start(self):
        t_udp = threading.Thread(target=self.run_udp, daemon=True)
        t_tcp = threading.Thread(target=self.run_tcp, daemon=True)
        t_udp.start()
        t_tcp.start()
        print("="*65)
        print(" Circl3DS Master & Relay Server está en funcionamiento.")
        print(f" - Esperando paquetes en puerto UDP {self.udp_port}")
        print(f" - Esperando transferencias en puerto TCP {self.tcp_port}")
        print(" Presiona Ctrl + C para detener.")
        print("="*65)
        try:
            while True:
                time.sleep(1)
        except KeyboardInterrupt:
            print("\nDeteniendo servidor...")
            self.running = False
            t_udp.join(timeout=1.0)
            t_tcp.join(timeout=1.0)
            print("Servidor detenido correctamente.")

if __name__ == "__main__":
    port = MULTI_UDP_PORT
    if len(sys.argv) > 1:
        try:
            port = int(sys.argv[1])
        except ValueError:
            pass
    server = Circl3dsRelayServer(udp_port=port, tcp_port=port+1)
    server.start()
