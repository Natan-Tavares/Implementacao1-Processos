SHELL   := /bin/bash
CC      = gcc
CFLAGS  = -Wall -Wextra -std=gnu11 -g
TARGET  = processflow
SRC     = processflow.c

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET) tests/*-actual.txt tests/*.o

test: $(TARGET)
	@if [ ! -d tests ]; then \
		echo "Nenhum diretorio 'tests/' encontrado - casos de teste nao incluidos nesta entrega."; \
		echo "Para testar, crie tests/testeN-entrada.txt e tests/testeN-saida.txt."; \
		exit 0; \
	fi; \
	echo "Executando testes..."; \
	pass=0; fail=0; \
	./$(TARGET) < tests/teste1-entrada.txt > tests/teste1-actual.txt; \
	if diff -q <(cat tests/teste1-actual.txt) <(cat tests/teste1-saida.txt; echo) > /dev/null 2>&1 || \
	   diff -q tests/teste1-actual.txt tests/teste1-saida.txt > /dev/null 2>&1; then \
		echo "[PASS] teste1 (interativo)"; pass=$$((pass+1)); \
	else echo "[FAIL] teste1 (interativo)"; fail=$$((fail+1)); fi; \
	./$(TARGET) < tests/teste2-entrada.txt > tests/teste2-actual.txt; \
	if diff -q <(cat tests/teste2-actual.txt) <(cat tests/teste2-saida.txt; echo) > /dev/null 2>&1 || \
	   diff -q tests/teste2-actual.txt tests/teste2-saida.txt > /dev/null 2>&1; then \
		echo "[PASS] teste2 (interativo - pipe)"; pass=$$((pass+1)); \
	else echo "[FAIL] teste2 (interativo - pipe)"; fail=$$((fail+1)); fi; \
	./$(TARGET) tests/teste3-entrada.txt > tests/teste3-actual.txt; \
	if diff -q <(cat tests/teste3-actual.txt) <(cat tests/teste3-saida.txt; echo) > /dev/null 2>&1 || \
	   diff -q tests/teste3-actual.txt tests/teste3-saida.txt > /dev/null 2>&1; then \
		echo "[PASS] teste3 (workflow/batch)"; pass=$$((pass+1)); \
	else echo "[FAIL] teste3 (workflow/batch)"; fail=$$((fail+1)); fi; \
	rm -f saida.txt; \
	echo "Resumo: $$pass passaram, $$fail falharam"
