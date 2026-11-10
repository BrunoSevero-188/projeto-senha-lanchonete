
/* =========================================
   SISTEMA DE SENHAS - LANCHONETE
   JavaScript puro
========================================= */

// Elementos HTML
const elementoSenhaAtual = document.getElementById("senhaAtual");
const elementoHistorico = document.getElementById("listaHistorico");
const elementoMensagem = document.getElementById("mensagem");
const elementoContador = document.getElementById("contador");
const elementoBotao = document.getElementById("btnChamar");
const elementoSom = document.getElementById("somAtivo");
const elementoRelogio = document.getElementById("relogio");

// Configurações
const LIMITE_HISTORICO = 5;
const LIMITE_DIGITOS = 4;

const CHAVE_ATUAL = "senhaAtual";
const CHAVE_HISTORICO = "historicoSenhas";

// Estado inicial
let senhaAtual = localStorage.getItem(CHAVE_ATUAL) || "";

let historico = JSON.parse(
    localStorage.getItem(CHAVE_HISTORICO) || "[]"
);

// Próxima senha para a simulação
let proximaSenha = 101;

// Contexto de áudio
let contextoAudio = null;


/* =========================================
   FORMATAÇÃO DE HORÁRIO
========================================= */

function obterHorario() {

    const agora = new Date();

    return agora.toLocaleTimeString("pt-BR", {
        hour: "2-digit",
        minute: "2-digit",
        second: "2-digit"
    });

}


/* =========================================
   SALVAR DADOS NO NAVEGADOR
========================================= */

function salvarDados() {

    localStorage.setItem(CHAVE_ATUAL, senhaAtual);

    localStorage.setItem(
        CHAVE_HISTORICO,
        JSON.stringify(historico)
    );

}


/* =========================================
   ATUALIZAR A SENHA ATUAL
========================================= */

function atualizarSenha() {

    elementoSenhaAtual.textContent = senhaAtual || "---";

    elementoContador.textContent =
        `${historico.length} / ${LIMITE_HISTORICO}`;

}


/* =========================================
   ATUALIZAR HISTÓRICO
========================================= */

function atualizarHistorico() {

    // Limpa o conteúdo anterior
    elementoHistorico.innerHTML = "";

    if (historico.length === 0) {

        const item = document.createElement("li");

        item.className = "historico-vazio";
        item.textContent = "Nenhuma senha chamada ainda.";

        elementoHistorico.appendChild(item);

        return;

    }

    // Exibe as senhas da mais recente para a mais antiga
    historico.forEach((registro) => {

        const item = document.createElement("li");

        const numero = document.createElement("span");

        numero.className = "historico-senha";
        numero.textContent = registro.senha;

        const horario = document.createElement("span");

        horario.className = "historico-horario";
        horario.textContent = registro.horario;

        item.appendChild(numero);
        item.appendChild(horario);

        elementoHistorico.appendChild(item);

    });

}


/* =========================================
   SOM DE SENHA CHAMADA
   Utiliza Web Audio API
========================================= */

function reproduzirSom() {

    // Verifica se o som está ativado
    if (!elementoSom.checked) {
        return;
    }

    // Cria o contexto de áudio quando necessário
    if (!contextoAudio) {

        const AudioContext =
            window.AudioContext || window.webkitAudioContext;

        if (!AudioContext) {
            console.warn("Web Audio API não disponível.");
            return;
        }

        contextoAudio = new AudioContext();

    }

    // Alguns navegadores exigem interação do usuário
    if (contextoAudio.state === "suspended") {
        contextoAudio.resume();
    }

    // Cria o oscilador responsável pelo som
    const oscilador = contextoAudio.createOscillator();

    // Controla o volume e o desaparecimento do som
    const volume = contextoAudio.createGain();

    // Frequência do aviso
    oscilador.type = "sine";
    oscilador.frequency.setValueAtTime(
        880,
        contextoAudio.currentTime
    );

    // Volume inicial
    volume.gain.setValueAtTime(
        0.0001,
        contextoAudio.currentTime
    );

    volume.gain.exponentialRampToValueAtTime(
        0.3,
        contextoAudio.currentTime + 0.05
    );

    // Finaliza o som suavemente
    volume.gain.exponentialRampToValueAtTime(
        0.0001,
        contextoAudio.currentTime + 0.7
    );

    // Conecta os componentes de áudio
    oscilador.connect(volume);
    volume.connect(contextoAudio.destination);

    // Reproduz o aviso
    oscilador.start();
    oscilador.stop(contextoAudio.currentTime + 0.75);

}


/* =========================================
   CHAMAR UMA NOVA SENHA
========================================= */

function chamarSenha(novaSenha, horario = obterHorario()) {

    // Converte a senha para texto
    novaSenha = String(novaSenha);

    // Valida a senha recebida
    if (!/^\d{1,4}$/.test(novaSenha)) {

        elementoMensagem.textContent =
            "Senha inválida. Utilize até 4 dígitos.";

        return false;

    }

    // Evita registrar novamente a mesma senha atual
    if (novaSenha === senhaAtual) {
        return false;
    }

    // Se já existe uma senha atual, ela vai para o histórico
    if (senhaAtual !== "") {

        historico.unshift({
            senha: senhaAtual,
            horario: obterHorario()
        });

    }

    // Limita o histórico a cinco registros
    historico = historico.slice(0, LIMITE_HISTORICO);

    // Define a nova senha atual
    senhaAtual = novaSenha;

    // Atualiza a interface
    atualizarSenha();
    atualizarHistorico();

    // Salva os dados no navegador
    salvarDados();

    // Atualiza a mensagem
    elementoMensagem.textContent =
        `Senha ${senhaAtual} chamada!`;

    // Reinicia a animação
    elementoSenhaAtual.classList.remove("animar-senha");

    void elementoSenhaAtual.offsetWidth;

    elementoSenhaAtual.classList.add("animar-senha");

    // Reproduz o aviso sonoro
    reproduzirSom();

    return true;

}


/* =========================================
   SIMULAÇÃO DE NOVAS SENHAS
========================================= */

function gerarSenhaTeste() {

    const senha = String(proximaSenha);

    // Avança para a próxima senha
    proximaSenha++;

    // Reinicia a sequência após 9999
    if (proximaSenha > 9999) {
        proximaSenha = 1;
    }

    return senha;

}


/* =========================================
   BOTÃO CHAMAR SENHA
========================================= */

elementoBotao.addEventListener("click", () => {

    const novaSenha = gerarSenhaTeste();

    chamarSenha(novaSenha);

});


/* =========================================
   CONTROLE DO SOM
========================================= */

elementoSom.addEventListener("change", () => {

    if (elementoSom.checked) {

        elementoMensagem.textContent = "Som ativado.";

    } else {

        elementoMensagem.textContent = "Som desativado.";

    }

});


/* =========================================
   RELÓGIO
========================================= */

function atualizarRelogio() {

    elementoRelogio.textContent = obterHorario();

}

// Atualiza o relógio a cada segundo
setInterval(atualizarRelogio, 1000);


/* =========================================
   INTEGRAÇÃO FUTURA COM ESP32
========================================= */

/*
   ENDPOINT FUTURO:

   GET http://IP_DO_ESP32/api/senha

   Resposta esperada:

   {
       "senha": "105",
       "horario": "15:30:00"
   }

   O ESP32 poderá enviar os dados por fetch()
   ou por uma conexão WebSocket.
*/


// Comunicação HTTP
async function consultarSenhaESP32() {

    try {

        const resposta = await fetch("/api/senha", {
            method: "GET",
            cache: "no-store"
        });

        if (!resposta.ok) {
            throw new Error("Falha na comunicação.");
        }

        const dados = await resposta.json();

        if (dados.senha !== undefined) {

            chamarSenha(
                dados.senha,
                dados.horario || obterHorario()
            );

        }

    } catch (erro) {

        console.error("Erro ao consultar ESP32:", erro);

    }

}


/*
   Comunicação WebSocket:

   O endereço será substituído pelo IP do ESP32.
*/

let conexaoESP32 = null;

function conectarWebSocket(endereco) {

    conexaoESP32 = new WebSocket(endereco);

    conexaoESP32.onopen = () => {

        console.log("Conectado ao ESP32.");

    };

    conexaoESP32.onmessage = (evento) => {

        try {

            const dados = JSON.parse(evento.data);

            if (dados.senha !== undefined) {

                chamarSenha(
                    dados.senha,
                    dados.horario || obterHorario()
                );

            }

        } catch (erro) {

            console.error("Mensagem inválida:", erro);

        }

    };

    conexaoESP32.onclose = () => {

        console.log("Conexão com ESP32 encerrada.");

    };

    conexaoESP32.onerror = (erro) => {

        console.error("Erro WebSocket:", erro);

    };

}


/* =========================================
   INICIALIZAÇÃO
========================================= */

// Restaura a interface quando a página abre
atualizarSenha();

atualizarHistorico();

atualizarRelogio();