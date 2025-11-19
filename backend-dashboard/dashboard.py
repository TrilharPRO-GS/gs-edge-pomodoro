"""
* GS Edge Computing: Timer Pomodoro "Trilhar"
* Rodrigo Cardoso Tadeo - RM: 562010
* Vinicius Cavalcanti dos Reis - RM: 562063
*
* Este arquivo implementa o Dashboard (Software) que atua como:
* 1. "Configurador": Envia comandos para o Hardware via FIWARE.
* 2. "Visualizador": Lê o estado do Hardware (Gêmeo Digital) no FIWARE.
"""

# Importa a biblioteca Streamlit para criar a interface gráfica web
import streamlit as st
# Importa a biblioteca Requests para fazer chamadas HTTP (GET/PATCH) ao FIWARE
import requests
# Importa a biblioteca Time para controlar o intervalo de atualização (sleep)
import time
# Importa timedelta para formatar segundos em formato de tempo legível (00:00)
from datetime import timedelta

# --- CONFIGURAÇÃO ---
# Define o IP da máquina. 'localhost' é usado porque este script roda DENTRO da VM Azure.
IP_DA_SUA_VM = "localhost" 

# Monta a URL base da API do Orion Context Broker (Porta 1026)
ORION_URL = f"http://{IP_DA_SUA_VM}:1026"

# Define o ID da entidade que representa nosso dispositivo físico (ESP32)
ENTITY_ID = "urn:ngsi-ld:SessaoEstudo:001"

# Define os cabeçalhos obrigatórios para falar com o IoT Agent/FIWARE
# 'fiware-service': 'smart' define o grupo de serviço configurado no Postman
FIWARE_HEADERS = {'fiware-service': 'smart', 'fiware-servicepath': '/'}

# --- COMUNICAÇÃO (Enviar Comandos) ---
def send_command(command_name: str, value: str):
    """
    Função para enviar comandos do Dashboard para o ESP32 via FIWARE.
    Ex: send_command("set_duracao", "15")
    """
    # Monta a URL específica para atualizar atributos da entidade (/attrs)
    url_comando = f"{ORION_URL}/v2/entities/{ENTITY_ID}/attrs" 
    
    # Cria o payload JSON no formato NGSI-v2 para comandos
    # O IoT Agent traduzirá isso para MQTT e enviará ao ESP32
    payload = { command_name: { "type": "command", "value": value } }
    
    try:
        # Faz uma requisição PATCH (atualização parcial) para o Orion
        requests.patch(url_comando, headers=FIWARE_HEADERS, json=payload, timeout=5) 
        # Exibe uma mensagem flutuante (toast) de sucesso na tela
        st.toast(f"Comando enviado: {value}")
    except: 
        # Se houver erro (ex: Orion fora do ar), exibe erro na tela
        st.error("Erro de conexão")

# --- COMUNICAÇÃO (Ler Status) ---
def get_status() -> dict:
    """
    Função para ler o estado atual (Telemetria) do dispositivo no FIWARE.
    Retorna um dicionário com os dados formatados.
    """
    # URL para pegar a entidade completa
    url_getter = f'{ORION_URL}/v2/entities/{ENTITY_ID}'
    
    try:
        # Faz uma requisição GET para ler os dados do Orion
        response = requests.get(url_getter, headers=FIWARE_HEADERS, timeout=3)
        # Converte a resposta (JSON) em um dicionário Python
        data = response.json()
        
        # Extrai os valores dos atributos com segurança (.get).
        # Se o atributo não existir (ainda não criado), usa um valor padrão.
        
        # Status atual (foco, pausa, ocioso)
        status = data.get("status_sessao", {}).get("value", "ocioso")
        # Contagem de sessões parciais (0 a 4)
        sessoes = data.get("sessoes_completas", {}).get("value", 0)
        # Contagem de ciclos completos (Ouro)
        ciclos = data.get("ciclos_totais", {}).get("value", 0) 
        # Tempo restante em segundos (vindo do ESP32)
        tempo_seg = data.get("tempo_restante_seg", {}).get("value", 0)
        # Configuração atual da pausa curta
        pausa_cfg = data.get("duracao_pausa_configurada", {}).get("value", 5) 
        # Tipo da próxima pausa (curta ou longa)
        pausa_tipo = data.get("proxima_pausa_tipo", {}).get("value", "curta") 
        
        # Formata os segundos para string "MM:SS" ou "HH:MM:SS"
        tempo_formatado = str(timedelta(seconds=tempo_seg))
        # Remove o "0:" inicial se for menos de 1 hora (estética)
        if tempo_formatado.startswith("0:"): tempo_formatado = tempo_formatado[2:]
        
        # Retorna todos os dados tratados
        return { "status": status, "sessoes": sessoes, "ciclos": ciclos, "tempo": tempo_formatado, "pausa_cfg": pausa_cfg, "pausa_tipo": pausa_tipo }
    
    except: 
        # Em caso de erro, retorna dados "zerados" para o dashboard não quebrar
        return {"status": "DESCONECTADO", "sessoes": 0, "ciclos": 0, "tempo": "00:00", "pausa_cfg": 5, "pausa_tipo": "curta"}

# --- INTERFACE GRÁFICA (Streamlit) ---

# Configura o título da página e layout centralizado
st.set_page_config(layout="centered", page_title="Trilhar IoT")
st.title("🚀 Dashboard Pomodoro Trilhar")
st.caption("Um Gêmeo Digital da sua sessão de estudos na busca da requalificação (FIAP GS Edge Computing)")

# --- SEÇÃO 1: Botões de Foco ---
st.subheader("1. Configurar Foco (Comando)")
# Cria 3 colunas para alinhar os botões lado a lado
col1, col2, col3 = st.columns(3)

# Botão 1: Envia comando para definir timer de 15 (minutos/segundos conforme ESP32)
with col1:
    if st.button("Definir 15 Minutos", use_container_width=True): send_command("set_duracao", "15")
# Botão 2: Envia comando para definir timer de 25
with col2:
    if st.button("Definir 25 Minutos", use_container_width=True): send_command("set_duracao", "25")
# Botão 3: Envia comando para definir timer de 50
with col3:
    if st.button("Definir 50 Minutos", use_container_width=True): send_command("set_duracao", "50")

# --- SEÇÃO 2: Botões de Descanso ---
st.subheader("2. Configurar Descanso (Comando)")
colA, colB, colC = st.columns(3)

# Botões para configurar o tempo de pausa curta
with colA:
    if st.button("Definir 3 Minutos", use_container_width=True): send_command("set_pausa", "3")
with colB:
    if st.button("Definir 5 Minutos", use_container_width=True): send_command("set_pausa", "5")
with colC:
    if st.button("Definir 10 Minutos", use_container_width=True): send_command("set_pausa", "10")

# Linha divisória visual
st.divider()

# --- SEÇÃO 3: Visualização (Telemetria) ---
st.subheader("3. Status do Dispositivo (Telemetria)")

# Cria "placeholders" (espaços vazios) que serão atualizados no loop
# Isso evita que a página inteira recarregue, atualizando apenas estes elementos
status_ph = st.empty()
tempo_ph = st.empty()
sessoes_ph = st.empty()
ciclos_ph = st.empty()
pausa_ph = st.empty() 

# --- LOOP PRINCIPAL (Atualização Dinâmica) ---
while True:
    # Busca os dados atuais do FIWARE
    data = get_status()
    
    # Lógica visual: Define ícones e textos baseados no status recebido
    st_label = data["status"].upper()
    st_icon = "💤" # Ícone padrão (Ocioso)
    st_color = "normal"
    
    if data["status"] == "foco":
        st_icon = "🎯" # Ícone de Foco
    elif data["status"] == "pausa_descanso":
        st_label = "DESCANSANDO"
        st_icon = "☕" # Ícone de Café
    elif data["status"] == "pausa_foco":
        st_label = "PAUSADO"
        st_icon = "⏸️" # Ícone de Pausa
    elif data["status"] == "pausa_automatica":
        st_label = "AUSENTE (Auto-Pause)"
        st_icon = "🚶‍♂️" # Ícone de Ausente
    elif data["status"] == "foco_concluido":
        st_label = "FOCO CONCLUÍDO!"
        st_icon = "✅" # Ícone de Conclusão

    # Atualiza o Widget de Status
    with status_ph.container():
        st.metric("Status da Sessão", st_label, f"{st_icon}", delta_color=st_color) 
    
    # Atualiza os Widgets de Tempo e Contadores
    tempo_ph.metric("Tempo Restante", data["tempo"])
    sessoes_ph.metric("Sessões Completas (Meta 4)", f"{data['sessoes']} / 4")
    
    # Atualiza o Contador de Ciclos Totais
    ciclos_ph.metric("Ciclos Totais (4x4)", f"{data['ciclos']} Completos")

    # Lógica para mostrar se a próxima pausa é Curta ou Longa
    p_lbl = f"{data['pausa_cfg']} min (Pausa Curta)"
    if data['pausa_tipo'] == "longa": p_lbl = "20 min (Pausa Longa)"
    
    # Atualiza o Widget de Próximo Descanso
    pausa_ph.metric("Próximo Descanso", p_lbl)
    
    # Pausa o loop por 1 segundo antes de atualizar novamente
    # (Define a frequência de atualização do dashboard)
    time.sleep(1)