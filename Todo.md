

Vou responder em três níveis, como manda a boa engenharia:

Leitura correta do que você observou (diagnóstico real)

O que isso significa tecnicamente (sem ilusão)

Proposta clara das Fases 16, 17, 18, 19 e 20 — ajustadas à sua realidade atual

Nada de jogar fase fora. Vamos evoluir o mapa.

1️⃣ Diagnóstico honesto do estado atual

O seu resultado é excelente, mesmo com bugs.

Vamos traduzir jogo por jogo:

✅ Super Mario Bros

Funciona completo

Áudio correto

Colisão correta
👉 Base validada

⚠️ Mega Man 1 (Mapper 2)

Jogo roda

Sem áudio
👉 CPU + Mapper ok
👉 APU ainda falha em casos mais exigentes

⚠️ Battletoads (Mapper 7)

Jogo roda

Sprites bugados
👉 Mapper parcialmente correto
👉 Problema clássico de CHR banking + sprite fetch timing

❌ Contra / Super C (Mapper 2 / MMC1)

Não roda
👉 Problema de PRG banking / masking / reset behavior

⚠️ Ninja Gaiden (Tecmo)

Roda

Áudio e sprites bugados
👉 Jogo extremamente agressivo com PPU/APU
👉 Excelente sinal: se ele roda, o core é real

📌 Resumo executivo:

Você está exatamente no ponto onde o emulador funciona, mas ainda não é tolerante ao caos dos jogos reais.

Isso é esperado. Tradicional. Clássico.

2️⃣ O que isso significa tecnicamente (sem romantizar)
🔴 Importante entender isso:

Compatibilidade não é implementar mapper.
É sobreviver ao abuso que o jogo faz dele.

Contra, Mega Man, Ninja Gaiden:

trocam banco no meio do frame

escrevem em registradores “fora de ordem”

dependem de reset states específicos

fazem timing borderline

Mario não faz isso.

Você agora está lidando com jogos que testam o hardware.

3️⃣ Proposta das próximas fases (16 → 20)

Agora vem a parte importante:
as fases mudam de natureza.

Antes: “fazer funcionar”
Agora: “tornar robusto”

os games para se ter compativel agora no emulador independentemente se vao funcionar em sprites ou audio se estao compativeis dia vencido hell yeah

🟦 FASE 16 — Performance & Determinismo (Fundação Invisível)

Tema:

“Sem determinismo, compatibilidade é sorte.”

Objetivo real

Antes de consertar Contra, você precisa garantir que:

o mesmo input

na mesma ROM

gera o mesmo comportamento

O que fazer agora

Remover 100% dos logs em hot path

Garantir stepCpu → stepPpu determinístico

Confirmar que um frame sempre executa o mesmo número de ciclos

Medir ciclos, não FPS

Critério de sucesso

✔ FPS estável
✔ Sem micro-stutter
✔ Replay consistente

📌 Essa fase resolve bugs “fantasmas” depois.

🟦 FASE 17 — Robustez de Mapper (Compatibilidade Real)

Tema:

“Mapper não é switch. É contrato.”

Aqui você não implementa mais mapper por mapper.
Você cria uma base sólida para todos.

Subfases recomendadas
17.1 — Mapper 2 (UNROM) — Contra

PRG mask correto

Reset state idêntico ao hardware

Banco fixo no último slot

Escrita fora de faixa ignorada (não crashar)

👉 Contra só quebra mapper mal defensivo

17.2 — Mapper 1 (MMC1) — Mega Man 2 / Super C

Shift register exato (5 writes)

Reset no bit 7

Delay real entre writes

PRG/CHR mode respeitado

📌 MMC1 mal implementado = áudio e gráficos errados.

17.3 — Mapper 7 (AOROM) — Battletoads

32KB PRG switching

Single-screen mirroring

CHR fixa, mas PPU fetch sensível

Sprites bugados aqui quase sempre são:
👉 timing de sprite fetch + nametable mirror

17.4 — Mapper 3 (CNROM)

Simples, mas exige CHR switch correto

Bugs aqui indicam erro no latch do PPU

Critério de sucesso

✔ Contra inicia
✔ Mega Man 2 passa da intro
✔ Battletoads sem sprites explodindo

🟦 FASE 18 — APU Profissional (Áudio de Verdade)

Tema:

“Áudio não perdoa timing errado.”

Agora o áudio vira protagonista.

O que corrigir

APU clock exatamente sincronizado com CPU

Sem “catch-up” agressivo

Buffer circular previsível

Filtro low-pass consistente

DMC channel (se ainda não estiver)

📌 Mega Man sem áudio = APU fora de fase, não bug de jogo.

Critério de sucesso

✔ Música estável
✔ Pitch consistente
✔ Sem “respiração” sonora

🟦 FASE 19 — PPU Edge Cases (Jogos Cruéis)

Tema:

“Jogos não respeitam a PPU. Eles a exploram.”

Aqui entram:

Sprite overflow flag real

Sprite evaluation timing

Priority rules

Mid-frame changes de scroll

Status bar tricks

📌 Ninja Gaiden e Battletoads vivem aqui.

Critério de sucesso

✔ Sprites estáveis
✔ Status bars corretas
✔ Sem flicker inesperado

🟦 FASE 20 — Compatibilidade Ampla & Regressão

Tema:

“Não quebrar o que já funcionava.”

Aqui você:

cria uma ROM list

testa 30–50 jogos

registra bugs conhecidos

garante que Mario continua perfeito

Critério de sucesso

✔ Lista de jogos jogáveis
✔ Bugs documentados
✔ Emulador confiável

🧠 Conclusão direta (importante)

Lucas, grava isso:

Seu emulador já é real.
O que falta agora não é “fazer funcionar”,
é torná-lo resistente à maldade dos jogos.

Contra não funcionar não é fracasso.
É sinal de que você chegou no nível onde Contra começa a falar.

🎯 Próximo passo recomendado (agora mesmo)

Se eu tivesse que escolher uma única coisa para você fazer amanhã:

👉 Fase 17.1 — Mapper 2 (UNROM) defensivo, focado em Contra

Porque:

desbloqueia vários jogos

força robustez

revela bugs estruturais

Se quiser, no próximo passo eu posso:

te guiar mapper por mapper

montar uma checklist específica para Contra

comparar comportamento com FCEUX ciclo a ciclo
