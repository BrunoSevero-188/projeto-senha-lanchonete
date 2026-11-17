#!/usr/bin/env python3
"""Servidor web e API local do painel de senhas."""
from http.server import ThreadingHTTPServer, SimpleHTTPRequestHandler
from pathlib import Path
from urllib.parse import urlparse
import json
import socket

PASTA = Path(__file__).resolve().parent
PORTA = 8080
ESTADO = {"senha": "", "horario": "", "historico": [], "atualizado": False}
PROXIMA = 1

def ip_rede():
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
            s.connect(("8.8.8.8", 80))
            return s.getsockname()[0]
    except OSError:
        return "127.0.0.1"

class Handler(SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(PASTA), **kwargs)

    def log_message(self, formato, *args):
        print(f"[HTTP] {self.address_string()} - {formato % args}")

    def enviar_json(self, dados, status=200):
        corpo = json.dumps(dados, ensure_ascii=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(corpo)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(corpo)

    def do_OPTIONS(self):
        self.send_response(204)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.end_headers()

    def do_GET(self):
        caminho = urlparse(self.path).path
        if caminho == "/api/senha":
            self.enviar_json(ESTADO.copy())
            return
        super().do_GET()

    def do_POST(self):
        global PROXIMA
        caminho = urlparse(self.path).path
        tamanho = int(self.headers.get("Content-Length", "0"))
        try:
            dados = json.loads(self.rfile.read(tamanho) or b"{}")
        except (json.JSONDecodeError, ValueError):
            self.enviar_json({"erro": "JSON inválido"}, 400)
            return

        if caminho == "/api/chamar":
            senha = dados.get("senha")
            if senha is None:
                senha = PROXIMA
                PROXIMA = PROXIMA + 1 if PROXIMA < 9999 else 1
            try:
                numero = int(senha)
                if not 1 <= numero <= 9999:
                    raise ValueError
            except (TypeError, ValueError):
                self.enviar_json({"erro": "A senha deve ser um número entre 1 e 9999"}, 400)
                return
            if ESTADO["senha"]:
                ESTADO["historico"].insert(0, {
                    "senha": ESTADO["senha"], "horario": ESTADO["horario"]
                })
                ESTADO["historico"] = ESTADO["historico"][:5]
            from datetime import datetime
            ESTADO.update({
                "senha": f"{numero:04d}",
                "horario": datetime.now().strftime("%H:%M:%S"),
                "atualizado": True
            })
            self.enviar_json(ESTADO.copy())
            ESTADO["atualizado"] = False
            return

        if caminho == "/api/reset":
            ESTADO.update({"senha": "", "horario": "", "historico": [], "atualizado": False})
            PROXIMA = 1
            self.enviar_json(ESTADO.copy())
            return

        self.enviar_json({"erro": "Endpoint não encontrado"}, 404)

if __name__ == "__main__":
    servidor = ThreadingHTTPServer(("0.0.0.0", PORTA), Handler)
    print("=" * 54)
    print(" PAINEL DE SENHAS - SERVIDOR LOCAL")
    print(f" Neste computador: http://localhost:{PORTA}")
    print(f" Na rede Wi-Fi:    http://{ip_rede()}:{PORTA}")
    print(" Para encerrar: Ctrl+C")
    print("=" * 54)
    try:
        servidor.serve_forever()
    except KeyboardInterrupt:
        print("\nServidor encerrado.")
    finally:
        servidor.server_close()
