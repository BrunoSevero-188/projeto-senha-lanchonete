/* =========================================
   PAINEL DE SENHAS - LANCHONETE
   JavaScript Puro (Vanilla JS) - ES6+
   Interface Web para exibição de senhas
   ========================================= */

/* =========================================
   CONFIGURAÇÕES E CONSTANTES
   ========================================= */
const CONFIG = {
    LIMITE_HISTORICO: 5,
    LIMITE_DIGITOS: 4,
    MIN_SENHA: 1,
    MAX_SENHA: 9999,

    CHAVE_ATUAL: 'senhaAtual',
    CHAVE_HISTORICO: 'historicoSenhas',
    CHAVE_SOM: 'somAtivo',
    CHAVE_AUTO_UPDATE: 'autoUpdate',

    API_BASE: '',
    ENDPOINTS: {
        SENHA: '/api/senha',
        HISTORICO: '/api/historico',
        CHAMAR: '/api/chamar',
        RESET: '/api/reset',
        SET: '/api/set'
    },

    POLLING_INTERVAL: 2000,
    WS_URL: null
};

/* =========================================
   ELEMENTOS DO DOM - CACHE
   ========================================= */
const elementos = {
    senhaAtual: document.getElementById('senhaAtual'),
    listaHistorico: document.getElementById('listaHistorico'),
    mensagem: document.getElementById('mensagem'),
    contador: document.getElementById('contador'),
    btnTeste: document.getElementById('btnTeste'),
    btnLimparHistorico: document.getElementById('btnLimparHistorico'),
    somAtivo: document.getElementById('somAtivo'),
    autoUpdate: document.getElementById('autoUpdate'),
    statusIndicador: document.getElementById('statusIndicador'),
    statusTexto: document.getElementById('statusTexto'),
    relogio: document.getElementById('relogio'),
    versao: document.getElementById('versao')
};

/* =========================================
   ESTADO DA APLICAÇÃO
   ========================================= */
const estado = {
    senhaAtual: '',
    historico: [],
    somAtivo: true,
    autoUpdate: true,
    conectado: false,
    ws: null,
    pollingTimer: null,
    proximaSenhaTeste: 101,
    audioContext: null,
    ultimoHorario: ''
};

/* =========================================
   INICIALIZAÇÃO
   ========================================= */
document.addEventListener('DOMContentLoaded', async () => {
    console.log('[Painel Web] Iniciando...');
    carregarPreferencias();
    carregarEstadoLocal();
    iniciarRelogio();
    configurarEventListeners();
    await tentarConexaoESP32();
    atualizarInterface();
    console.log('[Painel Web] Pronto!');
});

/* =========================================
   PREFERÊNCIAS (localStorage)
   ========================================= */
function carregarPreferencias() {
    estado.somAtivo = localStorage.getItem(CONFIG.CHAVE_SOM) !== 'false';
    estado.autoUpdate = localStorage.getItem(CONFIG.CHAVE_AUTO_UPDATE) !== 'false';
    elementos.somAtivo.checked = estado.somAtivo;
    elementos.autoUpdate.checked = estado.autoUpdate;
}

function salvarPreferencia(chave, valor) {
    localStorage.setItem(chave, valor);
}

/* =========================================
   ESTADO LOCAL (Simulação)
   ========================================= */
function carregarEstadoLocal() {
    const salva = localStorage.getItem(CONFIG.CHAVE_ATUAL);
    if (salva) estado.senhaAtual = salva;

    try {
        const hist = localStorage.getItem(CONFIG.CHAVE_HISTORICO);
        if (hist) {
            estado.historico = JSON.parse(hist);
            if (estado.historico.length > CONFIG.LIMITE_HISTORICO) {
                estado.historico = estado.historico.slice(0, CONFIG.LIMITE_HISTORICO);
            }
        }
    } catch (e) {
        console.warn('[Painel Web] Erro ao ler histórico:', e);
        estado.historico = [];
    }
}

function salvarEstadoLocal() {
    localStorage.setItem(CONFIG.CHAVE_ATUAL, estado.senhaAtual);
    localStorage.setItem(CONFIG.CHAVE_HISTORICO, JSON.stringify(estado.historico));
}

/* =========================================
   RELÓGIO
   ========================================= */
function iniciarRelogio() {
    atualizarRelogio();
    setInterval(atualizarRelogio, 1000);
}

function atualizarRelogio() {
    const agora = new Date();
    const horario = agora.toLocaleTimeString('pt-BR', {
        hour: '2-digit', minute: '2-digit', second: '2-digit', hour12: false
    });
    elementos.relogio.textContent = horario;
}

function obterHorarioAtual() {
    return new Date().toLocaleTimeString('pt-BR', {
        hour: '2-digit', minute: '2-digit', second: '2-digit', hour12: false
    });
}

/* =========================================
   EVENT LISTENERS
   ========================================= */
function configurarEventListeners() {
    elementos.btnTeste.addEventListener('click', () => {
        const novaSenha = gerarSenhaTeste();
        chamarSenha(novaSenha, obterHorarioAtual());
    });

    elementos.btnLimparHistorico.addEventListener('click', () => {
        if (confirm('Tem certeza que deseja limpar todo o histórico?')) {
            limparHistorico();
        }
    });

    elementos.somAtivo.addEventListener('change', (e) => {
        estado.somAtivo = e.target.checked;
        salvarPreferencia(CONFIG.CHAVE_SOM, estado.somAtivo);
        mostrarMensagem(estado.somAtivo ? 'Som ativado' : 'Som desativado', 'info');
    });

    elementos.autoUpdate.addEventListener('change', (e) => {
        estado.autoUpdate = e.target.checked;
        salvarPreferencia(CONFIG.CHAVE_AUTO_UPDATE, estado.autoUpdate);
        if (estado.autoUpdate) iniciarPolling(); else pararPolling();
        mostrarMensagem(estado.autoUpdate ? 'Auto-atualização ativada' : 'Auto-atualização desativada', 'info');
    });

    document.addEventListener('keydown', (e) => {
        if ((e.code === 'Space' || e.code === 'Enter') && !e.target.matches('input, button')) {
            e.preventDefault(); elementos.btnTeste.click();
        }
        if (e.code === 'KeyL' && e.ctrlKey) { e.preventDefault(); elementos.btnLimparHistorico.click(); }
        if (e.code === 'KeyM' && e.ctrlKey) { e.preventDefault(); elementos.somAtivo.click(); }
    });

    let lastTouchEnd = 0;
    document.addEventListener('touchend', (e) => {
        const now = Date.now();
        if (now - lastTouchEnd <= 300) e.preventDefault();
        lastTouchEnd = now;
    }, { passive: false });
}

/* =========================================
   CONEXÃO COM ESP32
   ========================================= */
async function tentarConexaoESP32() {
    if (CONFIG.WS_URL) {
        conectarWebSocket(CONFIG.WS_URL);
    } else {
        definirStatusConexao(false, 'Modo local (simulação)');
        if (estado.autoUpdate) iniciarPolling();
    }
}

function definirStatusConexao(conectado, mensagem = '') {
    estado.conectado = conectado;
    const statusEl = elementos.statusIndicador.parentElement;
    if (conectado) {
        statusEl.classList.add('conectado');
        elementos.statusTexto.textContent = mensagem || 'Conectado ao ESP32';
        elementos.versao.textContent = 'ESP32 Online';
    } else {
        statusEl.classList.remove('conectado');
        elementos.statusTexto.textContent = mensagem || 'Desconectado';
        elementos.versao.textContent = 'Modo Local';
    }
}

/* =========================================
   WEBSOCKET (Tempo Real)
   ========================================= */
function conectarWebSocket(url) {
    if (estado.ws) estado.ws.close();

    try {
        estado.ws = new WebSocket(url);
        estado.ws.onopen = () => {
            console.log('[WS] Conectado ao ESP32');
            definirStatusConexao(true, 'WebSocket conectado');
            pararPolling();
        };
        estado.ws.onmessage = (evento) => {
            try { processarDadosESP32(JSON.parse(evento.data)); } catch (e) { console.error('[WS] Mensagem inválida:', e); }
        };
        estado.ws.onclose = () => {
            console.log('[WS] Desconectado');
            definirStatusConexao(false, 'WebSocket desconectado');
            if (estado.autoUpdate) setTimeout(() => conectarWebSocket(url), 5000);
        };
        estado.ws.onerror = (erro) => console.error('[WS] Erro:', erro);
    } catch (e) { console.error('[WS] Falha:', e); definirStatusConexao(false, 'Erro WebSocket'); }
}

function processarDadosESP32(dados) {
    if (dados.senha !== undefined) {
        if (dados.tipo === 'reset') { estado.senhaAtual = ''; estado.historico = []; }
        else { chamarSenha(dados.senha, dados.horario || obterHorarioAtual(), false); }
    }
    if (dados.historico) { estado.historico = dados.historico.slice(0, CONFIG.LIMITE_HISTORICO); }
    atualizarInterface(); salvarEstadoLocal();
}

/* =========================================
   POLLING HTTP (Fallback)
   ========================================= */
function iniciarPolling() { pararPolling(); if (!estado.autoUpdate) return; estado.pollingTimer = setInterval(() => consultarAPI(CONFIG.ENDPOINTS.SENHA), CONFIG.POLLING_INTERVAL); }
function pararPolling() { if (estado.pollingTimer) { clearInterval(estado.pollingTimer); estado.pollingTimer = null; } }

async function consultarAPI(endpoint) {
    if (!CONFIG.API_BASE) return;
    try {
        const resp = await fetch(`${CONFIG.API_BASE}${endpoint}`, { method: 'GET', cache: 'no-store', headers: { 'Accept': 'application/json' } });
        if (!resp.ok) throw new Error(`HTTP ${resp.status}`);
        processarDadosESP32(await resp.json());
        definirStatusConexao(true);
    } catch (e) { console.warn('[API] Erro:', e.message); definirStatusConexao(false, 'Erro de comunicação'); }
}

/* =========================================
   LÓGICA DE SENHAS
   ========================================= */
function gerarSenhaTeste() { let s = String(estado.proximaSenhaTeste++); if (estado.proximaSenhaTeste > CONFIG.MAX_SENHA) estado.proximaSenhaTeste = CONFIG.MIN_SENHA; return s.padStart(4, '0'); }

function validarSenha(senha) { if (!/^\d{1,4}$/.test(senha)) return false; const n = parseInt(senha, 10); return n >= CONFIG.MIN_SENHA && n <= CONFIG.MAX_SENHA; }

function chamarSenha(novaSenha, horario = obterHorarioAtual(), salvar = true) {
    novaSenha = String(novaSenha).padStart(4, '0');
    if (!validarSenha(novaSenha)) { mostrarMensagem('Senha inválida. Use 1-4 dígitos.', 'erro'); return false; }
    if (novaSenha === estado.senhaAtual) return false;

    if (estado.senhaAtual) {
        estado.historico.unshift({ senha: estado.senhaAtual, horario: estado.ultimoHorario || obterHorarioAtual() });
        estado.historico = estado.historico.slice(0, CONFIG.LIMITE_HISTORICO);
    }

    estado.senhaAtual = novaSenha;
    estado.ultimoHorario = horario;

    atualizarInterface();
    if (salvar) salvarEstadoLocal();

    elementos.senhaAtual.classList.remove('animar');
    void elementos.senhaAtual.offsetWidth;
    elementos.senhaAtual.classList.add('animar');

    mostrarMensagem(`Senha ${novaSenha} chamada!`, 'sucesso');
    reproduzirSomChamada();

    if (estado.conectado && estado.ws && estado.ws.readyState === WebSocket.OPEN) {
        estado.ws.send(JSON.stringify({ acao: 'senha_chamada', senha: novaSenha }));
    }
    return true;
}

function limparHistorico() { estado.historico = []; atualizarInterface(); salvarEstadoLocal(); mostrarMensagem('Histórico limpo', 'info'); }

/* =========================================
   ATUALIZAÇÃO DA INTERFACE
   ========================================= */
function atualizarInterface() { atualizarSenhaAtual(); atualizarHistorico(); atualizarContador(); }

function atualizarSenhaAtual() { elementos.senhaAtual.textContent = estado.senhaAtual || '---'; }

function atualizarHistorico() {
    const lista = elementos.listaHistorico;
    if (estado.historico.length === 0) { lista.innerHTML = '<li class="historico-vazio" aria-hidden="true">Nenhuma senha chamada ainda.</li>'; return; }
    lista.innerHTML = '';
    estado.historico.forEach((reg, i) => {
        const li = document.createElement('li');
        li.style.animationDelay = `${i * 60}ms`;
        li.innerHTML = `<span class="historico-senha">${reg.senha}</span><span class="historico-horario">${reg.horario}</span>`;
        lista.appendChild(li);
    });
}

function atualizarContador() { elementos.contador.textContent = `${estado.historico.length} / ${CONFIG.LIMITE_HISTORICO}`; }

/* =========================================
   SOM (Web Audio API)
   ========================================= */
function getAudioContext() {
    if (!estado.audioContext) {
        const AC = window.AudioContext || window.webkitAudioContext;
        if (!AC) { console.warn('[Som] Web Audio API não suportada'); return null; }
        estado.audioContext = new AC();
    }
    return estado.audioContext;
}

function reproduzirSomChamada() {
    if (!estado.somAtivo) return;
    const ctx = getAudioContext(); if (!ctx) return;
    if (ctx.state === 'suspended') ctx.resume();
    const now = ctx.currentTime;
    [880, 880, 880].forEach((freq, i) => {
        const osc = ctx.createOscillator(), gain = ctx.createGain();
        osc.type = 'sine'; osc.frequency.value = freq;
        const t = now + i * 0.18;
        gain.gain.setValueAtTime(0.0001, t);
        gain.gain.exponentialRampToValueAtTime(0.3, t + 0.02);
        gain.gain.exponentialRampToValueAtTime(0.0001, t + 0.15);
        osc.connect(gain); gain.connect(ctx.destination);
        osc.start(t); osc.stop(t + 0.17);
    });
}

function reproduzirSomErro() { if (!estado.somAtivo) return; const ctx = getAudioContext(); if (!ctx) return; if (ctx.state === 'suspended') ctx.resume(); const o=ctx.createOscillator(),g=ctx.createGain(); o.type='square';o.frequency.value=200;g.gain.value=0.1;o.connect(g);g.connect(ctx.destination);o.start();o.stop(ctx.currentTime+0.3); }
function reproduzirSomSucesso() { if (!estado.somAtivo) return; const ctx=getAudioContext();if(!ctx)return;if(ctx.state==='suspended')ctx.resume();[600,800,1000].forEach((f,i)=>{const o=ctx.createOscillator(),g=ctx.createGain();o.type='sine';o.frequency.value=f;const t=ctx.currentTime+i*0.08;g.gain.setValueAtTime(0.15,t);g.gain.exponentialRampToValueAtTime(0.001,t+0.15);o.connect(g);g.connect(ctx.destination);o.start(t);o.stop(t+0.15);}); }

/* =========================================
   MENSAGENS TEMPORÁRIAS
   ========================================= */
function mostrarMensagem(texto, tipo = 'info') {
    elementos.mensagem.textContent = texto;
    elementos.mensagem.className = `mensagem tipo-${tipo}`;
    if (tipo !== 'padrao') {
        setTimeout(() => {
            if (elementos.mensagem.textContent === texto) {
                elementos.mensagem.textContent = estado.senhaAtual ? `Última chamada: ${estado.senhaAtual} às ${estado.ultimoHorario}` : 'Aguardando próxima chamada...';
                elementos.mensagem.className = 'mensagem';
            }
        }, 4000);
    }
}

/* =========================================
   INTEGRAÇÃO FUTURA COM ESP32
   ========================================= */
async function enviarComandoESP32(endpoint, dados = {}) {
    if (!CONFIG.API_BASE) return { erro: 'Modo local' };
    try { const r = await fetch(`${CONFIG.API_BASE}${endpoint}`, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(dados) }); return await r.json(); }
    catch (e) { console.error('[ESP32] Erro:', e); return { erro: e.message }; }
}

async function chamarProximaViaESP32() { return enviarComandoESP32(CONFIG.ENDPOINTS.CHAMAR); }
async function resetarViaESP32() { return enviarComandoESP32(CONFIG.ENDPOINTS.RESET); }
async function definirSenhaViaESP32(senha) { return enviarComandoESP32(CONFIG.ENDPOINTS.SET, { senha }); }

function configurarESP32(ip) { CONFIG.API_BASE = `http://${ip}`; CONFIG.WS_URL = `ws://${ip}:81`; if (estado.ws) estado.ws.close(); conectarWebSocket(CONFIG.WS_URL); consultarAPI(CONFIG.ENDPOINTS.SENHA); }

window.PainelSenhas = { chamar: () => chamarSenha(gerarSenhaTeste()), chamarProxima: chamarProximaViaESP32, resetar: resetarViaESP32, definirSenha: definirSenhaViaESP32, configurarESP32, estado: () => ({ ...estado }), config: CONFIG };

console.log('[Painel Web] Carregado. Use PainelSenhas no console para debug.');