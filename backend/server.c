/*
 * MININ-CHAT HTTP SERVER v1.0
 * ===========================
 * Minimal single-threaded HTTP server in C.
 * - Serves static frontend (index.html)
 * - REST API for chat operations
 * - Calls Fortran for message encryption/decryption
 * - Calls COBOL for message formatting via fork/pipe
 *
 * Build: gcc -O2 -o server server.c encrypt.o -lgfortran -lm
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <time.h>
#include <ctype.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>

/* ============================================================
 * FORTRAN ENCRYPTION INTERFACE (from encrypt.f90)
 * ============================================================ */
extern void minin_encrypt(const char *in, char *out,
                          const int *len, const int *key);
extern void minin_decrypt(const char *in, char *out,
                          const int *len, const int *key);

/* ============================================================
 * CONFIGURATION
 * ============================================================ */
#define PORT        3000
#define BACKLOG     32
#define BUF_SZ      16384
#define MAX_MSG     500
#define MAX_USR     64
#define MSG_SZ      480
#define NK_SZ       24
#define RM_SZ       24
#define TK_SZ       16
#define CIPHER_KEY  0xCAFE
#define COBOL_BIN   "/app/chat"
#define HTML_FILE   "/app/static/index.html"
#define TIMEOUT_SEC 120
#define POLL_LIMIT  50

/* ============================================================
 * DATA STRUCTURES
 * ============================================================ */
typedef struct {
    int    id;
    int    type;       /* 0=msg, 1=system, 2=whisper */
    char   nick[NK_SZ];
    char   room[RM_SZ];
    char   text[MSG_SZ];   /* plaintext (from COBOL format) */
    char   enc[MSG_SZ];    /* encrypted by Fortran */
    char   target[NK_SZ];  /* for whispers */
    time_t ts;
} Msg;

typedef struct {
    char   nick[NK_SZ];
    char   room[RM_SZ];
    char   token[TK_SZ + 1];
    time_t last_seen;
    int    active;
} Usr;

/* ============================================================
 * GLOBAL STATE
 * ============================================================ */
static Msg  g_msgs[MAX_MSG];
static Usr  g_usrs[MAX_USR];
static int  g_mcnt = 0;
static int  g_ucnt = 0;
static int  g_next_id = 1;
static char g_html[262144];  /* 256KB buffer for HTML */
static int  g_html_len = 0;

/* ============================================================
 * UTILITY FUNCTIONS
 * ============================================================ */

/* URL-decode in place */
static void url_decode(char *dst, const char *src) {
    for (; *src; src++, dst++) {
        if (*src == '+') {
            *dst = ' ';
        } else if (*src == '%' && isxdigit(src[1]) && isxdigit(src[2])) {
            unsigned int h;
            sscanf(src + 1, "%2x", &h);
            *dst = (char)h;
            src += 2;
        } else {
            *dst = *src;
        }
    }
    *dst = '\0';
}

/* Extract URL-encoded parameter value */
static int get_param(const char *qs, const char *key, char *val, int sz) {
    char needle[64];
    snprintf(needle, sizeof(needle), "%s=", key);
    const char *p = qs;

    while ((p = strstr(p, needle)) != NULL) {
        if (p == qs || *(p - 1) == '&') break;
        p++;
    }
    if (!p) { val[0] = '\0'; return 0; }

    p += strlen(needle);
    int i = 0;
    while (p[i] && p[i] != '&' && i < sz - 1) {
        val[i] = p[i];
        i++;
    }
    val[i] = '\0';

    char dec[1024];
    url_decode(dec, val);
    strncpy(val, dec, sz - 1);
    val[sz - 1] = '\0';
    return 1;
}

/* Generate random hex token */
static void gen_token(char *tok) {
    static const char hex[] = "0123456789abcdef";
    for (int i = 0; i < TK_SZ; i++)
        tok[i] = hex[rand() % 16];
    tok[TK_SZ] = '\0';
}

/* Find user by token */
static Usr *find_by_token(const char *tok) {
    for (int i = 0; i < g_ucnt; i++)
        if (g_usrs[i].active && strcmp(g_usrs[i].token, tok) == 0) {
            g_usrs[i].last_seen = time(NULL);
            return &g_usrs[i];
        }
    return NULL;
}

/* Find user by nick */
static Usr *find_by_nick(const char *nick) {
    for (int i = 0; i < g_ucnt; i++)
        if (g_usrs[i].active && strcasecmp(g_usrs[i].nick, nick) == 0)
            return &g_usrs[i];
    return NULL;
}

/* JSON-escape a string */
static void json_escape(char *dst, const char *src, int sz) {
    int j = 0;
    for (int i = 0; src[i] && j < sz - 6; i++) {
        unsigned char c = (unsigned char)src[i];
        if (c == '"')       { dst[j++] = '\\'; dst[j++] = '"'; }
        else if (c == '\\') { dst[j++] = '\\'; dst[j++] = '\\'; }
        else if (c == '\n') { dst[j++] = '\\'; dst[j++] = 'n'; }
        else if (c == '\r') { /* skip */ }
        else if (c == '\t') { dst[j++] = '\\'; dst[j++] = 't'; }
        else if (c >= 32 && c != 127) { dst[j++] = c; }
        else { /* skip non-printable */ }
    }
    dst[j] = '\0';
}

/* ============================================================
 * COBOL INTERFACE (via fork/pipe — no shell injection)
 * ============================================================ */
static void cobol_call(const char *input, char *output, int outsz) {
    int pipe_in[2], pipe_out[2];

    if (pipe(pipe_in) < 0 || pipe(pipe_out) < 0) {
        strncpy(output, "ERR|PIPE_FAIL", outsz);
        return;
    }

    pid_t pid = fork();
    if (pid < 0) {
        strncpy(output, "ERR|FORK_FAIL", outsz);
        close(pipe_in[0]); close(pipe_in[1]);
        close(pipe_out[0]); close(pipe_out[1]);
        return;
    }

    if (pid == 0) {
        /* Child: COBOL process */
        close(pipe_in[1]);
        close(pipe_out[0]);
        dup2(pipe_in[0], STDIN_FILENO);
        dup2(pipe_out[1], STDOUT_FILENO);
        close(pipe_in[0]);
        close(pipe_out[1]);
        execl(COBOL_BIN, "chat", (char *)NULL);
        _exit(127);
    }

    /* Parent */
    close(pipe_in[0]);
    close(pipe_out[1]);

    /* Write input to COBOL stdin */
    write(pipe_in[1], input, strlen(input));
    write(pipe_in[1], "\n", 1);
    close(pipe_in[1]);

    /* Read output from COBOL stdout */
    int n = read(pipe_out[0], output, outsz - 1);
    if (n > 0) {
        output[n] = '\0';
        /* Trim trailing whitespace/newlines */
        while (n > 0 && (output[n-1] == '\n' || output[n-1] == '\r'
                      || output[n-1] == ' '))
            output[--n] = '\0';
    } else {
        output[0] = '\0';
    }
    close(pipe_out[0]);

    int status;
    waitpid(pid, &status, 0);
}

/* ============================================================
 * MESSAGE STORAGE
 * ============================================================ */
static int add_message(const char *nick, const char *room,
                       const char *text, int type, const char *target)
{
    /* Shift if full */
    if (g_mcnt >= MAX_MSG) {
        int shift = MAX_MSG / 4;
        memmove(g_msgs, g_msgs + shift, sizeof(Msg) * (MAX_MSG - shift));
        g_mcnt -= shift;
    }

    Msg *m = &g_msgs[g_mcnt++];
    memset(m, 0, sizeof(Msg));
    m->id = g_next_id++;
    m->type = type;
    m->ts = time(NULL);
    strncpy(m->nick, nick, NK_SZ - 1);
    strncpy(m->room, room, RM_SZ - 1);
    strncpy(m->text, text, MSG_SZ - 1);
    if (target) strncpy(m->target, target, NK_SZ - 1);

    /* Encrypt the message text with Fortran */
    int len = (int)strlen(text);
    int key = CIPHER_KEY;
    if (len > 0 && len < MSG_SZ) {
        minin_encrypt(text, m->enc, &len, &key);
        m->enc[len] = '\0';
    }

    return m->id;
}

/* ============================================================
 * HTTP RESPONSE HELPERS
 * ============================================================ */
static void send_response(int fd, int code, const char *content_type,
                          const char *body, int body_len)
{
    const char *reason;
    switch (code) {
        case 200: reason = "OK"; break;
        case 400: reason = "Bad Request"; break;
        case 404: reason = "Not Found"; break;
        default:  reason = "Error"; break;
    }

    char header[512];
    int hlen = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "Cache-Control: no-cache\r\n"
        "\r\n",
        code, reason, content_type, body_len);

    write(fd, header, hlen);
    if (body_len > 0) write(fd, body, body_len);
}

static void send_json(int fd, const char *json) {
    send_response(fd, 200, "application/json; charset=utf-8",
                  json, (int)strlen(json));
}

static void send_html(int fd) {
    send_response(fd, 200, "text/html; charset=utf-8",
                  g_html, g_html_len);
}

static void send_404(int fd) {
    send_response(fd, 404, "text/plain", "404 Not Found", 13);
}

/* ============================================================
 * LOAD STATIC HTML
 * ============================================================ */
static void load_html(void) {
    FILE *f = fopen(HTML_FILE, "r");
    if (f) {
        g_html_len = (int)fread(g_html, 1, sizeof(g_html) - 1, f);
        g_html[g_html_len] = '\0';
        fclose(f);
        printf("[INIT] Loaded %s (%d bytes)\n", HTML_FILE, g_html_len);
    } else {
        g_html_len = snprintf(g_html, sizeof(g_html),
            "<html><body style='background:#000;color:#0f0;font-family:monospace'>"
            "<h1>MININ-CHAT</h1>"
            "<p>Frontend not found at %s</p></body></html>",
            HTML_FILE);
        printf("[WARN] HTML file not found: %s\n", HTML_FILE);
    }
}

/* ============================================================
 * API: POST /api/login   body: n=NICKNAME
 * ============================================================ */
static void handle_login(int fd, const char *body) {
    char nick[NK_SZ] = {0};
    get_param(body, "n", nick, NK_SZ);

    if (!nick[0]) {
        send_json(fd, "{\"ok\":0,\"e\":\"nickname required\"}");
        return;
    }

    /* Check duplicate */
    if (find_by_nick(nick)) {
        send_json(fd, "{\"ok\":0,\"e\":\"nick taken\"}");
        return;
    }

    /* Find free slot */
    int idx = -1;
    for (int i = 0; i < g_ucnt; i++)
        if (!g_usrs[i].active) { idx = i; break; }
    if (idx < 0) {
        if (g_ucnt >= MAX_USR) {
            send_json(fd, "{\"ok\":0,\"e\":\"server full\"}");
            return;
        }
        idx = g_ucnt++;
    }

    Usr *u = &g_usrs[idx];
    memset(u, 0, sizeof(Usr));
    strncpy(u->nick, nick, NK_SZ - 1);
    strncpy(u->room, "general", RM_SZ - 1);
    gen_token(u->token);
    u->last_seen = time(NULL);
    u->active = 1;

    /* Get MOTD from COBOL */
    char motd_raw[1024] = {0};
    cobol_call("MOTD", motd_raw, sizeof(motd_raw));
    char *motd = motd_raw;
    if (strncmp(motd, "OK|", 3) == 0) motd += 3;

    /* System message */
    char sysmsg[128];
    snprintf(sysmsg, sizeof(sysmsg), "%s joined #general", nick);
    add_message("SYSTEM", "general", sysmsg, 1, NULL);

    /* Respond */
    char json[2048], esc_motd[1024];
    json_escape(esc_motd, motd, sizeof(esc_motd));
    snprintf(json, sizeof(json),
        "{\"ok\":1,\"t\":\"%s\",\"motd\":\"%s\",\"room\":\"general\"}",
        u->token, esc_motd);
    send_json(fd, json);

    printf("[JOIN] %s (token=%s)\n", nick, u->token);
}

/* ============================================================
 * API: POST /api/send   body: t=TOKEN&m=MESSAGE
 * ============================================================ */
static void handle_send(int fd, const char *body) {
    char tok[TK_SZ + 1] = {0}, msg[MSG_SZ] = {0};
    get_param(body, "t", tok, TK_SZ + 1);
    get_param(body, "m", msg, MSG_SZ);

    Usr *u = find_by_token(tok);
    if (!u) {
        send_json(fd, "{\"ok\":0,\"e\":\"not authenticated\"}");
        return;
    }

    if (!msg[0]) {
        send_json(fd, "{\"ok\":0,\"e\":\"empty message\"}");
        return;
    }

    /* Handle whisper: /w target message */
    if (strncmp(msg, "/w ", 3) == 0) {
        char *space = strchr(msg + 3, ' ');
        if (space && space > msg + 3) {
            char target[NK_SZ] = {0};
            int tnl = (int)(space - (msg + 3));
            if (tnl >= NK_SZ) tnl = NK_SZ - 1;
            strncpy(target, msg + 3, tnl);

            /* Check target exists */
            if (!find_by_nick(target)) {
                send_json(fd, "{\"ok\":0,\"e\":\"user not found\"}");
                return;
            }

            char whisper_text[MSG_SZ];
            snprintf(whisper_text, sizeof(whisper_text),
                "[whisper] <%s> %s", u->nick, space + 1);
            add_message(u->nick, u->room, whisper_text, 2, target);
            send_json(fd, "{\"ok\":1}");
            return;
        }
    }

    /* Format via COBOL */
    char cobol_in[1024], cobol_out[1024];
    snprintf(cobol_in, sizeof(cobol_in), "FORMAT|%s|%s|%s",
             u->nick, msg, u->room);
    cobol_call(cobol_in, cobol_out, sizeof(cobol_out));

    char *formatted = cobol_out;
    if (strncmp(formatted, "OK|", 3) == 0)
        formatted += 3;
    else
        formatted = msg;  /* fallback to raw message */

    add_message(u->nick, u->room, formatted, 0, NULL);
    send_json(fd, "{\"ok\":1}");
}

/* ============================================================
 * API: GET /api/poll?t=TOKEN&a=AFTER_ID
 * ============================================================ */
static void handle_poll(int fd, const char *qs) {
    char tok[TK_SZ + 1] = {0}, after_s[16] = {0};
    get_param(qs, "t", tok, TK_SZ + 1);
    get_param(qs, "a", after_s, 16);
    int after = atoi(after_s);

    Usr *u = find_by_token(tok);
    if (!u) {
        send_json(fd, "{\"ok\":0}");
        return;
    }

    char json[65536];
    int pos = 0;
    pos += snprintf(json + pos, sizeof(json) - pos, "{\"ok\":1,\"msgs\":[");

    int count = 0;
    for (int i = 0; i < g_mcnt && count < POLL_LIMIT; i++) {
        Msg *m = &g_msgs[i];
        if (m->id <= after) continue;

        /* Filter: same room, or system in same room, or whisper to/from user */
        if (m->type == 2) {
            /* Whisper: visible only to sender and target */
            if (strcmp(m->nick, u->nick) != 0 &&
                strcmp(m->target, u->nick) != 0)
                continue;
        } else {
            if (strcmp(m->room, u->room) != 0)
                continue;
        }

        /* Decrypt from Fortran-encrypted storage for verification */
        char decrypted[MSG_SZ] = {0};
        int elen = (int)strlen(m->enc);
        int key = CIPHER_KEY;
        if (elen > 0) {
            minin_decrypt(m->enc, decrypted, &elen, &key);
            decrypted[elen] = '\0';
        }

        char esc_text[1024], esc_nick[64];
        json_escape(esc_text, m->text, sizeof(esc_text));
        json_escape(esc_nick, m->nick, sizeof(esc_nick));

        struct tm *tm = localtime(&m->ts);
        char timestr[16];
        strftime(timestr, sizeof(timestr), "%H:%M:%S", tm);

        if (count > 0) pos += snprintf(json + pos, sizeof(json) - pos, ",");
        pos += snprintf(json + pos, sizeof(json) - pos,
            "{\"i\":%d,\"n\":\"%s\",\"d\":\"%s\",\"ts\":\"%s\",\"y\":%d}",
            m->id, esc_nick, esc_text, timestr, m->type);
        count++;
    }

    pos += snprintf(json + pos, sizeof(json) - pos, "]}");
    send_json(fd, json);
}

/* ============================================================
 * API: POST /api/cmd   body: t=TOKEN&c=COMMAND
 * ============================================================ */
static void handle_cmd(int fd, const char *body) {
    char tok[TK_SZ + 1] = {0}, cmd[256] = {0};
    get_param(body, "t", tok, TK_SZ + 1);
    get_param(body, "c", cmd, 256);

    Usr *u = find_by_token(tok);
    if (!u) {
        send_json(fd, "{\"ok\":0,\"e\":\"not authenticated\"}");
        return;
    }

    char json[4096];

    /* /nick NEW_NAME */
    if (strncmp(cmd, "nick ", 5) == 0) {
        char *nn = cmd + 5;
        if (find_by_nick(nn)) {
            snprintf(json, sizeof(json),
                "{\"ok\":0,\"e\":\"nick '%s' already taken\"}", nn);
        } else {
            char sysmsg[128];
            snprintf(sysmsg, sizeof(sysmsg),
                "%s is now known as %s", u->nick, nn);
            add_message("SYSTEM", u->room, sysmsg, 1, NULL);
            strncpy(u->nick, nn, NK_SZ - 1);
            snprintf(json, sizeof(json), "{\"ok\":1}");
        }
    }
    /* /join ROOM */
    else if (strncmp(cmd, "join ", 5) == 0) {
        char *nr = cmd + 5;
        char sysmsg[128];
        snprintf(sysmsg, sizeof(sysmsg), "%s left #%s", u->nick, u->room);
        add_message("SYSTEM", u->room, sysmsg, 1, NULL);

        strncpy(u->room, nr, RM_SZ - 1);

        snprintf(sysmsg, sizeof(sysmsg), "%s joined #%s", u->nick, u->room);
        add_message("SYSTEM", u->room, sysmsg, 1, NULL);
        snprintf(json, sizeof(json), "{\"ok\":1}");
    }
    /* /users */
    else if (strcmp(cmd, "users") == 0) {
        int pos = snprintf(json, sizeof(json),
            "{\"ok\":1,\"d\":\"== Users in #%s == ", u->room);
        for (int i = 0; i < g_ucnt; i++) {
            if (g_usrs[i].active &&
                strcmp(g_usrs[i].room, u->room) == 0) {
                pos += snprintf(json + pos, sizeof(json) - pos,
                    "%s ", g_usrs[i].nick);
            }
        }
        pos += snprintf(json + pos, sizeof(json) - pos, "\"}");
    }
    /* /rooms */
    else if (strcmp(cmd, "rooms") == 0) {
        char rooms[32][RM_SZ];
        int counts[32] = {0};
        int rc = 0;

        for (int i = 0; i < g_ucnt; i++) {
            if (!g_usrs[i].active) continue;
            int found = -1;
            for (int j = 0; j < rc; j++)
                if (strcmp(rooms[j], g_usrs[i].room) == 0) { found = j; break; }
            if (found >= 0) {
                counts[found]++;
            } else if (rc < 32) {
                strncpy(rooms[rc], g_usrs[i].room, RM_SZ - 1);
                counts[rc] = 1;
                rc++;
            }
        }

        int pos = snprintf(json, sizeof(json),
            "{\"ok\":1,\"d\":\"== Active Rooms == ");
        for (int i = 0; i < rc; i++)
            pos += snprintf(json + pos, sizeof(json) - pos,
                "#%s(%d) ", rooms[i], counts[i]);
        pos += snprintf(json + pos, sizeof(json) - pos, "\"}");
    }
    /* /status */
    else if (strcmp(cmd, "status") == 0) {
        /* Also query COBOL */
        char cobol_out[512] = {0};
        cobol_call("STATUS", cobol_out, sizeof(cobol_out));
        char *cs = cobol_out;
        if (strncmp(cs, "OK|", 3) == 0) cs += 3;

        char esc_cs[512];
        json_escape(esc_cs, cs, sizeof(esc_cs));

        int online = 0;
        for (int i = 0; i < g_ucnt; i++)
            if (g_usrs[i].active) online++;

        snprintf(json, sizeof(json),
            "{\"ok\":1,\"d\":\"== SERVER STATUS == "
            "Online: %d | Messages: %d | "
            "Encryption: Fortran XOR-PRNG (key=0x%X) | "
            "Formatter: %s\"}",
            online, g_mcnt, CIPHER_KEY, esc_cs);
    }
    else {
        snprintf(json, sizeof(json),
            "{\"ok\":0,\"e\":\"unknown command\"}");
    }

    send_json(fd, json);
}

/* ============================================================
 * HTTP REQUEST HANDLER
 * ============================================================ */
static void handle_request(int fd) {
    char buf[BUF_SZ] = {0};

    /* Read the request with a short timeout */
    struct timeval tv = {5, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    int total = 0;
    int n = read(fd, buf, BUF_SZ - 1);
    if (n <= 0) return;
    total = n;

    /* For POST, ensure we read the full body */
    char *cl_hdr = strcasestr(buf, "Content-Length:");
    if (cl_hdr) {
        int cl = atoi(cl_hdr + 15);
        char *body_start = strstr(buf, "\r\n\r\n");
        if (body_start) {
            body_start += 4;
            int body_read = total - (int)(body_start - buf);
            while (body_read < cl && total < BUF_SZ - 1) {
                n = read(fd, buf + total, BUF_SZ - 1 - total);
                if (n <= 0) break;
                total += n;
                body_read += n;
            }
            buf[total] = '\0';
        }
    }

    /* Parse request line */
    char method[8] = {0}, path[512] = {0};
    sscanf(buf, "%7s %511s", method, path);

    /* Find body */
    char *body = strstr(buf, "\r\n\r\n");
    body = body ? body + 4 : "";

    /* Separate path and query string */
    char *qs = strchr(path, '?');
    if (qs) { *qs = '\0'; qs++; }
    else qs = "";

    /* Route request */
    if (strcmp(method, "GET") == 0) {
        if (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0) {
            send_html(fd);
        } else if (strcmp(path, "/api/poll") == 0) {
            handle_poll(fd, qs);
        } else if (strcmp(path, "/favicon.ico") == 0) {
            send_response(fd, 204, "text/plain", "", 0);
        } else {
            send_404(fd);
        }
    } else if (strcmp(method, "POST") == 0) {
        if (strcmp(path, "/api/login") == 0) {
            handle_login(fd, body);
        } else if (strcmp(path, "/api/send") == 0) {
            handle_send(fd, body);
        } else if (strcmp(path, "/api/cmd") == 0) {
            handle_cmd(fd, body);
        } else {
            send_404(fd);
        }
    } else if (strcmp(method, "OPTIONS") == 0) {
        /* CORS preflight */
        char hdr[256];
        int hl = snprintf(hdr, sizeof(hdr),
            "HTTP/1.1 204 No Content\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Access-Control-Allow-Methods: GET,POST\r\n"
            "Access-Control-Allow-Headers: Content-Type\r\n"
            "Content-Length: 0\r\n\r\n");
        write(fd, hdr, hl);
    }
}

/* ============================================================
 * CLEANUP TIMED-OUT USERS
 * ============================================================ */
static void cleanup_users(void) {
    time_t now = time(NULL);
    for (int i = 0; i < g_ucnt; i++) {
        if (g_usrs[i].active &&
            (now - g_usrs[i].last_seen) > TIMEOUT_SEC) {
            char sysmsg[128];
            snprintf(sysmsg, sizeof(sysmsg),
                "%s timed out", g_usrs[i].nick);
            add_message("SYSTEM", g_usrs[i].room, sysmsg, 1, NULL);
            g_usrs[i].active = 0;
            printf("[TIMEOUT] %s\n", g_usrs[i].nick);
        }
    }
}

/* ============================================================
 * MAIN
 * ============================================================ */
int main(void) {
    signal(SIGPIPE, SIG_IGN);
    signal(SIGCHLD, SIG_DFL);
    srand((unsigned)time(NULL) ^ (unsigned)getpid());

    printf("╔═══════════════════════════════════════╗\n");
    printf("║     MININ-CHAT SERVER v1.0            ║\n");
    printf("║     COBOL + FORTRAN + C               ║\n");
    printf("║     Port: %d                         ║\n", PORT);
    printf("╚═══════════════════════════════════════╝\n");

    load_html();

    /* Test COBOL */
    char test_out[256] = {0};
    cobol_call("MOTD", test_out, sizeof(test_out));
    printf("[INIT] COBOL test: %s\n", test_out[0] ? "OK" : "UNAVAILABLE");

    /* Test Fortran encryption */
    {
        const char *test = "Hello MININ-CHAT!";
        char encrypted[64] = {0}, decrypted[64] = {0};
        int len = (int)strlen(test), key = CIPHER_KEY;
        minin_encrypt(test, encrypted, &len, &key);
        minin_decrypt(encrypted, decrypted, &len, &key);
        decrypted[len] = '\0';
        printf("[INIT] Fortran crypto test: %s\n",
            strcmp(test, decrypted) == 0 ? "OK" : "FAIL");
    }

    /* Create server socket */
    int srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) { perror("socket"); return 1; }

    int opt = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return 1;
    }
    if (listen(srv, BACKLOG) < 0) {
        perror("listen");
        return 1;
    }

    printf("[INIT] Listening on 0.0.0.0:%d\n", PORT);
    printf("[INIT] Ready for connections.\n\n");

    time_t last_clean = time(NULL);

    /* Main accept loop with select for periodic cleanup */
    while (1) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(srv, &fds);
        struct timeval tv = {15, 0};

        int r = select(srv + 1, &fds, NULL, NULL, &tv);
        if (r > 0 && FD_ISSET(srv, &fds)) {
            struct sockaddr_in cli_addr;
            socklen_t cli_len = sizeof(cli_addr);
            int cli = accept(srv, (struct sockaddr *)&cli_addr, &cli_len);
            if (cli >= 0) {
                handle_request(cli);
                close(cli);
            }
        }

        /* Periodic cleanup every 30 seconds */
        time_t now = time(NULL);
        if (now - last_clean > 30) {
            cleanup_users();
            last_clean = now;
        }
    }

    close(srv);
    return 0;
}
