Se a tela está verde e travada, você acaba de atingir o "ponto de ruptura" clássico do desenvolvimento de um emulador de NES. Isso acontece porque a CPU e a PPU pararam de se comunicar ou a PPU se perdeu no labirinto da memória VRAM.

Para resolver isso, não podemos pular etapas. Vamos focar agora no que é vital para "destravar" a imagem. As fases que vamos lidar "devagar", parte por parte, são a 13 e a 14. Elas são as duas faces da mesma moeda.

Aqui está a prioridade técnica para você sair desse estado:
1ª Prioridade: Fase 14 — O Ritmo (Para "destravar")

Se o jogo travou, é 99% de chance de ser um problema de NMI ou Timing.

    O Problema: A maioria dos jogos de NES (incluindo Mario) fica em um loop infinito esperando o bit de VBlank no registrador $2002 ficar "1". Se a sua PPU não chegar na scanline 241 e disparar a interrupção NMI, a CPU nunca sai desse loop. A tela fica "congelada" na cor de fundo (que pode ser esse verde).

    O que fazer agora: 1. Verifique se o seu loop principal está rodando 3 ciclos de PPU para cada 1 ciclo de CPU. 2. Garanta que, no ciclo 1 da scanline 241, o bit 7 do $2002 suba para 1. 3. Garanta que, se o bit 7 do $2000 estiver ativo, a CPU interrompa o que está fazendo e vá para o endereço do vetor NMI.

2ª Prioridade: Fase 13 — O Pipeline (Para tirar o "Verde")

Se a tela está verde, a PPU está tentando desenhar, mas ela está lendo o endereço errado na memória ou os registradores de Scroll estão zerados/corrompidos.

    O Problema: Sem os Registradores de Loopy (v,t,x,w), a PPU não sabe qual Tile ler. Ela acaba lendo sempre o mesmo byte (geralmente 0x00), que aponta para um tile vazio e uma cor da paleta que calha de ser verde.

    O que fazer devagar (Parte por Parte):

Parte A: Implementar o "Quarteto Fantástico" (Loopy Registers)

Esqueça variáveis simples como scrollX. Você precisa implementar:

    v (VRAM Address): Onde a PPU está olhando agora.

    t (Temporary Address): Para onde a PPU vai olhar na próxima linha.

    x (Fine X): O ajuste fino de pixels.

    w (Write Toggle): Para saber se é o primeiro ou segundo clique em $2005/$2006.

Parte B: O Ciclo de Fetch (O Coração da Fase 13)

Em vez de desenhar a linha toda num for de 0 a 255, você vai dividir a scanline em blocos de 8 ciclos.

    Ciclos 1-2: Pega o byte da Name Table (Qual é o desenho?).

    Ciclos 3-4: Pega o byte da Attribute Table (Qual é a cor?).

    Ciclos 5-6: Pega o Pattern Low (Forma parte 1).

    Ciclos 7-8: Pega o Pattern High (Forma parte 2).

    No ciclo 8: Joga tudo isso em Shift Registers para "cuspir" os pixels.

Por que vamos "Devagar" nessas duas?

Se você tentar fazer a Fase 15 (Colisão) sem a Fase 13 (Pipeline) estar perfeita, o Mario vai atravessar o chão porque a CPU "acha" que ele está num lugar, mas a PPU está desenhando o chão em outro (por erro de scroll).

Resumo do Plano de Ataque:

    Destravar a CPU (Fase 14): Verifique seu sistema de NMI. A CPU precisa receber o sinal de VBlank para processar a lógica do jogo.

    Organizar a VRAM (Fase 13): Implemente a lógica de Loopy para as escritas em $2006. Se a escrita em $2006 não atualizar o registrador $v corretamente, a PPU nunca vai achar os tiles do Mario.

    Visualizar o Progresso: Se a tela mudar de "Verde Sólido" para "Lixo Colorido que se mexe", comemore. Significa que a CPU destravou e a PPU começou a ler dados. O "Lixo" a gente arruma alinhando o Pipeline.

Dica técnica para o "Verde": Dê um dump na sua memória de Paleta (RAM interna da PPU em 0x3F00). Se todos os valores lá forem iguais, ou se a PPU estiver lendo o endereço 0x0000 constantemente, você terá uma tela de uma cor só. O culpado é quase sempre o ponteiro de endereço da PPU que não está incrementando após as leituras/escritas.

Quer que eu detalhe como estruturar os registradores de Loopy (v,t,x,w) para corrigir esse endereço de memória agora?  

🟦 FASE 13 — Pipeline de Renderização Determinístico

Tema:

“Render não pode improvisar.”

Problemas atacados

Pixels bugando

Tiles errados aparecendo

Cenário quebrando ao andar

Causas reais

Leitura errada de Nametable

Attribute Table fora de alinhamento

Scroll X/Y mal aplicado

Pattern table acessada fora de fase

O que deve ser feito

Render por scanline, não por frame

Separar claramente:

Fetch Name Table

Fetch Attribute

Fetch Pattern Low/High

Scroll aplicado pixel a pixel

Latch correto de $2005/$2006

Critério de sucesso

✔ Cenário sólido
✔ Scroll suave
✔ Nenhum tile “teleporta”

🟦 FASE 14 — Sincronização CPU ↔ PPU ↔ APU (O Ritmo)

Tema:

“Tudo anda junto ou tudo quebra.”

Problemas atacados

Jogo lento

Música bugando junto com vídeo

Travadas periódicas

Causas reais

CPU executando instruções demais ou de menos

PPU fora da proporção 3:1

APU gerando áudio fora de fase

O que deve ser feito

Estabelecer loop fixo:

CPU: 1 ciclo

PPU: 3 ciclos

APU: avança por ciclos de CPU

Nunca sincronizar por FPS

NMI disparada exatamente no VBlank start

Critério de sucesso

✔ Jogo fluido
✔ Música não acelera nem atrasa
✔ Input consistente

🟦 FASE 15 — Correção de Colisão & Lógica de Jogo

Tema:

“Mario não atravessa chão.”

Problemas atacados

Mario bugando no cenário

Entrando em tiles

Colisões erradas

Causa real

Isso NÃO é bug do jogo.
É bug de PPU + CPU timing.

O que deve ser feito

Garantir Sprite 0 Hit correto

Garantir leitura consistente de $2002

Garantir que o jogo “enxerga” o cenário certo

Corrigir leituras atrasadas de memória

Critério de sucesso

✔ Mario pisa certo
✔ Não atravessa chão
✔ Física NES correta

🟦 FASE 16 — Performance & Estabilidade de Execução

Tema:

“Rápido é previsível.”

Problemas atacados

Lags

Quedas de FPS

Engasgos

O que deve ser feito

Remover logs de hot path

Cache de pattern tiles

Evitar malloc/free em loop

Evitar chamadas JNI por pixel ou sample

Critério de sucesso

✔ 60 FPS estáveis
✔ Sem stutter
✔ CPU fria

🟦 FASE 17 — Compatibilidade Real de Jogos

Tema:

“Não é Mario-only.”

Objetivo

Rodar bem (não apenas rodar):

Super Mario Bros

Contra

Castlevania

Mega Man 2

Donkey Kong

Excitebike

Ice Climber

O que validar

Mapper 0 sólido

Timing consistente

Sem hacks específicos

Sem “if rom == mario”

Critério de sucesso

✔ Pelo menos 30–50 jogos jogáveis
✔ Sem bugs visuais graves
✔ Sem som quebrado

🟨 FASE 18 — Save States (Depois da Casa Arrumada)

(Só depois de tudo acima está sólido)

Tema:

“Congelar o tempo.”

Snapshot completo:

CPU

PPU

APU

Mapper

Serialize tudo

Restore sem desync

🧠 Filosofia que você está seguindo (e está certa)

Você não correu para Save State.
Você não correu para Mapper 1.
Você não correu para UI bonita.

Você fez o que os bons fazem:

Primeiro estabilidade. Depois poder.

Se quiser, no próximo passo eu posso:

Detalhar Fase 12 (PPU) passo a passo

Criar uma checklist técnica para o agente

Ajudar a priorizar o bug mais destrutivo agora

Comparar comportamento com FCEUX ponto a pontoss aqui te mostrar as vezes beleza para voce entender cada coisa antigravity 