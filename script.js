(() => {
  "use strict";

  // Deve ser idêntico ao prefixo configurado no sketch.ino.
  const MQTT = {
    url: "wss://broker.emqx.io:8084/mqtt",
    topicoBase: "lanchonete/senhas/brunosevero-7f3a9c",
    reconnectPeriod: 3000,
    connectTimeout: 10000
  };
  const TOPICO_ESTADO = `${MQTT.topicoBase}/estado`;
  const TOPICO_COMANDO = `${MQTT.topicoBase}/comando`;
  const TOPICO_STATUS = `${MQTT.topicoBase}/status`;
  const LIMITE_HISTORICO = 5;

  const $ = (id) => document.getElementById(id);
  const el = {
    senha: $("senhaAtual"), historico: $("listaHistorico"), mensagem: $("mensagem"),
    contador: $("contador"), chamar: $("btnTeste"), limpar: $("btnLimparHistorico"),
    som: $("somAtivo"), auto: $("autoUpdate"), status: $("statusConexao"),
    statusTexto: $("statusTexto"), relogio: $("relogio"), versao: $("versao")
  };

  let clienteMQTT = null;
  let ultimaVersao = "";
  let audioContext = null;
  let ultimoEstado = { senha: "", horario: "", historico: [] };

  function horario() {
    return new Date().toLocaleTimeString("pt-BR", { hour12: false });
  }

  function atualizarRelogio() { el.relogio.textContent = horario(); }

  function definirStatus(conectado, texto) {
    el.status.classList.toggle("conectado", conectado);
    el.statusTexto.textContent = texto;
    el.versao.textContent = conectado ? "MQTT conectado" : "Aguardando MQTT";
  }

  function somChamada() {
    if (!el.som.checked) return;
    try {
      const Audio = window.AudioContext || window.webkitAudioContext;
      if (!Audio) return;
      audioContext ||= new Audio();
      if (audioContext.state === "suspended") audioContext.resume();
      [880, 1046, 880].forEach((freq, i) => {
        const osc = audioContext.createOscillator();
        const ganho = audioContext.createGain();
        const inicio = audioContext.currentTime + i * 0.16;
        osc.frequency.value = freq;
        ganho.gain.setValueAtTime(0.0001, inicio);
        ganho.gain.exponentialRampToValueAtTime(0.18, inicio + 0.02);
        ganho.gain.exponentialRampToValueAtTime(0.0001, inicio + 0.13);
        osc.connect(ganho);
        ganho.connect(audioContext.destination);
        osc.start(inicio);
        osc.stop(inicio + 0.14);
      });
    } catch (erro) { console.warn("Áudio indisponível:", erro); }
  }

  function renderizar(dados, tocar = false, forcar = false) {
    const senha = dados.senha || "";
    const hist = Array.isArray(dados.historico)
      ? dados.historico.slice(0, LIMITE_HISTORICO)
      : [];
    const versao = JSON.stringify({ senha, horario: dados.horario || "", hist });
    if (!forcar && versao === ultimaVersao) return;
    ultimaVersao = versao;
    ultimoEstado = { senha, horario: dados.horario || "", historico: hist };

    el.senha.textContent = senha || "----";
    el.historico.replaceChildren();
    if (!hist.length) {
      const li = document.createElement("li");
      li.className = "historico-vazio";
      li.textContent = "Nenhuma senha chamada ainda.";
      el.historico.appendChild(li);
    } else {
      hist.forEach((item, i) => {
        const li = document.createElement("li");
        li.style.animationDelay = `${i * 60}ms`;
        const numero = document.createElement("span");
        numero.className = "historico-senha";
        numero.textContent = String(item.senha).padStart(4, "0");
        const hora = document.createElement("span");
        hora.className = "historico-horario";
        hora.textContent = item.horario || "--:--:--";
        li.append(numero, hora);
        el.historico.appendChild(li);
      });
    }
    el.contador.textContent = `${hist.length} / ${LIMITE_HISTORICO}`;
    el.mensagem.textContent = senha
      ? `Última chamada: ${senha} às ${dados.horario || "--:--:--"}`
      : "Aguardando próxima chamada...";

    if (tocar) {
      el.senha.classList.remove("animar");
      void el.senha.offsetWidth;
      el.senha.classList.add("animar");
      somChamada();
    }
  }

  function publicarComando(comando) {
    if (!clienteMQTT || !clienteMQTT.connected) {
      el.mensagem.textContent = "MQTT desconectado. Aguarde a reconexão.";
      return false;
    }
    clienteMQTT.publish(TOPICO_COMANDO, JSON.stringify(comando), { qos: 0, retain: false }, (erro) => {
      if (erro) {
        el.mensagem.textContent = "Falha ao enviar comando MQTT.";
        console.error("Falha MQTT:", erro);
      }
    });
    return true;
  }

  function conectarMQTT() {
    if (!window.mqtt) {
      definirStatus(false, "Biblioteca MQTT não carregou");
      el.mensagem.textContent = "Não foi possível carregar MQTT.js. Verifique a internet.";
      return;
    }

    const clientId = `web-lanchonete-${Math.random().toString(16).slice(2, 10)}`;
    clienteMQTT = window.mqtt.connect(MQTT.url, {
      clientId,
      clean: true,
      connectTimeout: MQTT.connectTimeout,
      reconnectPeriod: MQTT.reconnectPeriod,
      keepalive: 30
    });

    clienteMQTT.on("connect", () => {
      definirStatus(true, "Broker MQTT conectado");
      if (el.auto.checked) assinarEstado();
      clienteMQTT.subscribe(TOPICO_STATUS);
      el.mensagem.textContent = "Conectado. Aguardando estado do ESP32...";
    });

    clienteMQTT.on("reconnect", () => definirStatus(false, "Reconectando MQTT..."));
    clienteMQTT.on("offline", () => definirStatus(false, "MQTT offline"));
    clienteMQTT.on("close", () => definirStatus(false, "Conexão MQTT fechada"));
    clienteMQTT.on("error", (erro) => console.error("Erro MQTT:", erro));

    clienteMQTT.on("message", (topico, mensagem) => {
      const conteudo = mensagem.toString();
      if (topico === TOPICO_ESTADO) {
        try {
          const dados = JSON.parse(conteudo);
          if (el.auto.checked) {
            const mudouSenha = dados.senha && dados.senha !== ultimoEstado.senha;
            renderizar(dados, Boolean(mudouSenha));
          }
        } catch (erro) { console.error("Estado MQTT inválido:", erro); }
      } else if (topico === TOPICO_STATUS) {
        console.info("ESP32 status:", conteudo);
      }
    });
  }

  function assinarEstado() {
    if (clienteMQTT?.connected) clienteMQTT.subscribe(TOPICO_ESTADO, { qos: 0 });
  }

  function cancelarEstado() {
    if (clienteMQTT?.connected) clienteMQTT.unsubscribe(TOPICO_ESTADO);
  }

  el.chamar.addEventListener("click", () => {
    if (publicarComando({ acao: "proxima" })) {
      el.mensagem.textContent = "Solicitação enviada ao ESP32...";
    }
  });

  el.limpar.addEventListener("click", () => {
    if (!window.confirm("Deseja realmente limpar a senha atual e o histórico do ESP32?")) return;
    if (publicarComando({ acao: "limpar" })) {
      el.mensagem.textContent = "Solicitação para limpar o painel enviada...";
    }
  });

  el.auto.addEventListener("change", () => {
    if (el.auto.checked) {
      assinarEstado();
      // Solicita uma publicação atual para atualizar o navegador.
      publicarComando({ acao: "sincronizar" });
      el.mensagem.textContent = "Recebimento de atualizações ativado.";
    } else {
      cancelarEstado();
      el.mensagem.textContent = "Atualizações automáticas pausadas.";
    }
  });

  el.som.checked = localStorage.getItem("painelSom") !== "0";
  el.som.addEventListener("change", () => {
    localStorage.setItem("painelSom", el.som.checked ? "1" : "0");
  });

  document.addEventListener("keydown", (evento) => {
    if (evento.target.matches("input, button, textarea, select")) return;
    if (evento.code === "Space" || evento.code === "Enter") {
      evento.preventDefault();
      el.chamar.click();
    }
  });

  atualizarRelogio();
  window.setInterval(atualizarRelogio, 1000);
  conectarMQTT();
})();
