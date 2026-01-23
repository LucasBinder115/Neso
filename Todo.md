🔍 CAMADA A — Auditoria de Verdade (Beyond “funciona”)
1. Testes de estresse temporal

Objetivo: encontrar bugs que só aparecem com o tempo.

Deixe o emulador rodando:

10 minutos

30 minutos

1 hora

Sem pausar, sem reset

Observe:

Drift de áudio

Micro-stutter

Input ficando “borrachudo”

💡 Muitos emuladores falham depois de milhares de frames.

🎧 CAMADA B — Áudio como instrumento de precisão

Você já tem APU correta. Agora é musicalidade técnica.

O que caçar

Clique/pop ao iniciar/parar canais

Envelope que “morde” cedo demais

Pulse muito alto vs Triangle sumido

DMC interferindo no timing da CPU

Testes práticos

Mega Man 2:

Tela de seleção (envelopes)

Contra:

Tiros contínuos + música

Ninja Gaiden:

Música longa + mudanças rápidas

🎯 Meta realista:

“O som não chama atenção — ele simplesmente está certo.”

🎮 CAMADA C — Jogabilidade invisível (Input & Frame)

Aqui mora a diferença entre emulador bom e emulador amado.

Checklist

Input lido uma vez por frame

Sem polling por ciclo

Sem atraso variável

Mesmo input → mesmo frame sempre

Faça o teste clássico:

Pula repetidamente no mesmo ponto

Mario sempre pula igual?

Mega Man sempre responde igual?

Se variar: tem jitter.

🧠 CAMADA D — Determinismo absoluto (modo cirúrgico)

Mesmo sem save state ainda, simule-o mentalmente.

Exercício poderoso

Pausa no frame X

Anota:

PC da CPU

Scanline + ciclo da PPU

Sequencer step da APU

Continua execução

Volta e compara

Se não bate:
➡️ tem estado escondido.

🧹 CAMADA E — Higiene extrema de código (luxo técnico)

Isso aqui não melhora FPS.
Melhora confiança.

Cada struct:

Estado?

Temporário?

Cache?

Cada variável global:

Precisa existir?

Cada static:

Está escondendo bug?

💬 Regra antiga:

Se você não consegue explicar por que algo é static, provavelmente não deveria ser.

📦 CAMADA F — Builds como contrato

Transforme o build em guardião da qualidade.

-Wall -Wextra -Werror

Sanitizers (quando possível):

UBSan

ASan (em desktop)

Zero warning permitido

O código passa a se defender sozinho.

🧪 CAMADA G — Test ROMs cruéis (sem dó)

Além dos jogos comerciais:

instr_test

apu_test

ppu_scroll

ROMs que “não são jogo”

Eles não querem divertir.
Querem quebrar você.

🧭 CAMADA H — Simplicidade final (menos é mais)

Pergunta final que vale ouro:

“Se eu tivesse que manter isso por 5 anos… onde eu tropeçaria?”

Refatore só esses pontos.

🏁 Quando saber que está pronto para avançar?

Quando você:

Não tem medo de mexer no código

Confia no áudio sem fone

Joga sem pensar no emulador

Esquece que está testando

Aí sim…
Save State vira consequência.
Online vira possibilidade.

Se quiser, no próximo passo eu posso:

Montar um roteiro diário de lapidação (30–60 min por dia)

Criar uma checklist de áudio NES “real feel”

Te ajudar a definir “quando parar” (isso é raro e valioso)

Você não está só polindo.