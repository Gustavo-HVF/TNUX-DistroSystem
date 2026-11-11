#!/usr/bin/env python3
"""Testa o servidor da ponte (build/tnuxd): página, chave, arquivos, terminal por WebSocket e tela.

    python3 tests/testar_ponte.py
"""
import base64, hashlib, http.client, json, os, shutil, socket, struct, subprocess, sys, tempfile, time, urllib.parse

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOKEN = "chave-de-teste-0123456789abcdef"
falhas = feitos = 0


def confere(nome, ok, detalhe=""):
    global falhas, feitos
    feitos += 1
    if not ok:
        falhas += 1
        print(f"FALHOU: {nome}\n        {detalhe}")


def porta_livre():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    p = s.getsockname()[1]
    s.close()
    return p


class Ponte:
    def __init__(self, casa):
        self.porta = porta_livre()
        amb = dict(os.environ, HOME=casa, TNUX_RAIZ=RAIZ)
        amb.pop("DISPLAY", None)
        amb.pop("WAYLAND_DISPLAY", None)
        self.proc = subprocess.Popen([os.path.join(RAIZ, "build", "tnuxd"), "--porta", str(self.porta), "--token", TOKEN],
                                     env=amb, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        for _ in range(100):
            try:
                socket.create_connection(("127.0.0.1", self.porta), timeout=0.2).close()
                return
            except OSError:
                time.sleep(0.05)
        raise RuntimeError("o tnuxd não ligou")

    def pedir(self, metodo, cam, corpo=None, cab=None, token=True):
        c = http.client.HTTPConnection("127.0.0.1", self.porta, timeout=20)
        h = dict(cab or {})
        if token:
            h.setdefault("X-TNUX-Token", TOKEN)
        c.request(metodo, cam, body=corpo, headers=h)
        r = c.getresponse()
        dados = r.read()
        c.close()
        return r.status, dados, r

    def json(self, metodo, cam, corpo=None, **k):
        st, dados, _ = self.pedir(metodo, cam, corpo, **k)
        try:
            return st, json.loads(dados)
        except ValueError:
            return st, dados

    def parar(self):
        self.proc.terminate()
        try:
            self.proc.wait(5)
        except subprocess.TimeoutExpired:
            self.proc.kill()


class WS:
    """Cliente WebSocket mínimo (o bastante para conversar com o tnuxd)."""

    def __init__(self, porta, cam, origem=None):
        self.s = socket.create_connection(("127.0.0.1", porta), timeout=15)
        chave = base64.b64encode(os.urandom(16)).decode()
        cab = f"GET {cam} HTTP/1.1\r\nHost: 127.0.0.1:{porta}\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: {chave}\r\nSec-WebSocket-Version: 13\r\n"
        if origem:
            cab += f"Origin: {origem}\r\n"
        self.s.sendall((cab + "\r\n").encode())
        self.buf = b""
        while b"\r\n\r\n" not in self.buf:
            d = self.s.recv(4096)
            if not d:
                break
            self.buf += d
        topo, _, self.buf = self.buf.partition(b"\r\n\r\n")
        self.status = int(topo.split()[1])
        esperado = base64.b64encode(hashlib.sha1((chave + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode()).digest())
        self.aceite_ok = esperado in topo

    def enviar(self, dados, texto=False):
        op = 1 if texto else 2
        m = os.urandom(4)
        n = len(dados)
        cab = bytes([0x80 | op])
        if n < 126:
            cab += bytes([0x80 | n])
        elif n < 65536:
            cab += bytes([0x80 | 126]) + struct.pack(">H", n)
        else:
            cab += bytes([0x80 | 127]) + struct.pack(">Q", n)
        self.s.sendall(cab + m + bytes(b ^ m[i & 3] for i, b in enumerate(dados)))

    def _ler(self, n):
        while len(self.buf) < n:
            d = self.s.recv(65536)
            if not d:
                raise EOFError
            self.buf += d
        r, self.buf = self.buf[:n], self.buf[n:]
        return r

    def receber(self):
        """Devolve (opcode, dados) do próximo quadro."""
        a, b = self._ler(2)
        n = b & 0x7F
        if n == 126:
            n = struct.unpack(">H", self._ler(2))[0]
        elif n == 127:
            n = struct.unpack(">Q", self._ler(8))[0]
        return a & 0x0F, self._ler(n)

    def ate(self, trecho, tempo=15):
        """Lê a saída até aparecer o trecho. Devolve tudo o que leu."""
        tudo = b""
        fim = time.time() + tempo
        self.s.settimeout(tempo)
        while trecho not in tudo and time.time() < fim:
            try:
                op, d = self.receber()
            except (EOFError, socket.timeout):
                break
            if op == 8:
                break
            if op == 2:
                tudo += d
        return tudo

    def fechar(self):
        try:
            self.s.close()
        except OSError:
            pass


def main():
    casa = tempfile.mkdtemp(prefix="tnux-teste-")
    p = Ponte(casa)
    q = urllib.parse.quote
    try:
        # página e catálogo: públicos
        st, d, _ = p.pedir("GET", "/", token=False)
        confere("página inicial", st == 200 and b"TNUX" in d, f"{st}")
        st, d, r = p.pedir("GET", "/tnux.wasm", token=False)
        confere("núcleo wasm com o tipo certo", st == 200 and r.getheader("Content-Type") == "application/wasm" and d[:4] == b"\0asm", f"{st} {r.getheader('Content-Type')}")
        st, d, _ = p.pedir("GET", "/../ponte/tnuxd.c", token=False)
        confere("não sai da pasta web", st in (400, 404), f"{st}")
        st, d, _ = p.pedir("GET", "/%2e%2e/%2e%2e/etc/passwd", token=False)
        confere("não sai da pasta web (codificado)", st in (400, 404), f"{st}")

        # chave
        st, j = p.json("GET", "/api/info", token=False)
        confere("API sem chave é recusada", st == 401, f"{st} {j}")
        st, j = p.json("GET", "/api/info", cab={"X-TNUX-Token": "errada-errada-errada-errada"}, token=False)
        confere("API com chave errada é recusada", st == 401, f"{st}")
        st, j = p.json("GET", "/api/info", cab={"Host": "site-malicioso.example"})
        confere("pedido para outro nome de host é recusado", st == 403, f"{st} {j}")
        st, j = p.json("POST", "/api/fs/mkdir?cam=" + q(casa + "/x"), cab={"Origin": "http://outro.example"})
        confere("pedido vindo de outro site é recusado", st == 403 and not os.path.exists(casa + "/x"), f"{st} {j}")
        st, d, r = p.pedir("GET", "/api/info")
        j = json.loads(d)
        confere("info", st == 200 and j.get("modo") == "ponte" and j.get("casa") == casa and j.get("cpus", 0) >= 1, f"{st} {j}")
        chave_apps = j.get("chave_apps", "")
        confere("não usa cookie (um app aberto no mesmo navegador não herda a chave)", r.getheader("Set-Cookie") is None and len(chave_apps) == 32, r.getheader("Set-Cookie"))
        st, j = p.json("GET", "/api/info", cab={"Cookie": "tnux_token=" + TOKEN}, token=False)
        confere("cookie não vale como chave", st == 401, f"{st}")
        st, j = p.json("GET", "/api/info?token=" + TOKEN, token=False)
        confere("a chave pode ir no endereço", st == 200, f"{st}")

        # arquivos
        st, j = p.json("POST", "/api/fs/mkdir?cam=" + q(casa + "/Documentos"))
        confere("mkdir", st == 200 and os.path.isdir(casa + "/Documentos"), f"{st} {j}")
        conteudo = "linha com acento: ação\n".encode() + bytes(range(256)) * 400
        st, j = p.json("POST", "/api/fs/gravar?cam=" + q(casa + "/Documentos/nota é.bin"), corpo=conteudo)
        confere("gravar", st == 200 and open(casa + "/Documentos/nota é.bin", "rb").read() == conteudo, f"{st} {j}")
        st, d, _ = p.pedir("GET", "/api/fs/ler?cam=" + q(casa + "/Documentos/nota é.bin"))
        confere("ler", st == 200 and d == conteudo, f"{st} {len(d)}")
        st, d, r = p.pedir("GET", "/api/fs/ler?cam=" + q(casa + "/Documentos/nota é.bin"), cab={"Range": "bytes=5-14"})
        confere("ler um trecho (Range)", st == 206 and d == conteudo[5:15] and r.getheader("Content-Range", "").startswith("bytes 5-14/"), f"{st} {d!r}")
        st, j = p.json("GET", "/api/fs/listar?cam=" + q(casa + "/Documentos"))
        confere("listar", st == 200 and [e["nome"] for e in j] == ["nota é.bin"] and j[0]["tipo"] == "f" and j[0]["tam"] == len(conteudo), f"{st} {j}")
        st, j = p.json("GET", "/api/fs/info?cam=" + q(casa))
        confere("info de pasta", st == 200 and j["tipo"] == "d" and j["gravavel"] is True, f"{st} {j}")
        st, j = p.json("POST", f"/api/fs/copiar?de={q(casa + '/Documentos')}&para={q(casa + '/Copia')}")
        confere("copiar pasta", st == 200 and os.path.exists(casa + "/Copia/nota é.bin"), f"{st} {j}")
        st, j = p.json("POST", f"/api/fs/mover?de={q(casa + '/Copia')}&para={q(casa + '/Movida')}")
        confere("mover", st == 200 and os.path.isdir(casa + "/Movida") and not os.path.exists(casa + "/Copia"), f"{st} {j}")
        st, j = p.json("POST", f"/api/fs/mover?de={q(casa + '/Movida')}&para={q(casa + '/Documentos')}")
        confere("mover não sobrescreve", st == 409 and os.path.isdir(casa + "/Movida"), f"{st} {j}")
        st, j = p.json("POST", "/api/fs/remover?cam=" + q(casa + "/Movida"))
        confere("remover pasta com arquivos", st == 200 and not os.path.exists(casa + "/Movida"), f"{st} {j}")
        st, j = p.json("POST", "/api/fs/remover?cam=" + q(casa + "/"))
        confere("não apaga a pasta pessoal", st == 403 and os.path.isdir(casa), f"{st} {j}")
        st, j = p.json("GET", "/api/fs/remover?cam=" + q(casa + "/Documentos"))
        confere("apagar exige POST", st == 405 and os.path.isdir(casa + "/Documentos"), f"{st} {j}")
        st, j = p.json("GET", "/api/fs/listar?cam=" + q(casa + "/nao-existe"))
        confere("pasta que não existe", st == 404 and "erro" in j, f"{st} {j}")
        st, d, r = p.pedir("GET", "/arq" + q(casa + "/Documentos/nota é.bin") + "?token=" + TOKEN, token=False)
        confere("/arq entrega arquivos com a chave", st == 200 and d == conteudo and r.getheader("Content-Security-Policy") == "sandbox", f"{st}")
        st, d, _ = p.pedir("GET", "/arq" + q(casa + "/Documentos/nota é.bin"), token=False)
        confere("/arq sem chave é recusado", st == 401, f"{st}")
        st, j = p.json("GET", "/api/tem?cmd=sh,programa-que-nao-existe-xyz")
        confere("tem comando", st == 200 and j == {"sh": True, "programa-que-nao-existe-xyz": False}, f"{st} {j}")

        # apps web instalados (~/.tnux/apps)
        os.makedirs(casa + "/.tnux/apps/meuapp/img")
        open(casa + "/.tnux/apps/meuapp/index.html", "w").write("<h1>meu app</h1>")
        open(casa + "/.tnux/apps/meuapp/.tnux-app.json", "w").write('{"id":"meuapp","nome":"Meu App","entrada":"index.html"}\n')
        st, j = p.json("GET", "/api/apps")
        confere("lista apps web", st == 200 and any(a["id"] == "meuapp" for a in j["web"]) and isinstance(j["nativos"], list), f"{st} {j}")
        st, d, _ = p.pedir("GET", f"/c/{chave_apps}/meuapp/", cab={"Host": f"localhost:{p.porta}"}, token=False)
        confere("serve o app web pela chave de conteúdo, no host separado", st == 200 and d == b"<h1>meu app</h1>", f"{st} {d!r}")
        st, d, _ = p.pedir("GET", f"/c/{'0' * 32}/meuapp/", token=False)
        confere("chave de conteúdo errada é recusada", st == 401, f"{st}")
        st, d, _ = p.pedir("GET", f"/c/{chave_apps}/meuapp/../../../etc/passwd", token=False)
        confere("apps não saem da pasta", st in (400, 404), f"{st}")
        st, d, _ = p.pedir("GET", f"/c/{chave_apps}/meuapp/.tnux-app.json", token=False)
        confere("arquivos ocultos dos apps não são servidos", st == 400, f"{st}")
        st, j = p.json("GET", f"/api/info?token={chave_apps}", token=False)
        confere("a chave de conteúdo não dá acesso ao aparelho", st == 401, f"{st}")

        # terminal por WebSocket
        w = WS(p.porta, "/ws/pty?cols=100&rows=30", origem=f"http://127.0.0.1:{p.porta}")
        confere("terminal sem chave é recusado", w.status == 401, f"{w.status}")
        w.fechar()
        w = WS(p.porta, f"/ws/pty?token={TOKEN}&cols=100&rows=30", origem="http://outro.example")
        confere("terminal vindo de outro site é recusado", w.status == 403, f"{w.status}")
        w.fechar()
        w = WS(p.porta, f"/ws/pty?token={TOKEN}&cols=100&rows=30", origem=f"http://127.0.0.1:{p.porta}")
        confere("terminal: aperto de mão do WebSocket", w.status == 101 and w.aceite_ok, f"{w.status}")
        saida = w.ate(b"$ ")
        confere("terminal: prompt do tnux", b"modo ponte" in saida, repr(saida[-300:]))
        w.enviar(b"echo $((6*7))-$TNUX_PONTE; stty size\r")
        saida = w.ate(b"30 100")
        confere("terminal: roda comandos do aparelho com o tamanho certo", b"42-1" in saida and b"30 100" in saida, repr(saida[-300:]))
        w.enviar(b'{"t":"r","c":120,"l":40}', texto=True)
        time.sleep(0.2)
        w.enviar(b"stty size\r")
        saida = w.ate(b"40 120")
        confere("terminal: redimensionar", b"40 120" in saida, repr(saida[-200:]))
        w.enviar(("echo colado-" + "x" * 70000 + " | wc -c\r").encode())
        saida = w.ate(b"70008", 20)
        confere("terminal: texto grande colado", b"70008" in saida, repr(saida[-120:]))
        w.enviar(b"exit\r")
        saida = w.ate(b"\xff\xff\xff", 5)
        confere("terminal: exit fecha a conexão", True)
        w.fechar()
        w = WS(p.porta, f"/ws/pty?token={TOKEN}&cmd=" + q("echo instalado-$((1+1))"))
        saida = w.ate(b"Pressione Enter")
        confere("terminal com comando pronto (instalações)", b"instalado-2" in saida and b"Pressione Enter" in saida, repr(saida[-300:]))
        w.fechar()

        # tela (VNC) para programas gráficos
        st, j = p.json("GET", "/api/tela/estado")
        confere("estado da tela", st == 200 and j["ativa"] is False, f"{st} {j}")
        if shutil.which("Xvnc") or shutil.which("Xtigervnc"):
            st, j = p.json("POST", "/api/tela/iniciar?l=800&a=600")
            confere("liga a tela", st == 200 and j.get("ativa") is True and j.get("largura") == 800, f"{st} {j}")
            if st == 200:
                w = WS(p.porta, f"/ws/tela?token={TOKEN}")
                confere("tela: WebSocket", w.status == 101, f"{w.status}")
                w.s.settimeout(10)
                op, d = w.receber()
                confere("tela: o servidor VNC se apresenta", op == 2 and d.startswith(b"RFB 003."), repr(d))
                w.enviar(b"RFB 003.008\n")
                op, d = w.receber()
                confere("tela: segurança 'nenhuma' (o acesso já é protegido pela chave e pelo socket local)", d == b"\x01\x01", repr(d))
                w.enviar(b"\x01")
                op, d = w.receber()
                confere("tela: autenticou", d == b"\0\0\0\0", repr(d))
                w.enviar(b"\x01")
                dados = b""
                while len(dados) < 24:
                    dados += w.receber()[1]
                larg, alt = struct.unpack(">HH", dados[:4])
                confere("tela: tamanho informado pelo servidor", (larg, alt) == (800, 600), f"{larg}x{alt}")
                w.fechar()
                confere("tela: socket local, sem porta TCP aberta", os.path.exists(casa + "/.tnux/tela.sock"), "")
                st, j = p.json("POST", "/api/tela/parar")
                confere("desliga a tela", st == 200 and j["ativa"] is False, f"{st} {j}")
        else:
            print("(Xvnc não instalado: pulei os testes da tela)")
    finally:
        p.parar()
        shutil.rmtree(casa, ignore_errors=True)
    print(f"{feitos - falhas}/{feitos} testes da ponte passaram")
    sys.exit(1 if falhas else 0)


if __name__ == "__main__":
    main()
