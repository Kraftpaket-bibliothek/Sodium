#include "API.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <time.h>
#include <signal.h>
#include <errno.h>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef int socklen_t;
#define close closesocket
#else
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/wait.h>
#endif

// ============================================================================
// SECTION 1: GLOBAL STATE & SIGNALS
// ============================================================================

static volatile bool g_running = true;
static int g_server_fd = -1;
static time_t g_start_time = 0;

static void handle_signal(int sig) {
    (void)sig;
    g_running = false;
    if (g_server_fd >= 0) {
#if defined(_WIN32)
        closesocket(g_server_fd);
#else
        close(g_server_fd);
#endif
        g_server_fd = -1;
    }
}

static double get_time_ms_precise(void) {
#if defined(_WIN32)
    LARGE_INTEGER freq, count;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&count);
    return ((double)count.QuadPart * 1000.0) / (double)freq.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ((double)ts.tv_sec * 1000.0) + ((double)ts.tv_nsec / 1000000.0);
#endif
}

// ============================================================================
// SECTION 2: EMBEDDED SINGLE-PAGE WEB APPLICATION (HTML / CSS / JS)
// ============================================================================

static const char* const EMBEDDED_INDEX_HTML =
"<!DOCTYPE html>\n"
"<html lang=\"en\">\n"
"<head>\n"
"  <meta charset=\"UTF-8\" />\n"
"  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\" />\n"
"  <title>Sodium - Automated Reasoning Studio</title>\n"
"  <style>\n"
"    :root {\n"
"      --bg-dark: #12101B;\n"
"      --card-bg: #1C1929;\n"
"      --card-border: #2E2942;\n"
"      --purple-main: #8A3FFC;\n"
"      --purple-light: #A56EFF;\n"
"      --purple-dark: #58169C;\n"
"      --text-white: #FFFFFF;\n"
"      --text-muted: #A29EAF;\n"
"      --sat-green: #25A244;\n"
"      --unsat-red: #DA1E28;\n"
"      --code-bg: #0C0A14;\n"
"    }\n"
"    * { box-sizing: border-box; margin: 0; padding: 0; font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; }\n"
"    body { background-color: var(--bg-dark); color: var(--text-white); padding: 24px; min-height: 100vh; }\n"
"    .container { max-width: 1200px; margin: 0 auto; }\n"
"    header { display: flex; align-items: center; justify-content: space-between; margin-bottom: 24px; padding-bottom: 16px; border-bottom: 1px solid var(--card-border); }\n"
"    .brand { display: flex; align-items: center; gap: 12px; }\n"
"    .logo-badge { background: linear-gradient(135deg, var(--purple-main), var(--purple-dark)); width: 40px; height: 40px; border-radius: 8px; display: flex; align-items: center; justify-content: center; font-weight: bold; font-size: 20px; box-shadow: 0 4px 12px rgba(138, 63, 252, 0.4); }\n"
"    h1 { font-size: 22px; font-weight: 600; }\n"
"    .subtitle { font-size: 13px; color: var(--text-muted); }\n"
"    .badge-status { padding: 6px 14px; border-radius: 20px; font-size: 13px; font-weight: 600; text-transform: uppercase; letter-spacing: 0.5px; }\n"
"    .badge-ready { background: #242038; color: var(--purple-light); border: 1px solid var(--purple-dark); }\n"
"    .badge-sat { background: rgba(37, 162, 68, 0.2); color: #42BE65; border: 1px solid var(--sat-green); }\n"
"    .badge-unsat { background: rgba(218, 30, 40, 0.2); color: #FA4D56; border: 1px solid var(--unsat-red); }\n"
"    .grid { display: grid; grid-template-columns: 1fr 1fr; gap: 24px; }\n"
"    @media(max-width: 900px) { .grid { grid-template-columns: 1fr; } }\n"
"    .card { background: var(--card-bg); border: 1px solid var(--card-border); border-radius: 12px; padding: 20px; display: flex; flex-direction: column; gap: 16px; }\n"
"    .tabs { display: flex; gap: 8px; border-bottom: 1px solid var(--card-border); padding-bottom: 12px; }\n"
"    .tab-btn { background: transparent; border: none; color: var(--text-muted); font-size: 14px; font-weight: 600; padding: 6px 14px; border-radius: 6px; cursor: pointer; transition: 0.2s; }\n"
"    .tab-btn.active { background: var(--purple-main); color: #FFF; }\n"
"    .editor-wrap { display: flex; flex-direction: column; gap: 8px; flex: 1; }\n"
"    textarea { width: 100%; height: 260px; background: var(--code-bg); border: 1px solid var(--card-border); border-radius: 8px; color: #E0E0E0; font-family: 'SF Mono', Menlo, Consolas, monospace; font-size: 13px; padding: 12px; resize: vertical; outline: none; }\n"
"    textarea:focus { border-color: var(--purple-main); box-shadow: 0 0 0 2px rgba(138, 63, 252, 0.2); }\n"
"    .presets { display: flex; align-items: center; gap: 8px; font-size: 13px; color: var(--text-muted); }\n"
"    select { background: var(--code-bg); color: #FFF; border: 1px solid var(--card-border); padding: 6px 10px; border-radius: 6px; outline: none; }\n"
"    .btn-solve { background: linear-gradient(135deg, var(--purple-main), var(--purple-dark)); color: #FFF; border: none; border-radius: 8px; padding: 12px 20px; font-size: 15px; font-weight: 600; cursor: pointer; display: flex; align-items: center; justify-content: center; gap: 8px; box-shadow: 0 4px 16px rgba(138, 63, 252, 0.3); transition: 0.2s; }\n"
"    .btn-solve:hover { opacity: 0.95; transform: translateY(-1px); }\n"
"    .metrics-bar { display: grid; grid-template-columns: repeat(4, 1fr); gap: 12px; }\n"
"    .metric-card { background: var(--code-bg); border: 1px solid var(--card-border); border-radius: 8px; padding: 10px; text-align: center; }\n"
"    .metric-value { font-size: 18px; font-weight: 700; color: #FFF; }\n"
"    .metric-label { font-size: 11px; color: var(--text-muted); text-transform: uppercase; margin-top: 4px; }\n"
"    .results-panel { display: flex; flex-direction: column; gap: 12px; max-height: 380px; overflow-y: auto; }\n"
"    table { width: 100%; border-collapse: collapse; font-size: 13px; text-align: left; }\n"
"    th { background: var(--code-bg); padding: 10px; color: var(--text-muted); font-weight: 600; position: sticky; top: 0; }\n"
"    td { padding: 10px; border-bottom: 1px solid var(--card-border); }\n"
"    .pill-true { background: rgba(37, 162, 68, 0.2); color: #42BE65; padding: 2px 8px; border-radius: 4px; font-weight: 600; font-size: 11px; }\n"
"    .pill-false { background: rgba(218, 30, 40, 0.2); color: #FA4D56; padding: 2px 8px; border-radius: 4px; font-weight: 600; font-size: 11px; }\n"
"  </style>\n"
"</head>\n"
"<body>\n"
"  <div class=\"container\">\n"
"    <header>\n"
"      <div class=\"brand\">\n"
"        <div class=\"logo-badge\">Na</div>\n"
"        <div>\n"
"          <h1>Sodium Automated Reasoning Studio</h1>\n"
"          <div class=\"subtitle\">Neuro-Symbolic CDCL Automated Verification Engine</div>\n"
"        </div>\n"
"      </div>\n"
"      <div id=\"global-badge\" class=\"badge-status badge-ready\">READY</div>\n"
"    </header>\n"
"    <div class=\"grid\">\n"
"      <div class=\"card\">\n"
"        <div class=\"tabs\">\n"
"          <button class=\"tab-btn active\" onclick=\"setMode('dimacs')\">DIMACS CNF</button>\n"
"          <button class=\"tab-btn\" onclick=\"setMode('json')\">Mode 1 JSON</button>\n"
"          <button class=\"tab-btn\" onclick=\"setMode('nlp')\">Natural Language (NLP)</button>\n"
"        </div>\n"
"        <div class=\"presets\">\n"
"          <label>Presets:</label>\n"
"          <select id=\"preset-select\" onchange=\"loadPreset()\">\n"
"            <option value=\"nqueens4\">N-Queens (N=4) [SAT]</option>\n"
"            <option value=\"php32\">Pigeonhole (3 items, 2 holes) [UNSAT]</option>\n"
"            <option value=\"mutex\">Mutex Exclusion [SAT]</option>\n"
"          </select>\n"
"        </div>\n"
"        <div class=\"editor-wrap\">\n"
"          <textarea id=\"code-editor\" spellcheck=\"false\"></textarea>\n"
"        </div>\n"
"        <button class=\"btn-solve\" onclick=\"triggerSolve()\">Solve with Sodium CDCL</button>\n"
"      </div>\n"
"      <div class=\"card\">\n"
"        <div class=\"metrics-bar\">\n"
"          <div class=\"metric-card\"><div class=\"metric-value\" id=\"m-time\">0.00 ms</div><div class=\"metric-label\">Latency</div></div>\n"
"          <div class=\"metric-card\"><div class=\"metric-value\" id=\"m-vars\">0</div><div class=\"metric-label\">Variables</div></div>\n"
"          <div class=\"metric-card\"><div class=\"metric-value\" id=\"m-clauses\">0</div><div class=\"metric-label\">Clauses</div></div>\n"
"          <div class=\"metric-card\"><div class=\"metric-value\" id=\"m-code\">-</div><div class=\"metric-label\">Status Code</div></div>\n"
"        </div>\n"
"        <div class=\"results-panel\">\n"
"          <table id=\"results-table\">\n"
"            <thead><tr><th>Variable</th><th>Proposition Name</th><th>Truth Assignment</th></tr></thead>\n"
"            <tbody id=\"results-body\"><tr><td colspan=\"3\" style=\"text-align:center;color:var(--text-muted);padding:24px;\">Submit a problem to view CDCL assignments.</td></tr></tbody>\n"
"          </table>\n"
"        </div>\n"
"      </div>\n"
"    </div>\n"
"  </div>\n"
"  <script>\n"
"    let currentMode = 'dimacs';\n"
"    const PRESETS = {\n"
"      nqueens4: \"c N-Queens 4x4 instance\\np cnf 16 84\\n1 2 3 4 0\\n5 6 7 8 0\\n9 10 11 12 0\\n13 14 15 16 0\\n-1 -2 0\\n-1 -3 0\\n-1 -4 0\\n-2 -3 0\\n-2 -4 0\\n-3 -4 0\\n-5 -6 0\\n-5 -7 0\\n-5 -8 0\\n-6 -7 0\\n-6 -8 0\\n-7 -8 0\\n-9 -10 0\\n-9 -11 0\\n-9 -12 0\\n-10 -11 0\\n-10 -12 0\\n-11 -12 0\\n-13 -14 0\\n-13 -15 0\\n-13 -16 0\\n-14 -15 0\\n-14 -16 0\\n-15 -16 0\\n-1 -5 0\\n-1 -9 0\\n-1 -13 0\\n-5 -9 0\\n-5 -13 0\\n-9 -13 0\\n-2 -6 0\\n-2 -10 0\\n-2 -14 0\\n-6 -10 0\\n-6 -14 0\\n-10 -14 0\\n-3 -7 0\\n-3 -11 0\\n-3 -15 0\\n-7 -11 0\\n-7 -15 0\\n-11 -15 0\\n-4 -8 0\\n-4 -12 0\\n-4 -16 0\\n-8 -12 0\\n-8 -16 0\\n-12 -16 0\\n-1 -6 0\\n-6 -11 0\\n-11 -16 0\\n-1 -11 0\\n-6 -16 0\\n-1 -16 0\\n-2 -7 0\\n-7 -12 0\\n-2 -12 0\\n-5 -10 0\\n-10 -15 0\\n-5 -15 0\\n-9 -14 0\\n-3 -8 0\\n-4 -7 0\\n-7 -10 0\\n-10 -13 0\\n-4 -10 0\\n-7 -13 0\\n-4 -13 0\\n-3 -6 0\\n-6 -9 0\\n-3 -9 0\\n-8 -11 0\\n-11 -14 0\\n-8 -14 0\\n-2 -5 0\\n-12 -15 0\",\n"
"      php32: \"c Pigeonhole Principle (3 pigeons into 2 holes) [UNSAT]\\np cnf 6 9\\n1 2 0\\n3 4 0\\n5 6 0\\n-1 -3 0\\n-1 -5 0\\n-3 -5 0\\n-2 -4 0\\n-2 -6 0\\n-4 -6 0\",\n"
"      mutex: \"c Mutual Exclusion Policy\\np cnf 3 4\\n1 2 0\\n-1 -2 0\\n-1 3 0\\n1 0\"\n"
"    };\n"
"    function setMode(mode) {\n"
"      currentMode = mode;\n"
"      document.querySelectorAll('.tab-btn').forEach(b => b.classList.remove('active'));\n"
"      event.target.classList.add('active');\n"
"      if(mode === 'dimacs') loadPreset();\n"
"      else if(mode === 'json') {\n"
"        document.getElementById('code-editor').value = JSON.stringify({\n"
"          format: 'cnf',\n"
"          solver_config: { name: 'sodium-cdcl', timeout_seconds: 10, verbose: false },\n"
"          metadata: { description: 'Server redundancy policy', variable_mapping: { '1': 'Server_A', '2': 'Server_B' } },\n"
"          clauses: [[1, 2, 0], [-1, -2, 0]],\n"
"          assumptions: [1]\n"
"        }, null, 2);\n"
"      } else if(mode === 'nlp') {\n"
"        document.getElementById('code-editor').value = 'Server A and Server B cannot be active together. Server A is currently active. Always ensure at least one server is online.';\n"
"      }\n"
"    }\n"
"    function loadPreset() {\n"
"      const key = document.getElementById('preset-select').value;\n"
"      if(PRESETS[key]) document.getElementById('code-editor').value = PRESETS[key];\n"
"    }\n"
"    async function triggerSolve() {\n"
"      const text = document.getElementById('code-editor').value;\n"
"      const badge = document.getElementById('global-badge');\n"
"      badge.className = 'badge-status badge-ready'; badge.innerText = 'SOLVING...';\n"
"      let endpoint = '/api/v1/dimacs';\n"
"      let body = text;\n"
"      let headers = { 'Content-Type': 'text/plain' };\n"
"      if(currentMode === 'json') {\n"
"        endpoint = '/api/v1/solve';\n"
"        headers['Content-Type'] = 'application/json';\n"
"      } else if(currentMode === 'nlp') {\n"
"        endpoint = '/api/v1/nlp-solve';\n"
"        headers['Content-Type'] = 'application/json';\n"
"        body = JSON.stringify({ prompt: text });\n"
"      }\n"
"      try {\n"
"        const res = await fetch(endpoint, { method: 'POST', headers, body });\n"
"        const data = await res.json();\n"
"        renderResults(data);\n"
"      } catch(err) {\n"
"        badge.className = 'badge-status badge-unsat'; badge.innerText = 'ERROR';\n"
"        alert('Error connecting to Sodium solver: ' + err);\n"
"      }\n"
"    }\n"
"    function renderResults(data) {\n"
"      const resp = data.response || {};\n"
"      const badge = document.getElementById('global-badge');\n"
"      const status = resp.status || 'UNKNOWN';\n"
"      if(status === 'SATISFIABLE') { badge.className = 'badge-status badge-sat'; badge.innerText = 'SATISFIABLE'; }\n"
"      else if(status === 'UNSATISFIABLE') { badge.className = 'badge-status badge-unsat'; badge.innerText = 'UNSATISFIABLE'; }\n"
"      else { badge.className = 'badge-status badge-ready'; badge.innerText = status; }\n"
"      const stats = resp.statistics || {};\n"
"      document.getElementById('m-time').innerText = (stats.solve_time_ms ? stats.solve_time_ms.toFixed(3) : '0.00') + ' ms';\n"
"      document.getElementById('m-vars').innerText = stats.variables || 0;\n"
"      document.getElementById('m-clauses').innerText = stats.clauses || 0;\n"
"      document.getElementById('m-code').innerText = resp.status_code || 0;\n"
"      const tbody = document.getElementById('results-body');\n"
"      tbody.innerHTML = '';\n"
"      const assign = resp.assignment || {};\n"
"      const keys = Object.keys(assign);\n"
"      if(keys.length === 0) {\n"
"        tbody.innerHTML = `<tr><td colspan=\"3\" style=\"text-align:center;padding:24px;color:var(--text-muted);\">${status === 'UNSATISFIABLE' ? 'Contradiction found. No valid model exists.' : 'No assignments returned.'}</td></tr>`;\n"
"        return;\n"
"      }\n"
"      const vmap = (data.metadata && data.metadata.variable_mapping) || {};\n"
"      for(const k of keys) {\n"
"        const val = assign[k];\n"
"        const name = vmap[k] || ('Variable #' + k);\n"
"        const pill = val ? '<span class=\"pill-true\">TRUE</span>' : '<span class=\"pill-false\">FALSE</span>';\n"
"        tbody.innerHTML += `<tr><td><strong>${k}</strong></td><td>${name}</td><td>${pill}</td></tr>`;\n"
"      }\n"
"    }\n"
"    window.onload = loadPreset;\n"
"  </script>\n"
"</body>\n"
"</html>";


// ============================================================================
// SECTION 3: HTTP SERVER ENGINE & REST API
// ============================================================================

typedef struct {
    int client_fd;
    char client_ip[64];
} HttpClientContext;

static void send_http_response(
    int client_fd,
    int status_code,
    const char* status_text,
    const char* content_type,
    const char* body,
    size_t body_len
) {
    char header_buf[1024];
    int header_len = snprintf(
        header_buf, sizeof(header_buf),
        "HTTP/1.1 %d %s\r\n"
        "Server: Sodium-Automated-Reasoning/1.0\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type, Authorization, X-Requested-With\r\n"
        "Connection: close\r\n"
        "\r\n",
        status_code, status_text, content_type, body_len
    );

#if defined(_WIN32)
    send(client_fd, header_buf, header_len, 0);
    if (body && body_len > 0) {
        send(client_fd, body, (int)body_len, 0);
    }
#else
    write(client_fd, header_buf, header_len);
    if (body && body_len > 0) {
        write(client_fd, body, body_len);
    }
#endif
}

static char* extract_http_body(const char* request, size_t req_len, size_t* out_body_len) {
    const char* marker = strstr(request, "\r\n\r\n");
    if (!marker) marker = strstr(request, "\n\n");
    if (!marker) return NULL;

    size_t header_len = (size_t)(marker - request) + (strncmp(marker, "\r\n\r\n", 4) == 0 ? 4 : 2);
    if (header_len >= req_len) return NULL;

    size_t body_len = req_len - header_len;
    char* body = (char*)malloc(body_len + 1);
    memcpy(body, request + header_len, body_len);
    body[body_len] = '\0';

    if (out_body_len) *out_body_len = body_len;
    return body;
}

static void* handle_client_connection(void* arg) {
    HttpClientContext* ctx = (HttpClientContext*)arg;
    int client_fd = ctx->client_fd;

    char buffer[65536];
    int bytes_read = 0;

#if defined(_WIN32)
    bytes_read = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
#else
    bytes_read = (int)read(client_fd, buffer, sizeof(buffer) - 1);
#endif

    if (bytes_read <= 0) {
#if defined(_WIN32)
        closesocket(client_fd);
#else
        close(client_fd);
#endif
        free(ctx);
        return NULL;
    }

    buffer[bytes_read] = '\0';

    // Parse HTTP Request Line (e.g., "POST /api/v1/solve HTTP/1.1")
    char method[16] = {0};
    char path[256] = {0};
    sscanf(buffer, "%15s %255s", method, path);

    // 1. CORS Preflight
    if (strcmp(method, "OPTIONS") == 0) {
        send_http_response(client_fd, 204, "No Content", "text/plain", "", 0);
    }
    // 2. Health Endpoint
    else if (strcmp(method, "GET") == 0 && strcmp(path, "/health") == 0) {
        time_t uptime = time(NULL) - g_start_time;
        char health_json[256];
        int len = snprintf(health_json, sizeof(health_json),
            "{\"status\":\"healthy\",\"engine\":\"sodium-cdcl\",\"version\":\"1.0.0\",\"uptime_sec\":%lld}\n",
            (long long)uptime
        );
        send_http_response(client_fd, 200, "OK", "application/json", health_json, len);
    }
    // 3. Web UI Root
    else if (strcmp(method, "GET") == 0 && (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0)) {
        send_http_response(
            client_fd, 200, "OK", "text/html; charset=utf-8",
            EMBEDDED_INDEX_HTML, strlen(EMBEDDED_INDEX_HTML)
        );
    }
    // 4. REST Endpoint: Mode 1 JSON Solve
    else if (strcmp(method, "POST") == 0 && strcmp(path, "/api/v1/solve") == 0) {
        size_t body_len = 0;
        char* body = extract_http_body(buffer, (size_t)bytes_read, &body_len);
        if (!body) {
            const char* err = "{\"error\":\"Missing request body\"}\n";
            send_http_response(client_fd, 400, "Bad Request", "application/json", err, strlen(err));
        } else {
            char* out_json = NULL;
            sodium_api_solve_json(body, &out_json);
            if (out_json) {
                send_http_response(client_fd, 200, "OK", "application/json", out_json, strlen(out_json));
                sodium_api_free_string(out_json);
            } else {
                const char* err = "{\"error\":\"Solver execution error\"}\n";
                send_http_response(client_fd, 500, "Internal Server Error", "application/json", err, strlen(err));
            }
            free(body);
        }
    }
    // 5. REST Endpoint: DIMACS Solve
    else if (strcmp(method, "POST") == 0 && strcmp(path, "/api/v1/dimacs") == 0) {
        size_t body_len = 0;
        char* body = extract_http_body(buffer, (size_t)bytes_read, &body_len);
        if (!body) {
            const char* err = "{\"error\":\"Missing DIMACS formula body\"}\n";
            send_http_response(client_fd, 400, "Bad Request", "application/json", err, strlen(err));
        } else {
            char* out_json = NULL;
            sodium_api_solve_dimacs(body, &out_json);
            if (out_json) {
                send_http_response(client_fd, 200, "OK", "application/json", out_json, strlen(out_json));
                sodium_api_free_string(out_json);
            } else {
                const char* err = "{\"error\":\"DIMACS solver execution error\"}\n";
                send_http_response(client_fd, 500, "Internal Server Error", "application/json", err, strlen(err));
            }
            free(body);
        }
    }
    // 6. REST Endpoint: NLP Translation & Solve
    else if (strcmp(method, "POST") == 0 && strcmp(path, "/api/v1/nlp-solve") == 0) {
        size_t body_len = 0;
        char* body = extract_http_body(buffer, (size_t)bytes_read, &body_len);
        if (!body) {
            const char* err = "{\"error\":\"Missing NLP prompt body\"}\n";
            send_http_response(client_fd, 400, "Bad Request", "application/json", err, strlen(err));
        } else {
            // Extract "prompt" field if body is JSON
            char extracted_prompt[2048] = {0};
            char* p_pos = strstr(body, "\"prompt\"");
            if (p_pos) {
                char* colon = strchr(p_pos, ':');
                if (colon) {
                    char* q1 = strchr(colon, '"');
                    if (q1) {
                        char* q2 = strchr(q1 + 1, '"');
                        if (q2) {
                            size_t plen = (size_t)(q2 - (q1 + 1));
                            if (plen < sizeof(extracted_prompt)) {
                                strncpy(extracted_prompt, q1 + 1, plen);
                            }
                        }
                    }
                }
            }
            if (extracted_prompt[0] == '\0') {
                strncpy(extracted_prompt, body, sizeof(extracted_prompt) - 1);
            }

            // Pipe prompt into Python llm_router.py
            char cmd[4096];
            snprintf(cmd, sizeof(cmd), "python3 src/NLP/llm_router.py --text \"%s\" 2>/dev/null", extracted_prompt);

            FILE* fp = popen(cmd, "r");
            if (!fp) {
                const char* err = "{\"error\":\"Failed to invoke Python NLP router\"}\n";
                send_http_response(client_fd, 500, "Internal Error", "application/json", err, strlen(err));
            } else {
                char nlp_json[32768] = {0};
                size_t nread = fread(nlp_json, 1, sizeof(nlp_json) - 1, fp);
                nlp_json[nread] = '\0';
                pclose(fp);

                if (nread > 0) {
                    char* out_json = NULL;
                    sodium_api_solve_json(nlp_json, &out_json);
                    if (out_json) {
                        send_http_response(client_fd, 200, "OK", "application/json", out_json, strlen(out_json));
                        sodium_api_free_string(out_json);
                    } else {
                        send_http_response(client_fd, 200, "OK", "application/json", nlp_json, strlen(nlp_json));
                    }
                } else {
                    const char* err = "{\"error\":\"Python NLP router returned no output\"}\n";
                    send_http_response(client_fd, 502, "Bad Gateway", "application/json", err, strlen(err));
                }
            }
            free(body);
        }
    }
    // 7. Not Found
    else {
        const char* not_found = "{\"error\":\"Endpoint not found\"}\n";
        send_http_response(client_fd, 404, "Not Found", "application/json", not_found, strlen(not_found));
    }

#if defined(_WIN32)
    closesocket(client_fd);
#else
    close(client_fd);
#endif
    free(ctx);
    return NULL;
}

static void run_server_daemon(const char* host, int port) {
#if defined(_WIN32)
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    g_start_time = time(NULL);

    g_server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_server_fd < 0) {
        fprintf(stderr, "Error: Failed to create socket (%s)\n", strerror(errno));
        return;
    }

    int opt = 1;
#if defined(_WIN32)
    setsockopt(g_server_fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
#else
    setsockopt(g_server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);

    if (strcmp(host, "0.0.0.0") == 0 || strcmp(host, "*") == 0) {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        inet_pton(AF_INET, host, &addr.sin_addr);
    }

    if (bind(g_server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "Error: Failed to bind to %s:%d (%s)\n", host, port, strerror(errno));
#if defined(_WIN32)
        closesocket(g_server_fd);
#else
        close(g_server_fd);
#endif
        return;
    }

    if (listen(g_server_fd, 64) < 0) {
        fprintf(stderr, "Error: Failed to listen on socket (%s)\n", strerror(errno));
#if defined(_WIN32)
        closesocket(g_server_fd);
#else
        close(g_server_fd);
#endif
        return;
    }

    printf("====================================================================\n");
    printf(" Sodium Automated Reasoning Engine Daemon Running\n");
    printf(" Listening on: http://%s:%d\n", host, port);
    printf(" Serving Web Studio & REST API (Ready for HTTPS Reverse Proxy)\n");
    printf(" Press Ctrl+C to terminate cleanly.\n");
    printf("====================================================================\n\n");

    while (g_running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_fd = accept(g_server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) {
            if (!g_running) break;
            continue;
        }

        HttpClientContext* ctx = (HttpClientContext*)malloc(sizeof(HttpClientContext));
        ctx->client_fd = client_fd;
        inet_ntop(AF_INET, &client_addr.sin_addr, ctx->client_ip, sizeof(ctx->client_ip));

#if defined(_WIN32)
        // Windows single-thread handle
        handle_client_connection(ctx);
#else
        pthread_t thread;
        if (pthread_create(&thread, NULL, handle_client_connection, ctx) == 0) {
            pthread_detach(thread);
        } else {
            handle_client_connection(ctx);
        }
#endif
    }

    printf("\nShutting down Sodium daemon cleanly.\n");
#if defined(_WIN32)
    WSACleanup();
#endif
}

// ============================================================================
// SECTION 4: LOCAL HEADLESS CLI & SOLVER DISPATCHER
// ============================================================================

static char* read_entire_file(const char* filepath) {
    FILE* fp = fopen(filepath, "rb");
    if (!fp) return NULL;

    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (sz < 0) {
        fclose(fp);
        return NULL;
    }

    char* buf = (char*)malloc((size_t)sz + 1);
    size_t nr = fread(buf, 1, (size_t)sz, fp);
    buf[nr] = '\0';
    fclose(fp);
    return buf;
}

static char* read_entire_stdin(void) {
    size_t cap = 4096;
    size_t len = 0;
    char* buf = (char*)malloc(cap);

    char temp[4096];
    size_t n;
    while ((n = fread(temp, 1, sizeof(temp), stdin)) > 0) {
        if (len + n + 1 >= cap) {
            cap = (cap * 2) + n + 4096;
            buf = (char*)realloc(buf, cap);
        }
        memcpy(buf + len, temp, n);
        len += n;
    }
    buf[len] = '\0';
    return buf;
}

static void print_usage(const char* prog_name) {
    printf("Sodium - Automated Reasoning & Neuro-Symbolic Verification Engine\n\n");
    printf("Usage: %s [OPTIONS]\n\n", prog_name);
    printf("Local CLI Execution:\n");
    printf("  -f, --file <path>        Solve a DIMACS (.cnf) or Mode 1 (.json) problem file\n");
    printf("  -j, --json <string>      Solve a JSON CNF formula string directly\n");
    printf("  -d, --dimacs <string>    Solve a DIMACS CNF formula string directly\n");
    printf("  -n, --nlp <prompt>       Translate natural language via NLP bridge and solve\n");
    printf("  -b, --benchmark          Display high-precision microsecond execution metrics\n");
    printf("  -g, --gui                Launch the native C++ Dear ImGui Desktop UI\n");
    printf("\nServer & Cloud Deployment (HTTPS Web Serving):\n");
    printf("  -s, --serve              Launch embedded HTTP daemon serving Web UI and REST API\n");
    printf("  -p, --port <number>      Port to listen on (default: 8080)\n");
    printf("  -H, --host <ip>          Host address to bind (default: 0.0.0.0)\n");
    printf("\nPiped Input:\n");
    printf("  cat problem.cnf | %s\n", prog_name);
    printf("\nHelp:\n");
    printf("  -h, --help               Display this help guide and exit\n\n");
}

int main(int argc, char** argv) {
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    const char* file_path = NULL;
    const char* json_input = NULL;
    const char* dimacs_input = NULL;
    const char* nlp_prompt = NULL;
    bool serve_mode = false;
    bool benchmark_mode = false;
    bool gui_mode = false;
    int port = 8080;
    const char* host = "0.0.0.0";

    // Read environment overrides
    const char* env_port = getenv("PORT");
    if (!env_port) env_port = getenv("SODIUM_PORT");
    if (env_port) port = atoi(env_port);

    const char* env_host = getenv("SODIUM_HOST");
    if (env_host) host = env_host;

    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--serve") == 0) {
            serve_mode = true;
        } else if (strcmp(argv[i], "-b") == 0 || strcmp(argv[i], "--benchmark") == 0) {
            benchmark_mode = true;
        } else if (strcmp(argv[i], "-g") == 0 || strcmp(argv[i], "--gui") == 0) {
            gui_mode = true;
        } else if ((strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--port") == 0) && i + 1 < argc) {
            port = atoi(argv[++i]);
        } else if ((strcmp(argv[i], "-H") == 0 || strcmp(argv[i], "--host") == 0) && i + 1 < argc) {
            host = argv[++i];
        } else if ((strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--file") == 0) && i + 1 < argc) {
            file_path = argv[++i];
        } else if ((strcmp(argv[i], "-j") == 0 || strcmp(argv[i], "--json") == 0) && i + 1 < argc) {
            json_input = argv[++i];
        } else if ((strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--dimacs") == 0) && i + 1 < argc) {
            dimacs_input = argv[++i];
        } else if ((strcmp(argv[i], "-n") == 0 || strcmp(argv[i], "--nlp") == 0) && i + 1 < argc) {
            nlp_prompt = argv[++i];
        }
    }

    // 1. Server Mode
    if (serve_mode) {
        run_server_daemon(host, port);
        return 0;
    }

    // 2. GUI Mode
    if (gui_mode) {
        printf("[*] Launching Sodium Native C++ Dear ImGui Desktop Interface...\n");
#if defined(SODIUM_ENABLE_GUI)
        SodiumUI_Init();
        printf("[*] Desktop UI initialized. Entering main render loop.\n");
        // Desktop window loop runs here when linked with GLFW/SDL
        SodiumUI_Shutdown();
        return 0;
#else
        printf("Note: Native Dear ImGui window requires GLFW/SDL backend compilation.\n");
        printf("To use the visual interface, launch the server daemon:\n");
        printf("    %s --serve --port %d\n", argv[0], port);
        printf("Then open http://localhost:%d in your browser.\n", port);
        return 0;
#endif
    }

    // 3. Local CLI Execution
    char* content_to_free = NULL;
    const char* text_to_solve = NULL;
    bool is_json = false;

    if (file_path) {
        content_to_free = read_entire_file(file_path);
        if (!content_to_free) {
            fprintf(stderr, "Error: Unable to open file: %s\n", file_path);
            return 1;
        }
        text_to_solve = content_to_free;

        // Auto-detect JSON vs DIMACS by file extension or first non-whitespace
        if (strstr(file_path, ".json") != NULL) {
            is_json = true;
        } else {
            const char* p = text_to_solve;
            while (*p && isspace((unsigned char)*p)) p++;
            if (*p == '{') is_json = true;
        }
    } else if (json_input) {
        text_to_solve = json_input;
        is_json = true;
    } else if (dimacs_input) {
        text_to_solve = dimacs_input;
        is_json = false;
    } else if (nlp_prompt) {
        printf("[*] Routing natural language specification to Python Pydantic bridge...\n");
        char cmd[4096];
        snprintf(cmd, sizeof(cmd), "python3 src/NLP/llm_router.py --text \"%s\" 2>/dev/null", nlp_prompt);

        FILE* fp = popen(cmd, "r");
        if (!fp) {
            fprintf(stderr, "Error: Failed to execute src/NLP/llm_router.py\n");
            return 1;
        }
        content_to_free = (char*)malloc(32768);
        size_t nr = fread(content_to_free, 1, 32767, fp);
        content_to_free[nr] = '\0';
        pclose(fp);

        if (nr == 0) {
            fprintf(stderr, "Error: No output from Python NLP router.\n");
            free(content_to_free);
            return 1;
        }
        text_to_solve = content_to_free;
        is_json = true;
    } else {
        // Check if input was piped through stdin
#if !defined(_WIN32)
        if (!isatty(STDIN_FILENO)) {
            content_to_free = read_entire_stdin();
            if (content_to_free && strlen(content_to_free) > 0) {
                text_to_solve = content_to_free;
                const char* p = text_to_solve;
                while (*p && isspace((unsigned char)*p)) p++;
                if (*p == '{') is_json = true;
            }
        }
#endif
    }

    if (!text_to_solve) {
        print_usage(argv[0]);
        return 1;
    }

    // Solve Problem
    double t_start = get_time_ms_precise();
    char* json_response = NULL;
    int status_code = 0;

    if (is_json) {
        status_code = sodium_api_solve_json(text_to_solve, &json_response);
    } else {
        status_code = sodium_api_solve_dimacs(text_to_solve, &json_response);
    }
    double elapsed_ms = get_time_ms_precise() - t_start;

    // Display Result
    if (json_response) {
        printf("%s\n", json_response);
        sodium_api_free_string(json_response);
    }

    if (benchmark_mode) {
        printf("\n================ [ Sodium Benchmark Telemetry ] ================\n");
        printf(" Status Code:      %d (%s)\n", status_code,
            (status_code == 10) ? "SATISFIABLE" : ((status_code == 20) ? "UNSATISFIABLE" : "UNKNOWN"));
        printf(" Solve Latency:    %.4f ms (%.6f seconds)\n", elapsed_ms, elapsed_ms / 1000.0);
        printf("================================================================\n");
    }

    if (content_to_free) free(content_to_free);
    return (status_code == 10) ? 0 : ((status_code == 20) ? 20 : 1);
}
