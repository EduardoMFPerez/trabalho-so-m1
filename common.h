#ifndef GERENCIADOR_COMMON_H
#define GERENCIADOR_COMMON_H

#include <cstdio>
#include <cstring>

#define MAX_NOME 50
#define MAX_RESULTADOS 64

#define OP_EXIT     0
#define OP_SELECT   1
#define OP_INSERT   2
#define OP_DELETE   3
#define OP_UPDATE   4

#define SEL_POR_ID   1
#define SEL_POR_NOME 2
#define SEL_TODOS    3

#define STATUS_OK             0
#define STATUS_NAO_ENCONTRADO 1
#define STATUS_ERRO           2

#define PIPE_REQ  "\\\\.\\pipe\\bd_req"
#define PIPE_RESP "\\\\.\\pipe\\bd_resp"

struct Registro {
    int  id;
    char nome[MAX_NOME];
};

struct Requisicao {
    int op;
    int id;
    char nome[MAX_NOME];
    int sel_tipo;
};

struct Resposta {
    int req_op;
    int req_id;
    char req_nome[MAX_NOME];
    int status;
    int registros;
    char msg[256];
    Registro regs[MAX_RESULTADOS];
};

static inline void copiarNome(char* destino, const char* origem) {
    std::snprintf(destino, MAX_NOME, "%s", origem);
}

#endif