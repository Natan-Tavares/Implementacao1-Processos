# ProcessFlow — Orquestrador de Processos

Implementação da atividade **Implementação 1 — Processos** (Infraestrutura de
Software, CESAR School).

## Arquivos

| Arquivo           | Descrição                                                             |
|--------------------|------------------------------------------------------------------------|
| `processflow.c`    | Código-fonte único do orquestrador de processos.                       |
| `Makefile`         | Compilação (`all`) e limpeza (`clean`).                                |
| `README.md`        | Este arquivo.                                                          |

## Sistema operacional utilizado

Desenvolvido e testado em **Linux (Ubuntu 24.04)**, usando `gcc`. Compatível
com qualquer sistema POSIX (Linux/Unix/macOS), pois utiliza apenas chamadas
de sistema padrão (`fork`, `execvp`, `waitpid`, `dup2`, `pipe`, `open`,
`chdir`).

## Como compilar

```bash
make
```

Gera o binário `processflow` no diretório atual.

## Como executar

Modo interativo (sem argumentos):

```bash
./processflow
processflow> task listar /bin/ls -l
processflow> run listar
processflow> exit
```

Modo workflow (arquivo `.pf` como argumento — cada linha lida é impressa
antes de ser processada, e o prompt não é exibido):

```bash
./processflow meu_workflow.pf
```

## Comandos suportados

- `task <nome> <programa> [argumentos...]` — cadastra uma tarefa.
- `run <nome>` — executa uma única tarefa cadastrada.
- `run sequential t1 t2 t3 ...` — executa as tarefas em sequência, uma
  aguardando o término da anterior.
- `run parallel t1 t2 t3 ...` — inicia todas as tarefas antes de aguardar o
  término do grupo.
- `run pipe t1 t2 t3 ...` — encadeia a saída de cada tarefa como entrada da
  próxima, cada uma em um processo diferente.
- `input <tarefa> <arquivo>` — redireciona a entrada da tarefa a partir de
  um arquivo.
- `output <tarefa> <arquivo>` — redireciona a saída da tarefa para um
  arquivo (truncando).
- `append <tarefa> <arquivo>` — redireciona a saída da tarefa para um
  arquivo, anexando ao final.
- `workdir <diretório>` — altera o diretório de trabalho usado pelas
  tarefas executadas posteriormente.
- `start <tarefa>` — inicia uma tarefa em background e imprime
  `[jobId] pid`.
- `jobs` — lista os jobs iniciados em background e seu status.
- `wait <jobId>` — aguarda o término de um job específico.
- `exit` — encerra o ProcessFlow (aguardando o término de qualquer job
  ainda em execução, para evitar processos zumbis).

## Decisões de implementação

- O prompt `processflow> ` só é impresso quando a entrada padrão é um
  terminal (`isatty`). Isso preserva o comportamento interativo exigido
  pela especificação quando usado por um usuário real, e evita poluir a
  saída quando a entrada é redirecionada de um arquivo em testes
  automatizados (como nos casos `teste1` e `teste2` fornecidos).
- Erros (tarefa/arquivo/diretório/job inexistente, falha ao criar processo,
  etc.) são impressos em `stderr` com o prefixo `processflow:`, permitindo
  distinguir claramente a saída dos programas executados da saída de
  diagnóstico do orquestrador.
- Ao final da execução (comando `exit` ou EOF), o ProcessFlow aguarda o
  término de qualquer job em background ainda ativo, evitando deixar
  processos zumbis.
- Não é usado `system()`, `popen()` nem qualquer delegação para outro
  shell; toda a criação/execução de processos é feita diretamente com
  `fork()`/`execvp()`/`waitpid()`/`pipe()`/`dup2()`.

## Limitações conhecidas

- Não há suporte a aspas para argumentos com espaços (os tokens são
  separados apenas por espaços/tabs).
- O tamanho máximo de uma linha de comando é 4096 caracteres.
