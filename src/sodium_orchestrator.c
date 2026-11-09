#include "API.h"
#include "Common/logger.h"
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
    LOG_INFO("CORE", "Signal %d captured. Initiating graceful shutdown...", sig);
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
"    .editor-wrap { display: flex; flex-direction: column; gap: 8px; flex: 1; min-height: 280px; }\n"
"    textarea { width: 100%; height: 280px; background: var(--code-bg); border: 1px solid var(--card-border); border-radius: 8px; color: #E0E0E0; font-family: 'SF Mono', Menlo, Consolas, monospace; font-size: 13px; padding: 12px; resize: vertical; outline: none; }\n"
"    textarea:focus { border-color: var(--purple-main); box-shadow: 0 0 0 2px rgba(138, 63, 252, 0.2); }\n"
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
"    .btn-nonllm { background: #58169C !important; color: #FFF !important; border: 1px solid var(--purple-light) !important; }\n"
"    .rule-row { display: flex; gap: 8px; align-items: center; }\n"
"    .rule-label { font-size: 11px; color: var(--text-muted); width: 48px; flex-shrink: 0; font-weight: 600; }\n"
"    .rule-input { flex: 1; background: #161324; border: 1px solid var(--card-border); color: #FFF; padding: 8px 12px; border-radius: 6px; font-size: 13px; outline: none; transition: 0.2s; }\n"
"    .rule-input:focus { border-color: var(--purple-light); box-shadow: 0 0 0 2px rgba(165, 110, 255, 0.2); }\n"
"    .rule-del-btn { background: transparent; border: none; color: #FA4D56; cursor: pointer; font-size: 16px; padding: 0 6px; line-height: 1; }\n"
"    .rule-del-btn:hover { color: #FF8389; }\n"
"  </style>\n"
"</head>\n"
"<body>\n"
"  <!-- Non-LLM Guidance Modal Popup -->\n"
"  <div id=\"nlp-guidance-modal\" style=\"display:none;position:fixed;top:0;left:0;width:100%;height:100%;background:rgba(0,0,0,0.75);z-index:999;align-items:center;justify-content:center;\">\n"
"    <div style=\"background:var(--card-bg);border:1px solid var(--purple-main);border-radius:12px;padding:24px;max-width:540px;display:flex;flex-direction:column;gap:14px;box-shadow:0 12px 36px rgba(0,0,0,0.8);\">\n"
"      <h3 style=\"color:var(--purple-light);font-size:18px;\">ℹ️ Non-LLM Classical NLP Active</h3>\n"
"      <p style=\"font-size:14px;line-height:1.5;color:#E0E0E0;\">\n"
"        You have toggled to Sodium's offline, deterministic <strong>Non-LLM Rule &amp; Math Compiler</strong>.\n"
"      </p>\n"
"      <div style=\"background:var(--code-bg);border-left:3px solid var(--purple-main);padding:12px;border-radius:6px;font-size:13px;line-height:1.6;color:#C9C5D6;\">\n"
"        <strong style=\"color:#FFF;\">Multi-Line Rule Guidelines:</strong><br/>\n"
"        Enter each distinct requirement or mathematical constraint into its <strong>own separate line</strong> below to avoid run-on sentences.<br/><br/>\n"
"        <strong style=\"color:#FFF;\">Supported Formats:</strong><br/>\n"
"        • Implication: <em>If Service A is active then Service B must be online.</em><br/>\n"
"        • Exclusion: <em>Service A and Service B cannot both be active.</em><br/>\n"
"        • Equivalence: <em>Service A is equivalent to Service B.</em><br/>\n"
"        • Equations: <em>x + y &lt;= 10; Count &gt;= 2; A -&gt; B.</em>\n"
"      </div>\n"
"      <button class=\"btn-solve\" style=\"align-self:flex-end;padding:8px 20px;font-size:13px;cursor:pointer;\" onclick=\"closeNLPModal()\">Understood &amp; Proceed</button>\n"
"    </div>\n"
"  </div>\n"
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
"          <button id=\"tab-nlp\" class=\"tab-btn active\" onclick=\"setMode('nlp')\">Natural Language (NLP)</button>\n"
"          <button id=\"tab-dimacs\" class=\"tab-btn\" onclick=\"setMode('dimacs')\">DIMACS CNF</button>\n"
"        </div>\n"
"        <div id=\"nlp-engine-controls\" style=\"display:flex;align-items:center;justify-content:space-between;padding:4px 0;\">\n"
"          <span style=\"font-size:12px;color:var(--text-muted);\">NLP Engine:</span>\n"
"          <button id=\"engine-toggle-btn\" class=\"tab-btn\" style=\"font-size:12px;padding:4px 12px;background:var(--code-bg);border:1px solid var(--card-border);\" onclick=\"toggleNLPEngine()\">LLM (AI)</button>\n"
"        </div>\n"
"        <div class=\"editor-wrap\">\n"
"          <textarea id=\"code-editor\" spellcheck=\"false\" placeholder=\"Enter your natural language requirements or policy rules here...\"></textarea>\n"
"          <div id=\"nonllm-rule-container\" style=\"display:none; flex-direction:column; gap:10px; height:280px; overflow-y:auto; background:var(--code-bg); padding:12px; border:1px solid var(--card-border); border-radius:8px;\">\n"
"            <div style=\"font-size:12px; color:var(--text-muted); display:flex; justify-content:space-between; align-items:center; border-bottom:1px solid var(--card-border); padding-bottom:6px;\">\n"
"              <span>Separate Rules (One per line to prevent continuous run-on):</span>\n"
"              <button type=\"button\" class=\"tab-btn\" style=\"padding:3px 10px; font-size:11px; background:var(--purple-dark); color:#FFF; cursor:pointer;\" onclick=\"addRuleRow('')\">+ Add Rule</button>\n"
"            </div>\n"
"            <div id=\"rule-rows\" style=\"display:flex; flex-direction:column; gap:8px;\"></div>\n"
"          </div>\n"
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
"    let currentMode = 'nlp';\n"
"    let nlpEngine = 'llm';\n"
"\n"
"    function toggleNLPEngine() {\n"
"      const btn = document.getElementById('engine-toggle-btn');\n"
"      const editor = document.getElementById('code-editor');\n"
"      const nonllmBox = document.getElementById('nonllm-rule-container');\n"
"      if (nlpEngine === 'llm') {\n"
"        nlpEngine = 'non-llm';\n"
"        btn.innerText = 'Non-LLM (Classical NLP)';\n"
"        btn.classList.add('btn-nonllm');\n"
"        document.getElementById('nlp-guidance-modal').style.display = 'flex';\n"
"        editor.style.display = 'none';\n"
"        nonllmBox.style.display = 'flex';\n"
"        initRuleRowsFromText(editor.value);\n"
"      } else {\n"
"        nlpEngine = 'llm';\n"
"        btn.innerText = 'LLM (AI)';\n"
"        btn.classList.remove('btn-nonllm');\n"
"        editor.value = getNonLLMText();\n"
"        nonllmBox.style.display = 'none';\n"
"        editor.style.display = 'block';\n"
"      }\n"
"    }\n"
"\n"
"    function closeNLPModal() {\n"
"      document.getElementById('nlp-guidance-modal').style.display = 'none';\n"
"    }\n"
"\n"
"    function initRuleRowsFromText(text) {\n"
"      const container = document.getElementById('rule-rows');\n"
"      container.innerHTML = '';\n"
"      let lines = text ? text.split(/[.;\\n]+/).map(s => s.trim()).filter(s => s.length > 0) : [];\n"
"      if (lines.length === 0) {\n"
"        lines = [\n"
"          'PrimaryServer and StandbyServer cannot both be active',\n"
"          'Either PrimaryServer or StandbyServer must be active',\n"
"          'If MaintenanceMode is active then PrimaryServer is offline',\n"
"          'MaintenanceMode is active'\n"
"        ];\n"
"      }\n"
"      lines.forEach(line => addRuleRow(line));\n"
"    }\n"
"\n"
"    function addRuleRow(val) {\n"
"      const container = document.getElementById('rule-rows');\n"
"      const count = container.children.length + 1;\n"
"      const row = document.createElement('div');\n"
"      row.className = 'rule-row';\n"
"      const cleanVal = (val || '').replace(/\"/g, '&quot;');\n"
"      row.innerHTML = `\n"
"        <span class=\"rule-label\">Rule ${count}:</span>\n"
"        <input type=\"text\" class=\"rule-input\" placeholder=\"e.g. PrimaryServer and StandbyServer cannot both be active\" value=\"${cleanVal}\" onkeydown=\"handleRuleKeyDown(event, this)\" />\n"
"        <button type=\"button\" class=\"rule-del-btn\" title=\"Delete rule\" onclick=\"removeRuleRow(this)\">✕</button>\n"
"      `;\n"
"      container.appendChild(row);\n"
"      const inp = row.querySelector('input');\n"
"      if (!val) inp.focus();\n"
"    }\n"
"\n"
"    function removeRuleRow(btn) {\n"
"      const container = document.getElementById('rule-rows');\n"
"      if (container.children.length <= 1) return;\n"
"      btn.closest('.rule-row').remove();\n"
"      container.querySelectorAll('.rule-row').forEach((r, idx) => {\n"
"        r.querySelector('.rule-label').innerText = `Rule ${idx + 1}:`;\n"
"      });\n"
"    }\n"
"\n"
"    function handleRuleKeyDown(e, input) {\n"
"      if (e.key === 'Enter') {\n"
"        e.preventDefault();\n"
"        addRuleRow('');\n"
"      }\n"
"    }\n"
"\n"
"    function getNonLLMText() {\n"
"      const inputs = document.querySelectorAll('.rule-input');\n"
"      const rules = [];\n"
"      inputs.forEach(inp => {\n"
"        const t = inp.value.trim();\n"
"        if (t) {\n"
"          rules.push(t.endsWith('.') || t.endsWith(';') ? t : t + '.');\n"
"        }\n"
"      });\n"
"      return rules.join(' ');\n"
"    }\n"
"\n"
"    function setMode(mode) {\n"
"      currentMode = mode;\n"
"      document.getElementById('tab-nlp').classList.toggle('active', mode === 'nlp');\n"
"      document.getElementById('tab-dimacs').classList.toggle('active', mode === 'dimacs');\n"
"      const nlpControls = document.getElementById('nlp-engine-controls');\n"
"      const editor = document.getElementById('code-editor');\n"
"      const nonllmBox = document.getElementById('nonllm-rule-container');\n"
"      if (mode === 'nlp') {\n"
"        if (nlpControls) nlpControls.style.display = 'flex';\n"
"        if (nlpEngine === 'non-llm') {\n"
"          editor.style.display = 'none';\n"
"          nonllmBox.style.display = 'flex';\n"
"          if (document.getElementById('rule-rows').children.length === 0) {\n"
"            initRuleRowsFromText(editor.value);\n"
"          }\n"
"        } else {\n"
"          editor.style.display = 'block';\n"
"          nonllmBox.style.display = 'none';\n"
"          if (!editor.value) {\n"
"            editor.value = 'PrimaryServer and StandbyServer cannot both be active. Either PrimaryServer or StandbyServer must be active. If MaintenanceMode is active then PrimaryServer is offline. MaintenanceMode is active.';\n"
"          }\n"
"        }\n"
"      } else if (mode === 'dimacs') {\n"
"        if (nlpControls) nlpControls.style.display = 'none';\n"
"        nonllmBox.style.display = 'none';\n"
"        editor.style.display = 'block';\n"
"        if (!editor.value || editor.value.startsWith('PrimaryServer')) {\n"
"          editor.value = 'c High-Availability Dual-Server Split-Brain Prevention Policy\\np cnf 3 4\\n-1 -2 0\\n1 2 0\\n-3 -1 0\\n3 0';\n"
"        }\n"
"      }\n"
"    }\n"
"\n"
"    async function triggerSolve() {\n"
"      let text = '';\n"
"      if (currentMode === 'nlp' && nlpEngine === 'non-llm') {\n"
"        text = getNonLLMText();\n"
"      } else {\n"
"        text = document.getElementById('code-editor').value;\n"
"      }\n"
"      if (!text.trim()) {\n"
"        alert('Please enter a formula or specification to solve.');\n"
"        return;\n"
"      }\n"
"      const badge = document.getElementById('global-badge');\n"
"      badge.className = 'badge-status badge-ready'; badge.innerText = 'SOLVING...';\n"
"      let endpoint = (currentMode === 'nlp') ? '/api/v1/nlp-solve' : '/api/v1/dimacs';\n"
"      let body = (currentMode === 'nlp') ? JSON.stringify({ prompt: text, engine: nlpEngine }) : text;\n"
"      let headers = { 'Content-Type': (currentMode === 'nlp') ? 'application/json' : 'text/plain' };\n"
"      try {\n"
"        const res = await fetch(endpoint, { method: 'POST', headers, body });\n"
"        const data = await res.json();\n"
"        renderResults(data);\n"
"      } catch (err) {\n"
"        badge.className = 'badge-status badge-unsat'; badge.innerText = 'ERROR';\n"
"        alert('Error connecting to Sodium solver: ' + err);\n"
"      }\n"
"    }\n"
"\n"
"    function renderResults(data) {\n"
"      const resp = data.response || {};\n"
"      const badge = document.getElementById('global-badge');\n"
"      if (data.error) {\n"
"        badge.className = 'badge-status badge-unsat'; badge.innerText = 'ERROR';\n"
"        document.getElementById('results-body').innerHTML = `<tr><td colspan=\"3\" style=\"text-align:center;padding:24px;color:#FA4D56;\"><strong>Error:</strong> ${data.error} ${data.details ? '<br/><small>' + data.details + '</small>' : ''}</td></tr>`;\n"
"        return;\n"
"      }\n"
"      const status = resp.status || 'UNKNOWN';\n"
"      if (status === 'SATISFIABLE') { badge.className = 'badge-status badge-sat'; badge.innerText = 'SATISFIABLE'; }\n"
"      else if (status === 'UNSATISFIABLE') { badge.className = 'badge-status badge-unsat'; badge.innerText = 'UNSATISFIABLE'; }\n"
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
"      if (keys.length === 0) {\n"
"        tbody.innerHTML = `<tr><td colspan=\"3\" style=\"text-align:center;padding:24px;color:var(--text-muted);\">${status === 'UNSATISFIABLE' ? 'Contradiction found. No valid model exists.' : 'No assignments returned.'}</td></tr>`;\n"
"        return;\n"
"      }\n"
"      const vmap = (data.metadata && data.metadata.variable_mapping) || {};\n"
"      for (const k of keys) {\n"
"        const val = assign[k];\n"
"        const name = vmap[k] || ('Variable #' + k);\n"
"        const pill = val ? '<span class=\"pill-true\">TRUE</span>' : '<span class=\"pill-false\">FALSE</span>';\n"
"        tbody.innerHTML += `<tr><td><strong>${k}</strong></td><td>${name}</td><td>${pill}</td></tr>`;\n"
"      }\n"
"    }\n"
"\n"
"    window.onload = function() {\n"
"      setMode('nlp');\n"
"    };\n"
"  </script>\n"
"</body>\n"
"</html>";

// ============================================================================
// SECTION 3: HTTP SERVER ENGINE & REST API HANDLERS
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
    LOG_DEBUG("HTTP", "Sent %d %s (Content-Length: %zu)", status_code, status_text, body_len);
}

static const char* find_case_insensitive(const char* haystack, const char* needle) {
    if (!haystack || !needle) return NULL;
    size_t needle_len = strlen(needle);
    for (const char* p = haystack; *p; p++) {
        if (strncasecmp(p, needle, needle_len) == 0) {
            return p;
        }
    }
    return NULL;
}

static void extract_json_field_string(const char* json, const char* field, char* out, size_t max_out) {
    out[0] = '\0';
    if (!json || !field) return;

    char key[64];
    snprintf(key, sizeof(key), "\"%s\"", field);
    const char* p = strstr(json, key);
    if (!p) return;

    p = strchr(p, ':');
    if (!p) return;
    p++;

    while (*p && isspace((unsigned char)*p)) p++;
    if (*p != '"') return;
    p++; // skip opening quote

    size_t idx = 0;
    while (*p && idx + 1 < max_out) {
        if (*p == '\\') {
            p++;
            if (*p == '"') { out[idx++] = '"'; p++; }
            else if (*p == '\\') { out[idx++] = '\\'; p++; }
            else if (*p == 'n') { out[idx++] = '\n'; p++; }
            else if (*p == 't') { out[idx++] = '\t'; p++; }
            else if (*p == 'r') { p++; }
            else if (*p) { out[idx++] = *p; p++; }
        } else if (*p == '"') {
            break; // reached unescaped closing quote
        } else {
            out[idx++] = *p;
            p++;
        }
    }
    out[idx] = '\0';
}

static char* extract_http_body(const char* request, size_t req_len, size_t* out_body_len) {
    const char* marker = strstr(request, "\r\n\r\n");
    if (!marker) marker = strstr(request, "\n\n");
    if (!marker) return NULL;

    size_t header_len = (size_t)(marker - request) + (strncmp(marker, "\r\n\r\n", 4) == 0 ? 4 : 2);
    if (header_len > req_len) return NULL;

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
    int total_bytes = 0;
    const char* header_end = NULL;
    size_t header_len = 0;
    size_t content_length = 0;

    // Phase 1: Read HTTP request headers until \r\n\r\n or \n\n
    while (total_bytes < (int)sizeof(buffer) - 1) {
#if defined(_WIN32)
        int n = recv(client_fd, buffer + total_bytes, sizeof(buffer) - 1 - total_bytes, 0);
#else
        int n = (int)read(client_fd, buffer + total_bytes, sizeof(buffer) - 1 - total_bytes);
#endif
        if (n <= 0) break;
        total_bytes += n;
        buffer[total_bytes] = '\0';

        header_end = strstr(buffer, "\r\n\r\n");
        if (!header_end) header_end = strstr(buffer, "\n\n");
        if (header_end) {
            header_len = (size_t)(header_end - buffer) + (strncmp(header_end, "\r\n\r\n", 4) == 0 ? 4 : 2);
            break;
        }
    }

    if (!header_end || total_bytes <= 0) {
#if defined(_WIN32)
        closesocket(client_fd);
#else
        close(client_fd);
#endif
        free(ctx);
        return NULL;
    }

    // Phase 2: Parse Content-Length header if present
    const char* cl_pos = find_case_insensitive(buffer, "Content-Length:");
    if (cl_pos) {
        content_length = (size_t)strtoul(cl_pos + 15, NULL, 10);
    }

    // Phase 3: Read remaining body bytes if needed
    size_t body_received = (size_t)total_bytes - header_len;
    while (body_received < content_length && total_bytes < (int)sizeof(buffer) - 1) {
        size_t needed = content_length - body_received;
        size_t space_left = sizeof(buffer) - 1 - total_bytes;
        size_t to_read = (needed < space_left) ? needed : space_left;

#if defined(_WIN32)
        int n = recv(client_fd, buffer + total_bytes, (int)to_read, 0);
#else
        int n = (int)read(client_fd, buffer + total_bytes, to_read);
#endif
        if (n <= 0) break;
        total_bytes += n;
        body_received += (size_t)n;
        buffer[total_bytes] = '\0';
    }

    // Parse HTTP Request Line (e.g., "POST /api/v1/nlp-solve HTTP/1.1")
    char method[16] = {0};
    char path[256] = {0};
    sscanf(buffer, "%15s %255s", method, path);

    LOG_INFO("HTTP", "Request: %s %s from client %s (FD: %d, Total: %d bytes, Body: %zu bytes)",
             method, path, ctx->client_ip, client_fd, total_bytes, body_received);

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
        char* body = extract_http_body(buffer, (size_t)total_bytes, &body_len);
        if (!body || body_len == 0) {
            LOG_WARN("HTTP", "POST /api/v1/solve missing body");
            const char* err = "{\"error\":\"Missing request body\"}\n";
            send_http_response(client_fd, 400, "Bad Request", "application/json", err, strlen(err));
            if (body) free(body);
        } else {
            LOG_INFO("API", "POST /api/v1/solve: Ingesting JSON payload (%zu bytes)", body_len);
            char* out_json = NULL;
            sodium_api_solve_json(body, &out_json);
            if (out_json) {
                send_http_response(client_fd, 200, "OK", "application/json", out_json, strlen(out_json));
                sodium_api_free_string(out_json);
            } else {
                LOG_ERROR("API", "POST /api/v1/solve solver execution failed");
                const char* err = "{\"error\":\"Solver execution error\"}\n";
                send_http_response(client_fd, 500, "Internal Server Error", "application/json", err, strlen(err));
            }
            free(body);
        }
    }
    // 5. REST Endpoint: DIMACS Solve
    else if (strcmp(method, "POST") == 0 && strcmp(path, "/api/v1/dimacs") == 0) {
        size_t body_len = 0;
        char* body = extract_http_body(buffer, (size_t)total_bytes, &body_len);
        if (!body || body_len == 0) {
            LOG_WARN("HTTP", "POST /api/v1/dimacs missing formula body");
            const char* err = "{\"error\":\"Missing DIMACS formula body\"}\n";
            send_http_response(client_fd, 400, "Bad Request", "application/json", err, strlen(err));
            if (body) free(body);
        } else {
            LOG_INFO("API", "POST /api/v1/dimacs: Ingesting formula (%zu bytes)", body_len);
            char* out_json = NULL;
            sodium_api_solve_dimacs(body, &out_json);
            if (out_json) {
                send_http_response(client_fd, 200, "OK", "application/json", out_json, strlen(out_json));
                sodium_api_free_string(out_json);
            } else {
                LOG_ERROR("API", "POST /api/v1/dimacs solver execution error");
                const char* err = "{\"error\":\"DIMACS solver execution error\"}\n";
                send_http_response(client_fd, 500, "Internal Server Error", "application/json", err, strlen(err));
            }
            free(body);
        }
    }
    // 6. REST Endpoint: NLP Translation & Solve
    else if (strcmp(method, "POST") == 0 && strcmp(path, "/api/v1/nlp-solve") == 0) {
        size_t body_len = 0;
        char* body = extract_http_body(buffer, (size_t)total_bytes, &body_len);
        if (!body || body_len == 0) {
            LOG_WARN("HTTP", "POST /api/v1/nlp-solve missing NLP prompt body (Body len: %zu)", body_len);
            const char* err = "{\"error\":\"Missing NLP prompt body\"}\n";
            send_http_response(client_fd, 400, "Bad Request", "application/json", err, strlen(err));
            if (body) free(body);
        } else {
            // Extract "prompt" field if body is JSON
            char extracted_prompt[32768] = {0};
            extract_json_field_string(body, "prompt", extracted_prompt, sizeof(extracted_prompt));
            if (extracted_prompt[0] == '\0') {
                strncpy(extracted_prompt, body, sizeof(extracted_prompt) - 1);
            }

            bool is_non_llm = (strstr(body, "non-llm") != NULL);
            LOG_INFO("NLP", "POST /api/v1/nlp-solve: Engine [%s], Prompt length: %zu bytes",
                     is_non_llm ? "non-llm" : "llm", strlen(extracted_prompt));

            const char* py_bin = (access(".venv/bin/python3", X_OK) == 0) ? ".venv/bin/python3" : "python3";

            // Safe temporary file to avoid shell argument escaping limits and issues
            char tmp_path[] = "/tmp/sodium_prompt_XXXXXX";
            int tmp_fd = mkstemp(tmp_path);
            if (tmp_fd < 0) {
                LOG_ERROR("NLP", "Failed to create temporary file for NLP prompt");
                const char* err = "{\"error\":\"Failed to create temporary prompt file\"}\n";
                send_http_response(client_fd, 500, "Internal Server Error", "application/json", err, strlen(err));
            } else {
                write(tmp_fd, extracted_prompt, strlen(extracted_prompt));
                close(tmp_fd);

                char cmd[1024];
                snprintf(cmd, sizeof(cmd), "%s src/NLP/llm_router.py %s --file \"%s\" 2>/tmp/sodium_nlp_err.log",
                         py_bin, is_non_llm ? "--engine non-llm" : "", tmp_path);

                FILE* fp = popen(cmd, "r");
                if (!fp) {
                    LOG_ERROR("NLP", "Failed to execute Python subprocess: %s", cmd);
                    unlink(tmp_path);
                    const char* err = "{\"error\":\"Failed to invoke Python NLP router\"}\n";
                    send_http_response(client_fd, 500, "Internal Server Error", "application/json", err, strlen(err));
                } else {
                    char nlp_json[65536] = {0};
                    size_t nread = fread(nlp_json, 1, sizeof(nlp_json) - 1, fp);
                    nlp_json[nread] = '\0';
                    int pcode = pclose(fp);
                    unlink(tmp_path);

                    if (nread > 0) {
                        LOG_INFO("NLP", "Python NLP router returned %zu bytes (Exit: %d)", nread, pcode);
                        char* out_json = NULL;
                        sodium_api_solve_json(nlp_json, &out_json);
                        if (out_json) {
                            send_http_response(client_fd, 200, "OK", "application/json", out_json, strlen(out_json));
                            sodium_api_free_string(out_json);
                        } else {
                            send_http_response(client_fd, 200, "OK", "application/json", nlp_json, strlen(nlp_json));
                        }
                    } else {
                        LOG_WARN("NLP", "Python NLP router returned 0 bytes (Exit: %d)", pcode);
                        FILE* err_fp = fopen("/tmp/sodium_nlp_err.log", "r");
                        char err_buf[512] = {0};
                        if (err_fp) {
                            size_t er = fread(err_buf, 1, sizeof(err_buf) - 1, err_fp);
                            err_buf[er] = '\0';
                            fclose(err_fp);
                        }
                        char err_resp[1024];
                        snprintf(err_resp, sizeof(err_resp),
                                 "{\"error\":\"Python NLP router returned no output\",\"details\":\"%.200s\"}\n",
                                 err_buf[0] ? err_buf : "Compilation error in NLP pipeline");
                        send_http_response(client_fd, 502, "Bad Gateway", "application/json", err_resp, strlen(err_resp));
                    }
                }
            }
            free(body);
        }
    }
    // 7. Ignore Browser Icon Prefetches
    else if (strstr(path, "favicon.ico") || strstr(path, "apple-touch-icon")) {
        send_http_response(client_fd, 204, "No Content", "text/plain", "", 0);
    }
    // 8. Not Found
    else {
        LOG_WARN("HTTP", "Endpoint not found: %s %s", method, path);
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
        LOG_ERROR("SERVER", "Failed to create server socket (%s)", strerror(errno));
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
        LOG_ERROR("SERVER", "Failed to bind to %s:%d (%s)", host, port, strerror(errno));
        fprintf(stderr, "Error: Failed to bind to %s:%d (%s)\n", host, port, strerror(errno));
#if defined(_WIN32)
        closesocket(g_server_fd);
#else
        close(g_server_fd);
#endif
        return;
    }

    if (listen(g_server_fd, 64) < 0) {
        LOG_ERROR("SERVER", "Failed to listen on socket (%s)", strerror(errno));
        fprintf(stderr, "Error: Failed to listen on socket (%s)\n", strerror(errno));
#if defined(_WIN32)
        closesocket(g_server_fd);
#else
        close(g_server_fd);
#endif
        return;
    }

    LOG_INFO("SERVER", "HTTP Daemon started on http://%s:%d (Socket FD: %d)", host, port, g_server_fd);
    printf("====================================================================\n");
    printf(" Sodium Engine Daemon Running\n");
    printf(" Listening on: http://%s:%d\n", host, port);
    printf(" Serving Web Studio & REST API (Ready for HTTPS Reverse Proxy)\n");
    printf(" Logging active to: %s\n", logger_get_filepath());
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
        LOG_DEBUG("SERVER", "Connection accepted (FD: %d, IP: %s)", client_fd, ctx->client_ip);

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

    LOG_INFO("SERVER", "HTTP Daemon loop finished. Total uptime: %llds", (long long)(time(NULL) - g_start_time));
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
    printf("  --non-llm                Use deterministic Classical NLP pipeline instead of LLM\n");
    printf("  -b, --benchmark          Display high-precision microsecond execution metrics\n");
    printf("  -g, --gui                Launch the native C++ Dear ImGui Desktop UI\n");
    printf("  -l, --log <path>         Specify destination file for action logging (default: sodium.log)\n");
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
    const char* log_file = "sodium.log";
    bool serve_mode = false;
    bool benchmark_mode = false;
    bool gui_mode = false;
    bool non_llm_mode = false;
    int port = 8080;
    const char* host = "0.0.0.0";

    // Read environment overrides
    const char* env_port = getenv("PORT");
    if (!env_port) env_port = getenv("SODIUM_PORT");
    if (env_port) port = atoi(env_port);

    const char* env_host = getenv("SODIUM_HOST");
    if (env_host) host = env_host;

    // Pre-scan for custom log path
    for (int i = 1; i < argc; i++) {
        if ((strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--log") == 0) && i + 1 < argc) {
            log_file = argv[i + 1];
            break;
        }
    }

    // Initialize application-wide logging
    logger_init(log_file);

    // Reconstruct CLI invocation string
    char full_cmd[2048] = {0};
    for (int i = 0; i < argc; i++) {
        strncat(full_cmd, argv[i], sizeof(full_cmd) - strlen(full_cmd) - 1);
        if (i < argc - 1) strncat(full_cmd, " ", sizeof(full_cmd) - strlen(full_cmd) - 1);
    }
    LOG_INFO("CORE", "Sodium engine starting up (PID: %d)", (int)getpid());
    LOG_INFO("CONFIG", "Command-line invocation: %s", full_cmd);
    LOG_INFO("CONFIG", "Action logging destination: %s", log_file);

    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            logger_shutdown();
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
        } else if (strcmp(argv[i], "--non-llm") == 0) {
            non_llm_mode = true;
        } else if ((strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--log") == 0) && i + 1 < argc) {
            i++; // already handled in pre-scan
        }
    }

    // 1. Server Mode
    if (serve_mode) {
        LOG_INFO("CORE", "Mode selected: Web Server Daemon (Host: %s, Port: %d)", host, port);
        run_server_daemon(host, port);
        logger_shutdown();
        return 0;
    }

    // 2. GUI Mode
    if (gui_mode) {
        LOG_INFO("CORE", "Mode selected: Native Desktop GUI");
        printf("[*] Launching Sodium Native C++ Dear ImGui Desktop Interface...\n");
#if defined(SODIUM_ENABLE_GUI)
        SodiumUI_Init();
        printf("[*] Desktop UI initialized. Entering main render loop.\n");
        // Desktop window loop runs here when linked with GLFW/SDL
        SodiumUI_Shutdown();
        logger_shutdown();
        return 0;
#else
        printf("Note: Native Dear ImGui window requires GLFW/SDL backend compilation.\n");
        printf("To use the visual interface, launch the server daemon:\n");
        printf("    %s --serve --port %d\n", argv[0], port);
        printf("Then open http://localhost:%d in your browser.\n", port);
        logger_shutdown();
        return 0;
#endif
    }

    // 3. Local CLI Execution
    LOG_INFO("CORE", "Mode selected: Local CLI Execution");
    char* content_to_free = NULL;
    const char* text_to_solve = NULL;
    bool is_json = false;

    if (file_path) {
        LOG_INFO("CLI", "Loading input formula from file: %s", file_path);
        content_to_free = read_entire_file(file_path);
        if (!content_to_free) {
            LOG_ERROR("CLI", "Unable to open or read file: %s", file_path);
            fprintf(stderr, "Error: Unable to open file: %s\n", file_path);
            logger_shutdown();
            return 1;
        }
        text_to_solve = content_to_free;
        LOG_INFO("CLI", "File read successfully (%zu bytes)", strlen(text_to_solve));

        // Auto-detect JSON vs DIMACS by file extension or first non-whitespace
        if (strstr(file_path, ".json") != NULL) {
            is_json = true;
        } else {
            const char* p = text_to_solve;
            while (*p && isspace((unsigned char)*p)) p++;
            if (*p == '{') is_json = true;
        }
    } else if (json_input) {
        LOG_INFO("CLI", "Ingesting inline JSON formula (%zu bytes)", strlen(json_input));
        text_to_solve = json_input;
        is_json = true;
    } else if (dimacs_input) {
        LOG_INFO("CLI", "Ingesting inline DIMACS formula (%zu bytes)", strlen(dimacs_input));
        text_to_solve = dimacs_input;
        is_json = false;
    } else if (nlp_prompt) {
        const char* py_bin = (access(".venv/bin/python3", X_OK) == 0) ? ".venv/bin/python3" : "python3";
        LOG_INFO("NLP", "Routing natural language prompt via [%s]: '%.100s...'",
                 non_llm_mode ? "Classical Non-LLM" : "Pydantic LLM", nlp_prompt);
        printf("[*] Routing natural language specification to Python %s pipeline...\n",
               non_llm_mode ? "Classical Non-LLM" : "Pydantic LLM");
        char cmd[4096];
        snprintf(cmd, sizeof(cmd), "%s src/NLP/llm_router.py %s --text \"%s\" 2>/dev/null",
                 py_bin, non_llm_mode ? "--engine non-llm" : "", nlp_prompt);

        FILE* fp = popen(cmd, "r");
        if (!fp) {
            LOG_ERROR("NLP", "Failed to execute Python subprocess: %s", cmd);
            fprintf(stderr, "Error: Failed to execute src/NLP/llm_router.py\n");
            logger_shutdown();
            return 1;
        }
        content_to_free = (char*)malloc(32768);
        size_t nr = fread(content_to_free, 1, 32767, fp);
        content_to_free[nr] = '\0';
        pclose(fp);

        if (nr == 0) {
            LOG_ERROR("NLP", "Subprocess returned 0 bytes output");
            fprintf(stderr, "Error: No output from Python NLP router.\n");
            free(content_to_free);
            logger_shutdown();
            return 1;
        }
        LOG_INFO("NLP", "NLP router returned %zu bytes of JSON formula", nr);
        text_to_solve = content_to_free;
        is_json = true;
    } else {
        // Check if input was piped through stdin
#if !defined(_WIN32)
        if (!isatty(STDIN_FILENO)) {
            LOG_INFO("CLI", "Reading formula from stdin pipe");
            content_to_free = read_entire_stdin();
            if (content_to_free && strlen(content_to_free) > 0) {
                text_to_solve = content_to_free;
                const char* p = text_to_solve;
                while (*p && isspace((unsigned char)*p)) p++;
                if (*p == '{') is_json = true;
                LOG_INFO("CLI", "Piped stdin read successfully (%zu bytes, format: %s)",
                         strlen(text_to_solve), is_json ? "JSON" : "DIMACS");
            }
        }
#endif
    }

    if (!text_to_solve) {
        LOG_WARN("CLI", "No input formula provided via arguments or stdin");
        print_usage(argv[0]);
        logger_shutdown();
        return 1;
    }

    // Solve Problem
    LOG_INFO("SOLVER", "Invoking CDCL solver engine (%s format)", is_json ? "JSON" : "DIMACS");
    double t_start = get_time_ms_precise();
    char* json_response = NULL;
    int status_code = 0;

    if (is_json) {
        status_code = sodium_api_solve_json(text_to_solve, &json_response);
    } else {
        status_code = sodium_api_solve_dimacs(text_to_solve, &json_response);
    }
    double elapsed_ms = get_time_ms_precise() - t_start;

    const char* status_name = (status_code == 10) ? "SATISFIABLE" :
                              ((status_code == 20) ? "UNSATISFIABLE" : "UNKNOWN");
    LOG_INFO("SOLVER", "Solve finished in %.4f ms. Status code: %d (%s)",
             elapsed_ms, status_code, status_name);

    // Display Result
    if (json_response) {
        printf("%s\n", json_response);
        sodium_api_free_string(json_response);
    }

    if (benchmark_mode) {
        printf("\n================ [ Sodium Benchmark Telemetry ] ================\n");
        printf(" Status Code:      %d (%s)\n", status_code, status_name);
        printf(" Solve Latency:    %.4f ms (%.6f seconds)\n", elapsed_ms, elapsed_ms / 1000.0);
        printf("================================================================\n");
    }

    if (content_to_free) free(content_to_free);
    int exit_code = (status_code == 10) ? 0 : ((status_code == 20) ? 20 : 1);
    LOG_INFO("CORE", "Sodium execution terminating with exit code: %d", exit_code);
    logger_shutdown();
    return exit_code;
}
