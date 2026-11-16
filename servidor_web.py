#!/usr/bin/env python3
"""
Servidor HTTP para o Painel de Senhas (versão standalone)
Mostra localhost e IP da rede para acesso de outros dispositivos na mesma WiFi.
"""
import http.server
import socketserver
import socket
import os
import sys

PORT = 8080
DIRECTORY = os.path.dirname(os.path.abspath(__file__))

class Handler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=DIRECTORY, **kwargs)

    def log_message(self, format, *args):
        # Log mais limpo
        sys.stderr.write(f"[{self.address_string()}] {format % args}\n")

def get_lan_ip():
    """Obtém o IP local da máquina na rede."""
    try:
        # Cria socket UDP para descobrir IP de saída
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except Exception:
        return "127.0.0.1"

def main():
    os.chdir(DIRECTORY)

    # Verifica se os arquivos existem (renomeados)
    required = ['index.html', 'script.js', 'style.css']
    missing = [f for f in required if not os.path.exists(f)]
    if missing:
        print("⚠️  Arquivos não encontrados. Renomeie primeiro:")
        print("   ren index.txt index.html")
        print("   ren script.txt script.js")
        print("   ren style.txt style.css")
        return

    lan_ip = get_lan_ip()

    with socketserver.TCPServer(("", PORT), Handler) as httpd:
        print("=" * 60)
        print("🌐  PAINEL DE SENHAS - Servidor Web Standalone")
        print("=" * 60)
        print(f"📁 Diretório: {DIRECTORY}")
        print(f"🔌 Porta: {PORT}")
        print("-" * 60)
        print(f"💻  Acesso LOCAL:     http://localhost:{PORT}/")
        print(f"📱  Acesso REDE (LAN): http://{lan_ip}:{PORT}/")
        print("-" * 60)
        print("💡 Use o IP da LAN para acessar de outro dispositivo")
        print("   conectado na MESMA WiFi do ESP32.")
        print("=" * 60)
        print("Pressione Ctrl+C para parar.\n")

        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print("\n🛑 Servidor encerrado.")

if __name__ == "__main__":
    main()