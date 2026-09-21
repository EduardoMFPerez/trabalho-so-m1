#include "common.h"

#include <windows.h>
#include <pthread.h>
#include <semaphore.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <queue>
#include <string>
#include <vector>

static std::vector<Registro> banco;
static pthread_mutex_t db_mutex = PTHREAD_MUTEX_INITIALIZER;

static std::queue<Requisicao> fila_jobs;
static pthread_mutex_t fila_mutex = PTHREAD_MUTEX_INITIALIZER;
static sem_t jobs_sem;

static pthread_mutex_t resp_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

static FILE* logfile = NULL;
static bool executando = true;
static HANDLE hResp = INVALID_HANDLE_VALUE;

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

static void registrarLog(const std::string& texto) {
    pthread_mutex_lock(&log_mutex);
    if (logfile) {
        time_t t = time(NULL);
        struct tm* tm = localtime(&t);
        fprintf(logfile, "[%02d/%02d/%04d %02d:%02d:%02d] %s\n",
                tm->tm_mday, tm->tm_mon + 1, tm->tm_year + 1900,
                tm->tm_hour, tm->tm_min, tm->tm_sec, texto.c_str());
        fflush(logfile);
    }
    pthread_mutex_unlock(&log_mutex);
}

static Resposta processar(const Requisicao& req) {
    Resposta r;
    memset(&r, 0, sizeof(r));
    r.status = STATUS_OK;
    r.req_op = req.op;
    r.req_id = req.id;
    copiarNome(r.req_nome, req.nome);

    pthread_mutex_lock(&db_mutex);

    switch (req.op) {
        case OP_INSERT: {
            bool duplicado = false;
            for (size_t i = 0; i < banco.size(); i++) {
                if (banco[i].id == req.id) { duplicado = true; break; }
            }
            if (duplicado) {
                r.status = STATUS_ERRO;
                snprintf(r.msg, sizeof(r.msg), "INSERT falhou: ja existe id=%d", req.id);
            } else {
                Registro reg;
                reg.id = req.id;
                copiarNome(reg.nome, req.nome);
                banco.push_back(reg);
                r.registros = 1;
                snprintf(r.msg, sizeof(r.msg), "INSERT ok: id=%d nome='%s'",
                         reg.id, reg.nome);
            }
            break;
        }
        case OP_DELETE: {
            int idx = -1;
            for (size_t i = 0; i < banco.size(); i++) {
                if (banco[i].id == req.id) { idx = static_cast<int>(i); break; }
            }
            if (idx < 0) {
                r.status = STATUS_NAO_ENCONTRADO;
                snprintf(r.msg, sizeof(r.msg), "DELETE: id=%d nao encontrado", req.id);
            } else {
                banco.erase(banco.begin() + idx);
                r.registros = 1;
                snprintf(r.msg, sizeof(r.msg), "DELETE ok: id=%d removido", req.id);
            }
            break;
        }
        case OP_UPDATE: {
            int idx = -1;
            for (size_t i = 0; i < banco.size(); i++) {
                if (banco[i].id == req.id) { idx = static_cast<int>(i); break; }
            }
            if (idx < 0) {
                r.status = STATUS_NAO_ENCONTRADO;
                snprintf(r.msg, sizeof(r.msg), "UPDATE: id=%d nao encontrado", req.id);
            } else {
                copiarNome(banco[idx].nome, req.nome);
                r.registros = 1;
                snprintf(r.msg, sizeof(r.msg), "UPDATE ok: id=%d novo nome='%s'",
                         req.id, banco[idx].nome);
            }
            break;
        }
        case OP_SELECT: {
            for (size_t i = 0; i < banco.size(); i++) {
                const Registro& reg = banco[i];
                bool encontrou = false;
                if (req.sel_tipo == SEL_POR_NOME) {
                    encontrou = (strcmp(reg.nome, req.nome) == 0);
                } else if (req.sel_tipo == SEL_TODOS) {
                    encontrou = true;
                } else {
                    encontrou = (reg.id == req.id);
                }
                if (encontrou && r.registros < MAX_RESULTADOS) {
                    r.regs[r.registros++] = reg;
                }
            }
            if (r.registros == 0) {
                r.status = STATUS_NAO_ENCONTRADO;
                snprintf(r.msg, sizeof(r.msg), "SELECT: nenhum registro encontrado");
            } else {
                snprintf(r.msg, sizeof(r.msg), "SELECT: %d registro(s) retornados",
                         r.registros);
            }
            break;
        }
        default:
            r.status = STATUS_ERRO;
            snprintf(r.msg, sizeof(r.msg), "operacao desconhecida");
            break;
    }

    pthread_mutex_unlock(&db_mutex);
    return r;
}

static void* worker(void* arg) {
    (void)arg;
    while (true) {
        sem_wait(&jobs_sem);
        if (!executando) break;

        pthread_mutex_lock(&fila_mutex);
        Requisicao req = fila_jobs.front();
        fila_jobs.pop();
        pthread_mutex_unlock(&fila_mutex);

        Resposta res = processar(req);

        pthread_mutex_lock(&resp_mutex);
        writeAll(hResp, &res, sizeof(res));
        pthread_mutex_unlock(&resp_mutex);

        unsigned long long tid = static_cast<unsigned long long>(pthread_self());
        char tmp[512];
        snprintf(tmp, sizeof(tmp), "[thread %llu] %s", tid, res.msg);
        registrarLog(tmp);
    }
    return NULL;
}

static void imprimirBanco() {
    pthread_mutex_lock(&db_mutex);
    printf("=== Tabela final (%u registros) ===\n",
           static_cast<unsigned>(banco.size()));
    for (size_t i = 0; i < banco.size(); i++) {
        printf("  id=%d nome='%s'\n", banco[i].id, banco[i].nome);
    }
    pthread_mutex_unlock(&db_mutex);
}

int main(int argc, char** argv) {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    int nThreads = 4;
    if (argc > 1) nThreads = atoi(argv[1]);
    if (nThreads < 1) nThreads = 1;
    if (nThreads > 16) nThreads = 16;

    printf("=== Servidor gerenciador de banco ===\n");
    printf("Pool de threads: %d\n", nThreads);

    sem_init(&jobs_sem, 0, 0);

    const char* nomes_seed[] = { "Ana", "Bruno", "Carla" };
    for (int i = 0; i < 3; i++) {
        Registro reg;
        reg.id = i + 1;
        copiarNome(reg.nome, nomes_seed[i]);
        banco.push_back(reg);
    }

    DWORD bufsz = sizeof(Resposta) + sizeof(Requisicao) + 512;
    HANDLE hReq = CreateNamedPipe(
        PIPE_REQ, PIPE_ACCESS_INBOUND,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
        1, bufsz, bufsz, 0, NULL);
    if (hReq == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "Erro ao criar pipe de requisicoes (%lu)\n", GetLastError());
        sem_destroy(&jobs_sem);
        return 1;
    }
    hResp = CreateNamedPipe(
        PIPE_RESP, PIPE_ACCESS_OUTBOUND,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
        1, bufsz, bufsz, 0, NULL);
    if (hResp == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "Erro ao criar pipe de respostas (%lu)\n", GetLastError());
        CloseHandle(hReq);
        sem_destroy(&jobs_sem);
        return 1;
    }

    printf("Aguardando cliente conectar...\n");

    if (!ConnectNamedPipe(hReq, NULL) &&
        GetLastError() != ERROR_PIPE_CONNECTED) {
        fprintf(stderr, "Falha ao conectar pipe de requisicoes (%lu)\n",
                GetLastError());
        return 1;
    }
    if (!ConnectNamedPipe(hResp, NULL) &&
        GetLastError() != ERROR_PIPE_CONNECTED) {
        fprintf(stderr, "Falha ao conectar pipe de respostas (%lu)\n",
                GetLastError());
        return 1;
    }
    printf("Cliente conectado. Processando requisicoes...\n");

    logfile = fopen("server_log.txt", "a");
    if (!logfile) {
        fprintf(stderr, "Nao foi possivel abrir server_log.txt\n");
    }

    pthread_t pool[16];
    for (int i = 0; i < nThreads; i++) {
        pthread_create(&pool[i], NULL, worker, NULL);
    }

    bool cliente_ativo = true;
    while (cliente_ativo) {
        Requisicao req;
        if (!readAll(hReq, &req, sizeof(req))) {
            printf("Cliente desconectou.\n");
            break;
        }
        if (req.op == OP_EXIT) {
            printf("Sinal de encerramento recebido.\n");
            break;
        }
        pthread_mutex_lock(&fila_mutex);
        fila_jobs.push(req);
        pthread_mutex_unlock(&fila_mutex);
        sem_post(&jobs_sem);
    }

    executando = false;
    for (int i = 0; i < nThreads; i++) sem_post(&jobs_sem);
    for (int i = 0; i < nThreads; i++) pthread_join(pool[i], NULL);

    if (logfile) fclose(logfile);
    DisconnectNamedPipe(hReq);
    DisconnectNamedPipe(hResp);
    CloseHandle(hReq);
    CloseHandle(hResp);

    imprimirBanco();

    pthread_mutex_destroy(&db_mutex);
    pthread_mutex_destroy(&fila_mutex);
    pthread_mutex_destroy(&resp_mutex);
    pthread_mutex_destroy(&log_mutex);
    sem_destroy(&jobs_sem);

    printf("Servidor encerrado.\n");
    return 0;
}