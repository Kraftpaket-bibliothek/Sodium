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
"      --bg-white: #ffffff;\n"
"      --bg-light: #fafafa;\n"
"      --bg-accent: #f4f4f6;\n"
"      --border-black: #000000;\n"
"      --border-light: #e0e0e0;\n"
"      --text-main: #111111;\n"
"      --text-muted: #666666;\n"
"      --accent-orange: #d84a26;\n"
"      --sat-green: #1e7e34;\n"
"      --unsat-red: #c81e1e;\n"
"    }\n"
"    * { box-sizing: border-box; margin: 0; padding: 0; }\n"
"    body {\n"
"      background-color: var(--bg-white);\n"
"      color: var(--text-main);\n"
"      padding: 32px 40px;\n"
"      min-height: 100vh;\n"
"      font-family: -apple-system, BlinkMacSystemFont, \"Segoe UI\", Roboto, Helvetica, Arial, sans-serif;\n"
"      transition: background-color 0.2s ease, color 0.2s ease;\n"
"    }\n"
"    .container { max-width: 1300px; margin: 0 auto; }\n"
"    \n"
"    /* Header with wireframe brutalist styling */\n"
"    header {\n"
"      display: flex;\n"
"      align-items: center;\n"
"      justify-content: space-between;\n"
"      margin-bottom: 24px;\n"
"      padding-bottom: 18px;\n"
"      border-bottom: 2px solid var(--border-black);\n"
"    }\n"
"    .brand { display: flex; align-items: center; gap: 14px; }\n"
"    .logo-badge {\n"
"      background: #000000;\n"
"      color: #ffffff;\n"
"      width: 44px;\n"
"      height: 44px;\n"
"      border: 2px solid var(--border-black);\n"
"      display: flex;\n"
"      align-items: center;\n"
"      justify-content: center;\n"
"      font-weight: 700;\n"
"      font-size: 22px;\n"
"      font-family: monospace;\n"
"    }\n"
"    h1 {\n"
"      font-size: 30px;\n"
"      font-weight: 700;\n"
"      letter-spacing: -0.5px;\n"
"      color: var(--text-main);\n"
"      line-height: 1.15;\n"
"    }\n"
"    .subtitle {\n"
"      font-size: 14px;\n"
"      color: var(--text-muted);\n"
"      font-weight: 400;\n"
"      margin-top: 3px;\n"
"    }\n"
"    \n"
"    /* Half-Moon Contrast Toggle Button */\n"
"    .contrast-btn {\n"
"      width: 44px;\n"
"      height: 44px;\n"
"      border: 2px solid var(--border-black);\n"
"      background: var(--bg-white);\n"
"      color: var(--text-main);\n"
"      display: flex;\n"
"      align-items: center;\n"
"      justify-content: center;\n"
"      cursor: pointer;\n"
"      transition: all 0.2s ease;\n"
"    }\n"
"    .contrast-btn:hover {\n"
"      background: var(--text-main);\n"
"      color: var(--bg-white);\n"
"    }\n"
"    .contrast-icon {\n"
"      width: 22px;\n"
"      height: 22px;\n"
"      transition: transform 0.3s ease;\n"
"    }\n"
"\n"
"    /* Dark Contrast Mode Styles */\n"
"    body.dark-contrast {\n"
"      --bg-white: #121214;\n"
"      --bg-light: #1a1a1e;\n"
"      --bg-accent: #26262b;\n"
"      --border-black: #ffffff;\n"
"      --border-light: #38383e;\n"
"      --text-main: #ffffff;\n"
"      --text-muted: #9e9ea6;\n"
"      --sat-green: #34d399;\n"
"      --unsat-red: #f87171;\n"
"    }\n"
"    body.dark-contrast .contrast-icon {\n"
"      transform: rotate(180deg);\n"
"    }\n"
"    body.dark-contrast .btn-solve,\n"
"    body.dark-contrast .tab-btn.active,\n"
"    body.dark-contrast th {\n"
"      background: #ffffff;\n"
"      color: #000000;\n"
"      border-color: #ffffff;\n"
"    }\n"
"    body.dark-contrast .btn-solve:hover {\n"
"      background: #e0e0e0;\n"
"    }\n"
"    body.dark-contrast .tab-btn {\n"
"      background: #121214;\n"
"      color: #ffffff;\n"
"      border-color: #ffffff;\n"
"    }\n"
"    body.dark-contrast .logo-badge {\n"
"      background: #ffffff;\n"
"      color: #000000;\n"
"      border-color: #ffffff;\n"
"    }\n"
"    body.dark-contrast tr:nth-child(even) td {\n"
"      background: #161619;\n"
"    }\n"
"    body.dark-contrast .sample-select,\n"
"    body.dark-contrast .rule-input,\n"
"    body.dark-contrast textarea {\n"
"      background: #1a1a1e;\n"
"      color: #ffffff;\n"
"      border-color: #ffffff;\n"
"    }\n"
"\n"
"    /* Main Grid */\n"
"    .grid { display: grid; grid-template-columns: 1fr 1fr; gap: 28px; }\n"
"    @media(max-width: 960px) { .grid { grid-template-columns: 1fr; } }\n"
"\n"
"    /* Cards */\n"
"    .card {\n"
"      background: var(--bg-white);\n"
"      border: 2px solid var(--border-black);\n"
"      padding: 22px;\n"
"      display: flex;\n"
"      flex-direction: column;\n"
"      gap: 16px;\n"
"      min-height: 580px;\n"
"    }\n"
"\n"
"    /* Tabs */\n"
"    .tabs-bar {\n"
"      display: flex;\n"
"      justify-content: space-between;\n"
"      align-items: center;\n"
"      border-bottom: 2px solid var(--border-black);\n"
"      padding-bottom: 14px;\n"
"    }\n"
"    .tabs { display: flex; gap: 10px; }\n"
"    .tab-btn {\n"
"      background: var(--bg-white);\n"
"      border: 1.5px solid var(--border-black);\n"
"      color: var(--text-main);\n"
"      font-size: 13px;\n"
"      font-weight: 600;\n"
"      padding: 8px 16px;\n"
"      cursor: pointer;\n"
"      transition: all 0.15s ease;\n"
"      font-family: inherit;\n"
"    }\n"
"    .tab-btn.active {\n"
"      background: #000000;\n"
"      color: #ffffff;\n"
"      border-color: #000000;\n"
"    }\n"
"    .tab-btn:hover:not(.active) {\n"
"      background: var(--bg-accent);\n"
"    }\n"
"\n"
"    /* Sample selector dropdown */\n"
"    .sample-select {\n"
"      background: var(--bg-white);\n"
"      border: 1.5px solid var(--border-black);\n"
"      color: var(--text-main);\n"
"      padding: 6px 10px;\n"
"      font-size: 12px;\n"
"      font-family: monospace;\n"
"      outline: none;\n"
"      cursor: pointer;\n"
"    }\n"
"\n"
"    .editor-wrap {\n"
"      display: flex;\n"
"      flex-direction: column;\n"
"      gap: 8px;\n"
"      flex: 1;\n"
"      min-height: 320px;\n"
"    }\n"
"    textarea {\n"
"      width: 100%;\n"
"      height: 340px;\n"
"      background: var(--bg-light);\n"
"      border: 2px solid var(--border-black);\n"
"      color: var(--text-main);\n"
"      font-family: 'SF Mono', Menlo, Consolas, monospace;\n"
"      font-size: 13px;\n"
"      padding: 14px;\n"
"      resize: vertical;\n"
"      outline: none;\n"
"      line-height: 1.45;\n"
"    }\n"
"    textarea:focus {\n"
"      border-color: var(--accent-orange);\n"
"    }\n"
"\n"
"    /* Rule editor */\n"
"    #rule-editor-container {\n"
"      display: flex;\n"
"      flex-direction: column;\n"
"      gap: 10px;\n"
"      height: 340px;\n"
"      overflow-y: auto;\n"
"      background: var(--bg-light);\n"
"      padding: 14px;\n"
"      border: 2px solid var(--border-black);\n"
"    }\n"
"    .rule-row { display: flex; gap: 8px; align-items: center; }\n"
"    .rule-label {\n"
"      font-size: 12px;\n"
"      color: var(--text-muted);\n"
"      width: 54px;\n"
"      flex-shrink: 0;\n"
"      font-weight: 600;\n"
"      font-family: monospace;\n"
"    }\n"
"    .rule-input {\n"
"      flex: 1;\n"
"      background: var(--bg-white);\n"
"      border: 1.5px solid var(--border-black);\n"
"      color: var(--text-main);\n"
"      padding: 8px 12px;\n"
"      font-size: 13px;\n"
"      font-family: monospace;\n"
"      outline: none;\n"
"      transition: border-color 0.15s;\n"
"    }\n"
"    .rule-input:focus {\n"
"      border: 2px solid var(--accent-orange);\n"
"    }\n"
"    .rule-del-btn {\n"
"      background: transparent;\n"
"      border: 1.5px solid transparent;\n"
"      color: var(--unsat-red);\n"
"      cursor: pointer;\n"
"      font-size: 16px;\n"
"      padding: 4px 8px;\n"
"      line-height: 1;\n"
"      font-weight: 700;\n"
"    }\n"
"    .rule-del-btn:hover {\n"
"      border-color: var(--border-black);\n"
"      background: var(--bg-white);\n"
"    }\n"
"\n"
"    /* Buttons */\n"
"    .btn-solve {\n"
"      background: #000000;\n"
"      color: #ffffff;\n"
"      border: 2px solid var(--border-black);\n"
"      padding: 14px 20px;\n"
"      font-size: 14px;\n"
"      font-weight: 700;\n"
"      letter-spacing: 0.5px;\n"
"      cursor: pointer;\n"
"      display: flex;\n"
"      align-items: center;\n"
"      justify-content: center;\n"
"      gap: 8px;\n"
"      transition: all 0.15s ease;\n"
"      font-family: inherit;\n"
"    }\n"
"    .btn-solve:hover {\n"
"      background: #222222;\n"
"      transform: translateY(-1px);\n"
"    }\n"
"\n"
"    /* Right Card: Metrics & Results */\n"
"    .metrics-bar {\n"
"      display: grid;\n"
"      grid-template-columns: repeat(4, 1fr);\n"
"      gap: 12px;\n"
"    }\n"
"    .metric-card {\n"
"      background: var(--bg-light);\n"
"      border: 1.5px solid var(--border-black);\n"
"      padding: 12px 8px;\n"
"      text-align: center;\n"
"    }\n"
"    .metric-value {\n"
"      font-size: 18px;\n"
"      font-weight: 700;\n"
"      color: var(--text-main);\n"
"      font-family: monospace;\n"
"    }\n"
"    .metric-label {\n"
"      font-size: 11px;\n"
"      color: var(--text-muted);\n"
"      text-transform: uppercase;\n"
"      margin-top: 4px;\n"
"      font-weight: 600;\n"
"      letter-spacing: 0.5px;\n"
"    }\n"
"\n"
"    .results-panel {\n"
"      display: flex;\n"
"      flex-direction: column;\n"
"      gap: 12px;\n"
"      flex: 1;\n"
"      max-height: 420px;\n"
"      overflow-y: auto;\n"
"      border: 1.5px solid var(--border-black);\n"
"    }\n"
"    table {\n"
"      width: 100%;\n"
"      border-collapse: collapse;\n"
"      font-size: 13px;\n"
"      text-align: left;\n"
"    }\n"
"    th {\n"
"      background: #000000;\n"
"      color: #ffffff;\n"
"      padding: 10px 12px;\n"
"      font-weight: 600;\n"
"      position: sticky;\n"
"      top: 0;\n"
"      font-family: monospace;\n"
"      font-size: 12px;\n"
"      letter-spacing: 0.5px;\n"
"    }\n"
"    td {\n"
"      padding: 10px 12px;\n"
"      border-bottom: 1px solid var(--border-light);\n"
"    }\n"
"    tr:nth-child(even) td {\n"
"      background: var(--bg-light);\n"
"    }\n"
"    .pill-true {\n"
"      background: #e0f2e9;\n"
"      color: var(--sat-green);\n"
"      border: 1px solid var(--sat-green);\n"
"      padding: 2px 8px;\n"
"      font-weight: 700;\n"
"      font-size: 11px;\n"
"      font-family: monospace;\n"
"    }\n"
"    .pill-false {\n"
"      background: #fde8e8;\n"
"      color: var(--unsat-red);\n"
"      border: 1px solid var(--unsat-red);\n"
"      padding: 2px 8px;\n"
"      font-weight: 700;\n"
"      font-size: 11px;\n"
"      font-family: monospace;\n"
"    }\n"
"    .plan-panel {\n"
"      background: var(--bg-white);\n"
"      border: 1.5px solid var(--border-black);\n"
"      padding: 12px;\n"
"      display: flex;\n"
"      flex-direction: column;\n"
"      gap: 8px;\n"
"    }\n"
"    .plan-header {\n"
"      display: flex;\n"
"      justify-content: space-between;\n"
"      align-items: center;\n"
"      border-bottom: 1.5px solid var(--border-black);\n"
"      padding-bottom: 6px;\n"
"    }\n"
"    .plan-title {\n"
"      font-size: 12px;\n"
"      font-weight: 700;\n"
"      text-transform: uppercase;\n"
"      letter-spacing: 0.5px;\n"
"      color: var(--text-main);\n"
"      font-family: monospace;\n"
"    }\n"
"    .plan-grid {\n"
"      display: grid;\n"
"      grid-template-columns: 1fr 1fr;\n"
"      gap: 12px;\n"
"    }\n"
"    @media(max-width: 600px) { .plan-grid { grid-template-columns: 1fr; } }\n"
"    .plan-col {\n"
"      display: flex;\n"
"      flex-direction: column;\n"
"      gap: 6px;\n"
"    }\n"
"    .plan-col-title {\n"
"      font-size: 11px;\n"
"      font-weight: 700;\n"
"      text-transform: uppercase;\n"
"      letter-spacing: 0.5px;\n"
"      font-family: monospace;\n"
"    }\n"
"    .plan-col-title.active-title { color: var(--sat-green); }\n"
"    .plan-col-title.inactive-title { color: var(--text-muted); }\n"
"    .plan-items {\n"
"      display: flex;\n"
"      flex-direction: column;\n"
"      gap: 4px;\n"
"      max-height: 130px;\n"
"      overflow-y: auto;\n"
"    }\n"
"    .plan-item {\n"
"      font-size: 12px;\n"
"      padding: 5px 8px;\n"
"      border: 1px solid var(--border-light);\n"
"      background: var(--bg-light);\n"
"      display: flex;\n"
"      align-items: center;\n"
"      gap: 6px;\n"
"      font-family: monospace;\n"
"    }\n"
"    .plan-item.active { border-left: 3px solid var(--sat-green); }\n"
"    .plan-item.inactive { border-left: 3px solid var(--border-black); opacity: 0.8; }\n"
"    body.dark-contrast .plan-panel { background: #121214; border-color: #ffffff; }\n"
"    body.dark-contrast .plan-item { background: #1a1a1e; border-color: #333333; }\n"
"  </style>\n"
"</head>\n"
"<body>\n"
"  <div class=\"container\">\n"
"    <header>\n"
"      <div class=\"brand\">\n"
"        <div class=\"logo-badge\">Na</div>\n"
"        <div>\n"
"          <h1>Sodium Automated Reasoning Studio</h1>\n"
"          <div class=\"subtitle\">Neuro-Symbolic CDCL Automated Verification Engine &mdash; Pure Logic Rules</div>\n"
"        </div>\n"
"      </div>\n"
"      <button id=\"contrast-toggle\" class=\"contrast-btn\" title=\"Toggle Contrast Mode\" onclick=\"toggleContrast()\">\n"
"        <svg class=\"contrast-icon\" viewBox=\"0 0 24 24\" width=\"22\" height=\"22\">\n"
"          <circle cx=\"12\" cy=\"12\" r=\"10\" stroke=\"currentColor\" stroke-width=\"2\" fill=\"none\" />\n"
"          <path d=\"M 12 2 A 10 10 0 0 0 12 22 Z\" fill=\"currentColor\" />\n"
"        </svg>\n"
"      </button>\n"
"    </header>\n"
"\n"
"    <div class=\"grid\">\n"
"      <!-- LEFT CARD: Input Editor & Rules -->\n"
"      <div class=\"card\">\n"
"        <div class=\"tabs-bar\">\n"
"          <div class=\"tabs\">\n"
"            <button id=\"tab-nlp\" class=\"tab-btn active\" onclick=\"setMode('nlp')\">Natural Language (NLP)</button>\n"
"            <button id=\"tab-dimacs\" class=\"tab-btn\" onclick=\"setMode('dimacs')\">DIMACS CNF</button>\n"
"          </div>\n"
"        </div>\n"
"\n"
"        <div id=\"nlp-engine-controls\" style=\"display:flex;align-items:center;justify-content:space-between;padding:2px 0;\">\n"
"          <span style=\"font-size:12px;color:var(--text-muted);font-family:monospace;\">Classical NLP Specification &amp; Rule Builder</span>\n"
"        </div>\n"
"\n"
"        <div class=\"editor-wrap\">\n"
"          <textarea id=\"code-editor\" spellcheck=\"false\" placeholder=\"Enter DIMACS CNF here...\" style=\"display:none;\"></textarea>\n"
"          \n"
"          <div id=\"rule-editor-container\">\n"
"            <div style=\"font-size:12px; color:var(--text-muted); display:flex; justify-content:space-between; align-items:center; border-bottom:1.5px solid var(--border-black); padding-bottom:8px;\">\n"
"              <span style=\"font-family:monospace;font-weight:600;\">Separate Rules (One per line):</span>\n"
"              <button type=\"button\" class=\"tab-btn\" style=\"padding:3px 10px; font-size:11px;\" onclick=\"addRuleRow('')\">+ Add Rule</button>\n"
"            </div>\n"
"            <div id=\"rule-rows\" style=\"display:flex; flex-direction:column; gap:8px;\"></div>\n"
"          </div>\n"
"        </div>\n"
"\n"
"        <button class=\"btn-solve\" onclick=\"triggerSolve()\">Solve with Sodium CDCL</button>\n"
"      </div>\n"
"\n"
"      <!-- RIGHT CARD: Metrics & Proof Assignments -->\n"
"      <div class=\"card\">\n"
"        <div class=\"metrics-bar\">\n"
"          <div class=\"metric-card\"><div class=\"metric-value\" id=\"m-time\">0.00 ms</div><div class=\"metric-label\">Latency</div></div>\n"
"          <div class=\"metric-card\"><div class=\"metric-value\" id=\"m-vars\">0</div><div class=\"metric-label\">Variables</div></div>\n"
"          <div class=\"metric-card\"><div class=\"metric-value\" id=\"m-clauses\">0</div><div class=\"metric-label\">Clauses</div></div>\n"
"          <div class=\"metric-card\"><div class=\"metric-value\" id=\"m-code\">-</div><div class=\"metric-label\">Status Code</div></div>\n"
"        </div>\n"
"\n"
"        <!-- Executive Action Plan -->\n"
"        <div id=\"plan-panel\" class=\"plan-panel\" style=\"display:none;\">\n"
"          <div class=\"plan-header\">\n"
"            <span class=\"plan-title\">Operational Execution Plan</span>\n"
"            <span class=\"pill-true\" style=\"font-size:10px;\">PROVEN SATISFIABLE</span>\n"
"          </div>\n"
"          <div class=\"plan-grid\">\n"
"            <div class=\"plan-col\">\n"
"              <div class=\"plan-col-title active-title\">&#x2713; Recommended Actions &amp; Active States</div>\n"
"              <div id=\"plan-active-items\" class=\"plan-items\"></div>\n"
"            </div>\n"
"            <div class=\"plan-col\">\n"
"              <div class=\"plan-col-title inactive-title\">&#x2715; Suppressed / Inactive Conditions</div>\n"
"              <div id=\"plan-inactive-items\" class=\"plan-items\"></div>\n"
"            </div>\n"
"          </div>\n"
"        </div>\n"
"\n"
"        <div style=\"display:flex; justify-content:space-between; align-items:center; margin-top:2px;\">\n"
"          <span style=\"font-size:12px; font-weight:600; font-family:monospace; color:var(--text-muted);\">Execution Directives &amp; System States:</span>\n"
"          <button id=\"toggle-tseitin-btn\" type=\"button\" class=\"tab-btn\" style=\"padding:2px 8px; font-size:11px; display:none;\" onclick=\"toggleTseitinVars()\">+ Show Proof Variables (0)</button>\n"
"        </div>\n"
"\n"
"        <div class=\"results-panel\">\n"
"          <table id=\"results-table\">\n"
"            <thead><tr><th>Condition / System State</th><th style=\"width:100px;\">Truth Value</th><th style=\"width:180px;\">Operational Directive</th></tr></thead>\n"
"            <tbody id=\"results-body\"><tr><td colspan=\"3\" style=\"text-align:center;color:var(--text-muted);padding:32px;font-family:monospace;\">Submit a problem to view CDCL assignments.</td></tr></tbody>\n"
"          </table>\n"
"        </div>\n"
"      </div>\n"
"    </div>\n"
"  </div>\n"
"\n"
"  <script>\n"
"    let currentMode = 'nlp';\n"
"\n"
"    function toggleContrast() {\n"
"      document.body.classList.toggle('dark-contrast');\n"
"      try {\n"
"        const isDark = document.body.classList.contains('dark-contrast');\n"
"        localStorage.setItem('sodium-contrast', isDark ? 'dark' : 'light');\n"
"      } catch (e) {}\n"
"    }\n"
"\n"
"    function initRuleRowsFromText(text) {\n"
"      const container = document.getElementById('rule-rows');\n"
"      container.innerHTML = '';\n"
"      let lines = text ? text.split(/[.;\\n]+/).map(s => s.trim()).filter(s => s.length > 0) : [];\n"
"      if (lines.length === 0) {\n"
"        lines = [\n"
"          'NOT (PrimaryServer AND StandbyServer)',\n"
"          'PrimaryServer OR StandbyServer',\n"
"          'IF MaintenanceMode THEN NOT PrimaryServer',\n"
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
"        <input type=\"text\" class=\"rule-input\" placeholder=\"e.g. NOT (PrimaryServer AND StandbyServer)\" value=\"${cleanVal}\" onkeydown=\"handleRuleKeyDown(event, this)\" />\n"
"        <button type=\"button\" class=\"rule-del-btn\" title=\"Delete rule\" onclick=\"removeRuleRow(this)\">&#x2715;</button>\n"
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
"    function getClassicalRuleText() {\n"
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
"      const ruleEditor = document.getElementById('rule-editor-container');\n"
"      if (mode === 'nlp') {\n"
"        if (nlpControls) nlpControls.style.display = 'flex';\n"
"        editor.style.display = 'none';\n"
"        ruleEditor.style.display = 'flex';\n"
"        if (document.getElementById('rule-rows').children.length === 0) {\n"
"          initRuleRowsFromText('NOT (PrimaryServer AND StandbyServer). PrimaryServer OR StandbyServer. IF MaintenanceMode THEN NOT PrimaryServer. MaintenanceMode.');\n"
"        }\n"
"      } else if (mode === 'dimacs') {\n"
"        if (nlpControls) nlpControls.style.display = 'none';\n"
"        ruleEditor.style.display = 'none';\n"
"        editor.style.display = 'block';\n"
"        if (!editor.value || editor.value.startsWith('PrimaryServer')) {\n"
"          editor.value = 'c High-Availability Dual-Server Split-Brain Prevention Policy\\\\np cnf 3 4\\\\n-1 -2 0\\\\n1 2 0\\\\n-3 -1 0\\\\n3 0';\n"
"        }\n"
"      }\n"
"    }\n"
"\n"
"    function loadPreset(name) {\n"
"      if (!name) return;\n"
"      if (name === 'zebra') {\n"
"        setMode('dimacs');\n"
"        document.getElementById('code-editor').value = \n"
"          'c Zebra Puzzle: Einstein Riddle (125 variables, 5 houses)\\\\n' +\n"
"          'c Categories: Color (1-25), Nation (26-50), Drink (51-75), Smoke (76-100), Pet (101-125)\\\\n' +\n"
"          'p cnf 125 15\\\\n' +\n"
"          'c Clue 8: Milk drunk in middle house 3 (var 63)\\\\n63 0\\\\n' +\n"
"          'c Clue 9: Norwegian lives in first house 1 (var 41)\\\\n41 0\\\\n' +\n"
"          'c Clue 14: Norwegian next to blue house -> Blue in house 2 (var 22)\\\\n22 0\\\\n' +\n"
"          'c Clue 7: Kools in yellow house -> Yellow in house 1 (var 16), Kools in house 1 (var 81)\\\\n16 0\\\\n81 0\\\\n' +\n"
"          'c Clue 11: Horse next to Kools -> Horse in house 2 (var 117)\\\\n117 0\\\\n' +\n"
"          'c Clue 4: Ukrainian drinks tea -> Tea in house 2 (var 57), Ukrainian in house 2 (var 37)\\\\n57 0\\\\n37 0\\\\n' +\n"
"          'c Clue 1: Englishman in red house -> Red in house 3 (var 3), Englishman in house 3 (var 28)\\\\n3 0\\\\n28 0\\\\n' +\n"
"          'c Clue 5: Ivory in house 4 (var 14), Green in house 5 (var 10)\\\\n14 0\\\\n10 0\\\\n' +\n"
"          'c Clue 3: Coffee in green house -> Coffee in house 5 (var 55)\\\\n55 0\\\\n' +\n"
"          'c Deductions: Norwegian drinks water (var 71), Japanese owns zebra (var 125)\\\\n71 0\\\\n125 0';\n"
"      } else if (name === 'server') {\n"
"        setMode('nlp');\n"
"        initRuleRowsFromText('NOT (PrimaryServer AND StandbyServer). PrimaryServer OR StandbyServer. IF MaintenanceMode THEN NOT PrimaryServer. MaintenanceMode.');\n"
"      } else if (name === 'alu') {\n"
"        setMode('dimacs');\n"
"        document.getElementById('code-editor').value =\n"
"          'c TI 74181 ALU Combinational Equivalence Miter (Addition mode M=0, S=1001)\\\\nc Golden Adder XOR 74181 Output = Difference Miter\\\\np cnf 5 6\\\\n-1 2 0\\\\n-2 3 0\\\\n-3 4 0\\\\n-4 5 0\\\\n1 0\\\\n-5 0';\n"
"      } else if (name === 'queens') {\n"
"        setMode('dimacs');\n"
"        document.getElementById('code-editor').value =\n"
"          'c 4-Queens Problem (16 variables: q_row_col)\\\\np cnf 16 12\\\\n1 2 3 4 0\\\\n5 6 7 8 0\\\\n9 10 11 12 0\\\\n13 14 15 16 0\\\\n-1 -2 0\\\\n-1 -5 0\\\\n-1 -6 0\\\\n-2 -6 0\\\\n-2 -7 0\\\\n-3 -7 0\\\\n-3 -8 0\\\\n-4 -8 0';\n"
"      } else if (name === 'estop') {\n"
"        setMode('nlp');\n"
"        initRuleRowsFromText('ChannelA_Healthy. NOT ChannelB_Healthy. IF (ChannelA_Healthy AND ChannelB_Healthy) THEN SafeOperation. IF NOT SafeOperation THEN ActuatorCutoff.');\n"
"      }\n"
"    }\n"
"\n"
"    async function triggerSolve() {\n"
"      let text = '';\n"
"      if (currentMode === 'nlp') {\n"
"        text = getClassicalRuleText();\n"
"      } else {\n"
"        text = document.getElementById('code-editor').value;\n"
"      }\n"
"      if (!text.trim()) {\n"
"        alert('Please enter a formula or specification to solve.');\n"
"        return;\n"
"      }\n"
"      document.getElementById('m-code').innerText = 'SOLVING...';\n"
"      let endpoint = (currentMode === 'nlp') ? '/api/v1/nlp-solve' : '/api/v1/dimacs';\n"
"      let body = (currentMode === 'nlp') ? JSON.stringify({ prompt: text }) : text;\n"
"      let headers = { 'Content-Type': (currentMode === 'nlp') ? 'application/json' : 'text/plain' };\n"
"      try {\n"
"        const res = await fetch(endpoint, { method: 'POST', headers, body });\n"
"        const data = await res.json();\n"
"        renderResults(data);\n"
"      } catch (err) {\n"
"        document.getElementById('m-code').innerText = 'ERR';\n"
"        alert('Error connecting to Sodium solver: ' + err);\n"
"      }\n"
"    }\n"
"\n"
"    let showTseitin = false;\n"
"\n"
"    function isInternalVar(name) {\n"
"      if (!name) return false;\n"
"      const lower = name.toLowerCase();\n"
"      return lower.startsWith('__tseitin') || lower.startsWith('autovar_') || lower.includes('tseitin');\n"
"    }\n"
"\n"
"    function formatPhrase(raw) {\n"
"      if (!raw) return '';\n"
"      let s = raw.trim();\n"
"      s = s.replace(/^Variable\\s*#\\s*/i, '');\n"
"      s = s.replace(/_/g, ' ');\n"
"      if (s.length > 0) {\n"
"        s = s.charAt(0).toUpperCase() + s.slice(1);\n"
"      }\n"
"      return s;\n"
"    }\n"
"\n"
"    function getOperationalDirective(phrase, val) {\n"
"      const clean = phrase.trim();\n"
"      let m = clean.match(/^(.+?)\\s+(?:is\\s+in|in|at)\\s+(.+)$/i);\n"
"      if (m) {\n"
"        return val ? '&#x2713; Place ' + m[1] + ' in ' + m[2] : '&#x2715; Exclude ' + m[1] + ' from ' + m[2];\n"
"      }\n"
"      m = clean.match(/^(.+?)\\s+(drinks?|operates?|keeps?|owns?|smokes?|eats?|runs?|requires?)\\s+(.+)$/i);\n"
"      if (m) {\n"
"        return val ? ('&#x2713; ' + clean) : ('&#x2715; Prohibit: ' + clean);\n"
"      }\n"
"      m = clean.match(/^(.+?)\\s+(?:is|are|was|were)\\s+(.+)$/i);\n"
"      if (m) {\n"
"        const subj = m[1].charAt(0).toUpperCase() + m[1].slice(1);\n"
"        const pred = m[2];\n"
"        if (val) {\n"
"          if (/^(online|offline|active|inactive|ready|locked|unlocked|verified|open|closed|established)$/i.test(pred)) {\n"
"            return '&#x2713; Set ' + subj + ' to ' + pred.toUpperCase();\n"
"          }\n"
"          return '&#x2713; Ensure ' + subj + ' is ' + pred;\n"
"        } else {\n"
"          if (/^(online|offline|active|inactive|ready|locked|unlocked|verified|open|closed|established)$/i.test(pred)) {\n"
"            return '&#x2715; Keep ' + subj + ' NOT ' + pred.toUpperCase();\n"
"          }\n"
"          return '&#x2715; Ensure ' + subj + ' is NOT ' + pred;\n"
"        }\n"
"      }\n"
"      return val ? ('&#x2713; Enforce: ' + clean) : ('&#x2715; Prevent: ' + clean);\n"
"    }\n"
"\n"
"    function toggleTseitinVars() {\n"
"      showTseitin = !showTseitin;\n"
"      document.querySelectorAll('.tseitin-row').forEach(row => {\n"
"        row.style.display = showTseitin ? '' : 'none';\n"
"      });\n"
"      const btn = document.getElementById('toggle-tseitin-btn');\n"
"      if (btn) {\n"
"        const count = btn.getAttribute('data-count') || '0';\n"
"        btn.innerText = showTseitin ? `- Hide Proof Variables (${count})` : `+ Show Proof Variables (${count})`;\n"
"      }\n"
"    }\n"
"\n"
"    function renderResults(data) {\n"
"      const resp = data.response || {};\n"
"      const planPanel = document.getElementById('plan-panel');\n"
"      const activeContainer = document.getElementById('plan-active-items');\n"
"      const inactiveContainer = document.getElementById('plan-inactive-items');\n"
"      const toggleBtn = document.getElementById('toggle-tseitin-btn');\n"
"\n"
"      if (data.error) {\n"
"        document.getElementById('m-code').innerText = 'ERROR';\n"
"        document.getElementById('results-body').innerHTML = `<tr><td colspan=\"3\" style=\"text-align:center;padding:24px;color:#c81e1e;font-family:monospace;\"><strong>Error:</strong> ${data.error} ${data.details ? '<br/><small>' + data.details + '</small>' : ''}</td></tr>`;\n"
"        if (planPanel) planPanel.style.display = 'none';\n"
"        if (toggleBtn) toggleBtn.style.display = 'none';\n"
"        return;\n"
"      }\n"
"      const status = resp.status || 'UNKNOWN';\n"
"      const stats = resp.statistics || {};\n"
"      document.getElementById('m-time').innerText = (stats.solve_time_ms ? stats.solve_time_ms.toFixed(3) : '0.00') + ' ms';\n"
"      document.getElementById('m-vars').innerText = stats.variables || 0;\n"
"      document.getElementById('m-clauses').innerText = stats.clauses || 0;\n"
"      document.getElementById('m-code').innerText = status === 'SATISFIABLE' ? 'SAT' : (status === 'UNSATISFIABLE' ? 'UNSAT' : (resp.status_code || 0));\n"
"      const tbody = document.getElementById('results-body');\n"
"      tbody.innerHTML = '';\n"
"\n"
"      if (status === 'UNSATISFIABLE') {\n"
"        if (planPanel) {\n"
"          planPanel.style.display = 'block';\n"
"          planPanel.innerHTML = `\n"
"            <div class=\"plan-header\" style=\"border-bottom-color:var(--unsat-red);\">\n"
"              <span class=\"plan-title\" style=\"color:var(--unsat-red);\">Contradiction Detected</span>\n"
"              <span class=\"pill-false\">UNSATISFIABLE</span>\n"
"            </div>\n"
"            <div style=\"font-size:12px; padding:10px 0; color:var(--text-main); font-family:monospace;\">\n"
"              The provided constraints form a direct logical conflict. Zero valid configurations satisfy all rules simultaneously.\n"
"            </div>\n"
"          `;\n"
"        }\n"
"        if (toggleBtn) toggleBtn.style.display = 'none';\n"
"        tbody.innerHTML = `<tr><td colspan=\"3\" style=\"text-align:center;padding:24px;color:var(--text-muted);font-family:monospace;\">Contradiction found. Zero satisfying assignments exist.</td></tr>`;\n"
"        return;\n"
"      }\n"
"\n"
"      const assign = resp.assignment || {};\n"
"      const keys = Object.keys(assign);\n"
"      if (keys.length === 0) {\n"
"        if (planPanel) planPanel.style.display = 'none';\n"
"        if (toggleBtn) toggleBtn.style.display = 'none';\n"
"        tbody.innerHTML = `<tr><td colspan=\"3\" style=\"text-align:center;padding:24px;color:var(--text-muted);font-family:monospace;\">No assignments returned.</td></tr>`;\n"
"        return;\n"
"      }\n"
"\n"
"      const vmap = (data.metadata && data.metadata.variable_mapping) || {};\n"
"      let tseitinCount = 0;\n"
"      let activePlanHtml = '';\n"
"      let inactivePlanHtml = '';\n"
"      showTseitin = false;\n"
"\n"
"      for (const k of keys) {\n"
"        const val = assign[k];\n"
"        const rawName = vmap[k] || k;\n"
"        const isTseitin = isInternalVar(rawName) || isInternalVar(k);\n"
"        const phrase = formatPhrase(rawName);\n"
"        const directive = getOperationalDirective(phrase, val);\n"
"        const pill = val ? '<span class=\"pill-true\">TRUE</span>' : '<span class=\"pill-false\">FALSE</span>';\n"
"\n"
"        if (isTseitin) {\n"
"          tseitinCount++;\n"
"          tbody.innerHTML += `\n"
"            <tr class=\"tseitin-row\" style=\"display:none; opacity:0.75;\">\n"
"              <td><span style=\"font-family:monospace; color:var(--text-muted); font-size:12px;\">${phrase}</span></td>\n"
"              <td>${pill}</td>\n"
"              <td><span style=\"font-size:11px; font-family:monospace; color:var(--text-muted);\">Proof Auxiliary Gate</span></td>\n"
"            </tr>\n"
"          `;\n"
"        } else {\n"
"          tbody.innerHTML += `\n"
"            <tr>\n"
"              <td><strong style=\"font-family:monospace; font-size:13px;\">${phrase}</strong></td>\n"
"              <td>${pill}</td>\n"
"              <td><span style=\"font-family:monospace; font-size:11px;\">${directive}</span></td>\n"
"            </tr>\n"
"          `;\n"
"          if (val) {\n"
"            activePlanHtml += `<div class=\"plan-item active\"><span style=\"color:var(--sat-green);font-weight:bold;\">&#x2713;</span> <strong>${phrase}</strong></div>`;\n"
"          } else {\n"
"            inactivePlanHtml += `<div class=\"plan-item inactive\"><span style=\"color:var(--unsat-red);font-weight:bold;\">&#x2715;</span> <span>${phrase}</span></div>`;\n"
"          }\n"
"        }\n"
"      }\n"
"\n"
"      if (planPanel) {\n"
"        planPanel.style.display = 'block';\n"
"        if (activeContainer) activeContainer.innerHTML = activePlanHtml || '<div style=\"font-size:11px; color:var(--text-muted);\">No active directives.</div>';\n"
"        if (inactiveContainer) inactiveContainer.innerHTML = inactivePlanHtml || '<div style=\"font-size:11px; color:var(--text-muted);\">No inactive conditions.</div>';\n"
"      }\n"
"\n"
"      if (toggleBtn) {\n"
"        if (tseitinCount > 0) {\n"
"          toggleBtn.style.display = 'inline-block';\n"
"          toggleBtn.setAttribute('data-count', tseitinCount);\n"
"          toggleBtn.innerText = `+ Show Proof Variables (${tseitinCount})`;\n"
"        } else {\n"
"          toggleBtn.style.display = 'none';\n"
"        }\n"
"      }\n"
"    }\n"
"\n"
"    window.onload = function() {\n"
"      try {\n"
"        if (localStorage.getItem('sodium-contrast') === 'dark') {\n"
"          document.body.classList.add('dark-contrast');\n"
"        }\n"
"      } catch (e) {}\n"
"      setMode('nlp');\n"
"    };\n"
"  </script>\n"
"</body>\n"
"</html>\n";

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

            LOG_INFO("NLP", "POST /api/v1/nlp-solve: Classical pipeline, Prompt length: %zu bytes",
                     strlen(extracted_prompt));

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
                snprintf(cmd, sizeof(cmd), "%s -m src.NLP.NLPAlgo.classical_pipeline --file \"%s\" 2>/tmp/sodium_nlp_err.log",
                         py_bin, tmp_path);

                FILE* fp = popen(cmd, "r");
                if (!fp) {
                    LOG_ERROR("NLP", "Failed to execute Python subprocess: %s", cmd);
                    unlink(tmp_path);
                    const char* err = "{\"error\":\"Failed to invoke Classical NLP pipeline\"}\n";
                    send_http_response(client_fd, 500, "Internal Server Error", "application/json", err, strlen(err));
                } else {
                    char nlp_json[65536] = {0};
                    size_t nread = fread(nlp_json, 1, sizeof(nlp_json) - 1, fp);
                    nlp_json[nread] = '\0';
                    int pcode = pclose(fp);
                    unlink(tmp_path);

                    if (nread > 0) {
                        LOG_INFO("NLP", "Classical NLP pipeline returned %zu bytes (Exit: %d)", nread, pcode);
                        char* out_json = NULL;
                        sodium_api_solve_json(nlp_json, &out_json);
                        if (out_json) {
                            send_http_response(client_fd, 200, "OK", "application/json", out_json, strlen(out_json));
                            sodium_api_free_string(out_json);
                        } else {
                            send_http_response(client_fd, 200, "OK", "application/json", nlp_json, strlen(nlp_json));
                        }
                    } else {
                        LOG_WARN("NLP", "Classical NLP pipeline returned 0 bytes (Exit: %d)", pcode);
                        FILE* err_fp = fopen("/tmp/sodium_nlp_err.log", "r");
                        char err_buf[512] = {0};
                        if (err_fp) {
                            size_t er = fread(err_buf, 1, sizeof(err_buf) - 1, err_fp);
                            err_buf[er] = '\0';
                            fclose(err_fp);
                        }
                        char err_resp[1024];
                        snprintf(err_resp, sizeof(err_resp),
                                 "{\"error\":\"Classical NLP pipeline returned no output\",\"details\":\"%.200s\"}\n",
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
    printf("  -n, --nlp <prompt>       Translate natural language via Classical NLP pipeline and solve\n");
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
        } else if ((strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--log") == 0) && i + 1 < argc) {
            i++; // already handled in pre-scan
        } else {
            fprintf(stderr, "Error: Unsupported or incomplete option '%s'. Use --help to see supported options.\n", argv[i]);
            return 2;
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
        LOG_INFO("NLP", "Compiling natural language prompt with Classical NLP: '%.100s...'", nlp_prompt);
        printf("[*] Compiling natural language specification with Classical NLP pipeline...\n");
        char prompt_path[] = "/tmp/sodium_cli_prompt_XXXXXX";
        int prompt_fd = mkstemp(prompt_path);
        if (prompt_fd < 0 || write(prompt_fd, nlp_prompt, strlen(nlp_prompt)) < 0) {
            if (prompt_fd >= 0) { close(prompt_fd); unlink(prompt_path); }
            fprintf(stderr, "Error: Failed to create temporary NLP prompt file.\n");
            logger_shutdown();
            return 1;
        }
        close(prompt_fd);
        char cmd[4096];
        snprintf(cmd, sizeof(cmd), "%s -m src.NLP.NLPAlgo.classical_pipeline --file \"%s\" 2>/dev/null",
                 py_bin, prompt_path);

        FILE* fp = popen(cmd, "r");
        if (!fp) {
            LOG_ERROR("NLP", "Failed to execute Python subprocess: %s", cmd);
            unlink(prompt_path);
            fprintf(stderr, "Error: Failed to execute Classical NLP pipeline.\n");
            logger_shutdown();
            return 1;
        }
        content_to_free = (char*)malloc(32768);
        size_t nr = fread(content_to_free, 1, 32767, fp);
        content_to_free[nr] = '\0';
        pclose(fp);
        unlink(prompt_path);

        if (nr == 0) {
            LOG_ERROR("NLP", "Subprocess returned 0 bytes output");
            fprintf(stderr, "Error: No output from Classical NLP pipeline.\n");
            free(content_to_free);
            logger_shutdown();
            return 1;
        }
        LOG_INFO("NLP", "Classical NLP pipeline returned %zu bytes of JSON formula", nr);
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
