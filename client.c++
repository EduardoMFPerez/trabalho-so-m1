#include "common.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

static bool writeAll(HANDLE h, const void* buf, DWORD n) {
    const char* p = static_cast<const char*>(buf);
    DWORD total = 0;
    while (total < n) {
        DWORD w = 0;
        if (!WriteFile(h, p + total, n - total, &w, NULL)) return false;
        if (w == 0) return false;
        total += w;
    }
    return true;
}

static bool readAll(HANDLE h, void* buf, DWORD n) {
    char* p = static_cast<char*>(buf);
    DWORD total = 0;
    while (total < n) {
        DWORD r = 0;
        if (!ReadFile(h, p + total, n - total, &r, NULL)) return false;
        if (r == 0) return false;
        total += r;
    }
    return true;
}

static void limparEntrada() {
    int c;
    while ((c = getchar()) != '\n' && c != EOF) {}
}

static void lerLinha(char* buf, int n) {
    if (fgets(buf, n, stdin)) {
        size_t l = strlen(buf);
        if (l > 0 && buf[l - 1] == '\n') buf[l - 1] = '\0';
    }
}

static void enviar(HANDLE hReq, HANDLE hResp, const Requisicao& req) {
    if (!writeAll(hReq, &req, sizeof(req))) {
        printf("  ERRO: falha ao enviar requisicao.\n");
        return;
    }
    Resposta r;
    if (!readAll(hResp, &r, sizeof(r))) {
        printf("  ERRO: falha ao ler resposta do servidor.\n");
        return;
    }
    printf("  Resposta (origem op=%d id=%d): %s\n", r.req_op, r.req_id, r.msg);
    if (r.req_op == OP_SELECT && r.registros > 0) {
        for (int i = 0; i < r.registros; i++) {
            printf("    -> id=%d nome='%s'\n", r.regs[i].id, r.regs[i].nome);
        }
    }
}

static void modoAutomatico(HANDLE hReq, HANDLE hResp, int total) {
    const char* nomes[] = {
        "Ana", "Bruno", "Carla", "Diego", "Eduarda", "Felipe",
        "Gabriela", "Heitor", "Isabela", "Karen", "Lucas", "Marina"
    };
    int num_nomes = (int)(sizeof(nomes) / sizeof(nomes[0]));

    Requisicao reqs[128];
    int n = 0;
    for (int i = 0; i < total && n < 128; i++) {
        Requisicao req;
        memset(&req, 0, sizeof(req));
        req.op = (rand() % 4) + 1;
        req.id = (rand() % 25) + 1;

        if (req.op == OP_INSERT || req.op == OP_UPDATE) {
            copiarNome(req.nome, nomes[rand() % num_nomes]);
        }
        if (req.op == OP_SELECT) {
            req.sel_tipo = SEL_POR_ID;
        }
        reqs[n++] = req;
    }

    printf("  Enviando lote de %d requisicoes...\n", n);
    for (int i = 0; i < n; i++) {
        if (!writeAll(hReq, &reqs[i], sizeof(Requisicao))) {
            printf("  ERRO ao enviar requisicao %d.\n", i);
            return;
        }
        printf("  [%2d/%d] enviada op=%d id=%d\n",
               i + 1, n, reqs[i].op, reqs[i].id);
    }

    printf("  Aguardando %d respostas...\n", n);
    for (int i = 0; i < n; i++) {
        Resposta r;
        if (!readAll(hResp, &r, sizeof(r))) {
            printf("  ERRO ao ler resposta %d.\n", i);
            return;
        }
        printf("  Resposta (origem op=%d id=%d): %s\n",
               r.req_op, r.req_id, r.msg);
        if (r.req_op == OP_SELECT && r.registros > 0) {
            for (int j = 0; j < r.registros; j++) {
                printf("    -> id=%d nome='%s'\n", r.regs[j].id, r.regs[j].nome);
            }
        }
    }
}

int main() {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    srand((unsigned)time(NULL));

    printf("=== Cliente gerenciador de banco ===\n");

    if (!WaitNamedPipe(PIPE_REQ, 5000)) {
        fprintf(stderr, "Servidor nao esta rodando. Inicie o servidor primeiro.\n");
        return 1;
    }

    HANDLE hReq = CreateFile(
        PIPE_REQ, GENERIC_WRITE, 0, NULL,
        OPEN_EXISTING, 0, NULL);
    if (hReq == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "Erro ao abrir pipe de requisicoes (%lu)\n", GetLastError());
        return 1;
    }

    if (!WaitNamedPipe(PIPE_RESP, 5000)) {
        fprintf(stderr, "Servidor nao esta rodando. Inicie o servidor primeiro.\n");
        CloseHandle(hReq);
        return 1;
    }
    HANDLE hResp = CreateFile(
        PIPE_RESP, GENERIC_READ, 0, NULL,
        OPEN_EXISTING, 0, NULL);
    if (hResp == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "Erro ao abrir pipe de respostas (%lu)\n", GetLastError());
        CloseHandle(hReq);
        return 1;
    }

    printf("Conectado ao servidor.\n\n");

    int op = -1;
    while (op != 0) {
        printf("=== MENU ===\n");
        printf(" 0 - Sair\n");
        printf(" 1 - SELECT por id\n");
        printf(" 2 - INSERT (novo registro: id + nome)\n");
        printf(" 3 - DELETE (remove por id)\n");
        printf(" 4 - UPDATE (altera nome por id)\n");
        printf(" 5 - SELECT por nome\n");
        printf(" 6 - SELECT todos\n");
        printf(" 7 - AUTO (%d requisicoes aleatorias)\n", 20);
        printf("Escolha: ");

        if (scanf("%d", &op) != 1) {
            limparEntrada();
            if (feof(stdin)) {
                op = 0;
                continue;
            }
            op = -1;
            continue;
        }

        Requisicao req;
        memset(&req, 0, sizeof(req));
        req.op = op;

        switch (op) {
            case OP_EXIT:
                break;
            case OP_INSERT: {
                printf("  id: ");
                scanf("%d", &req.id);
                limparEntrada();
                printf("  nome: ");
                lerLinha(req.nome, MAX_NOME);
                enviar(hReq, hResp, req);
                break;
            }
            case OP_DELETE: {
                printf("  id: ");
                scanf("%d", &req.id);
                enviar(hReq, hResp, req);
                break;
            }
            case OP_SELECT: {
                req.sel_tipo = SEL_POR_ID;
                printf("  id: ");
                scanf("%d", &req.id);
                enviar(hReq, hResp, req);
                break;
            }
            case OP_UPDATE: {
                printf("  id: ");
                scanf("%d", &req.id);
                limparEntrada();
                printf("  novo nome: ");
                lerLinha(req.nome, MAX_NOME);
                enviar(hReq, hResp, req);
                break;
            }
            case 5: {
                limparEntrada();
                req.sel_tipo = SEL_POR_NOME;
                printf("  nome: ");
                lerLinha(req.nome, MAX_NOME);
                req.op = OP_SELECT;
                enviar(hReq, hResp, req);
                break;
            }
            case 6: {
                req.sel_tipo = SEL_TODOS;
                req.op = OP_SELECT;
                enviar(hReq, hResp, req);
                break;
            }
            case 7:
                modoAutomatico(hReq, hResp, 20);
                break;
            default:
                printf("  Opcao invalida.\n");
                break;
        }
        printf("\n");
    }

    Requisicao saida;
    memset(&saida, 0, sizeof(saida));
    saida.op = OP_EXIT;
    writeAll(hReq, &saida, sizeof(saida));

    CloseHandle(hReq);
    CloseHandle(hResp);
    printf("Cliente encerrado.\n");
    return 0;
}