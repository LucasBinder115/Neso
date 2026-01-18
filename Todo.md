🧠 Diagnóstico honesto do estado atual

Pelo que você descreveu, tecnicamente você está em:

✅ Fase 13 — IMPLEMENTADA (base sólida)

Pipeline existe

Loopy registers funcionam

Attribute shifters 16-bit corretos

Sprite 0 hit real

Scroll funcional

⚠️ Ainda não perfeita, mas funcional o suficiente para jogos reais.

✅ Fase 14 — IMPLEMENTADA (funcional, não refinada)

NMI dispara corretamente

CPU não trava mais

APU em fase (timbre e pitch corretos)

Timing geral coerente

⚠️ Performance e micro-jitter ainda presentes (esperado).

🟡 Fase 15 — PARCIALMENTE IMPLEMENTADA

Aqui vem um ponto importante:

A colisão já funciona porque a base está certa.
Isso é sinal de sucesso das fases 13 e 14.

Mas ainda faltam:

leituras tardias

edge cases

jogos que abusam de Sprite 0 hit

📌 Então o que muda no TODO.md?
❌ O que NÃO faz mais sentido manter

No todo.md, remova o tom de “destravamento” das fases 13 e 14.

Essas frases agora estão obsoletas:

“para sair da tela verde”

“para destravar”

“ponto de ruptura”

👉 Isso já foi vencido.

✅ Nova proposta de estrutura (recomendada)
🟦 FASE 13 — Pipeline de Renderização (VALIDAÇÃO & AJUSTE FINO)

Status: 🟢 Funcional
Objetivo agora: Fidelidade, não sobrevivência

Novo foco:

Pequenos glitches de tile

Scroll jitter

Attribute edge cases

Sprite overflow

Sprite priority

Critério atualizado:
✔ Mario jogável
✔ Cenário correto
✔ Glitches raros e localizados

🟦 FASE 14 — Sincronização (ESTABILIZAÇÃO)

Status: 🟢 Funcional
Objetivo agora: Estabilidade e previsibilidade

Novo foco:

Remover lentidões artificiais

Corrigir frames “travadinhos”

Garantir APU perfeitamente em fase

Medir ciclos, não FPS

Critério atualizado:
✔ Jogo consistente
✔ Áudio não “respira”
✔ Input sem delay perceptível

🟦 FASE 15 — Lógica de Jogo & Casos Limite

Status: 🟡 Em andamento real

Agora sim ela vira protagonista.

Novo foco:

Sprite 0 hit em jogos não-Mario

Jogos com split-screen

Leituras maliciosas de $2002

Jogos que dependem de timing “errado”

🟦 FASE 16 — Performance (AGORA SIM)

Esse é o momento certo.
Antes disso seria desperdício.

Aqui você:

remove logs

troca debug por release

cacheia tiles

reduz JNI

mede frame time

🧭 Conclusão clara e sem rodeio

Lucas, escuta isso com atenção:

Você não precisa mais “seguir as fases para destravar”.
Você precisa usar as fases como checklist de qualidade.

As fases continuam válidas como mapa mental,
mas o projeto mudou de estado.

Antes:

“Será que funciona?”

Agora:

“Funciona. Onde não está fiel?”

Isso é outra liga.

📍 Minha recomendação prática (ação imediata)

Atualize o todo.md hoje

Marque Fase 13 e 14 como Funcionais

Mude o texto para “Refinamento”

Abra um novo bloco

“Glitches conhecidos”

“Problemas de performance”

“Diferenças vs FCEUX”

Pare de usar Mario como único juiz

Próximo jogo: Contra ou Ice Climber

Se eles rodarem → seu emulador é real