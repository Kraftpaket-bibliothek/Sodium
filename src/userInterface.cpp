#include <imgui.h>
#include <vector>
#include <string>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <chrono>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <memory>
#include <functional>

// ============================================================================
// SECTION 1: SODIUM API PLUGIN INTERFACE
// ============================================================================
// These hooks represent the contract with the upcoming API.h / API.c.
// When you define API.h and implement API.c, connect the calls inside these
// functions. The UI provides interactive fallback mock engines out-of-the-box
// so the interface is fully runnable and demonstrable immediately.
// ============================================================================

namespace SodiumPlugin {

    enum class SolverStatus {
        IDLE,
        RUNNING,
        SATISFIABLE,
        UNSATISFIABLE,
        INTERRUPTED,
        ERROR_STATE
    };

    struct SolverParams {
        float vsids_decay = 0.95f;
        int restart_base = 100;
        float restart_multiplier = 1.5f;
        float compaction_threshold = 0.30f;
        int polarity_mode = 0; // 0 = Phase Saving, 1 = Default False, 2 = Default True
        bool enable_2wl_compaction = true;
    };

    struct TelemetryData {
        int variable_count = 0;
        int original_clause_count = 0;
        int learned_clause_count = 0;
        int decisions_count = 0;
        int conflicts_count = 0;
        int propagations_count = 0;
        int restarts_count = 0;
        int memory_used_slots = 0;
        int memory_wasted_slots = 0;
        double solve_time_ms = 0.0;
        std::vector<float> vsids_activities;
    };

    struct TrailItem {
        int decision_level;
        int literal;
        std::string reason_clause;
        bool is_decision;
    };

    struct VariableAssignment {
        int variable;
        int value; // 1 = True, -1 = False, 0 = Unassigned
        int decision_level;
        std::string explanation;
    };

    struct NLPExtractedConstraint {
        std::string natural_clause;
        std::string first_order_logic;
        std::string compiled_cnf;
        bool is_hard_constraint;
    };

    struct SolverResults {
        SolverStatus status = SolverStatus::IDLE;
        double execution_time_ms = 0.0;
        std::vector<VariableAssignment> model;
        std::vector<TrailItem> trail;
        std::vector<std::string> unsat_core;
    };

    struct NLPResults {
        SolverStatus status = SolverStatus::IDLE;
        double parsing_time_ms = 0.0;
        double reasoning_time_ms = 0.0;
        std::vector<NLPExtractedConstraint> extracted_constraints;
        std::vector<std::string> synthesized_plan;
        std::vector<std::string> contradiction_explanation;
    };

    // --- FUTURE API PLUGIN SLOTS ---
    // Plug in your API.h / API.c functions here.

    inline bool API_SolveSAT(const char* cnf_text, const SolverParams& params,
                             SolverResults& out_results, TelemetryData& out_telemetry) {
        // [PLUGIN SLOT]: Replace with:
        // return sodium_api_solve_dimacs(cnf_text, &params, &out_results, &out_telemetry);

        (void)params;
        auto start = std::chrono::high_resolution_clock::now();

        out_results.model.clear();
        out_results.trail.clear();
        out_results.unsat_core.clear();

        if (!cnf_text || strlen(cnf_text) == 0) {
            out_results.status = SolverStatus::ERROR_STATE;
            return false;
        }

        // Lightweight parser simulation for fallback demonstration
        std::istringstream stream(cnf_text);
        std::string line;
        int max_var = 0;
        int clauses = 0;

        while (std::getline(stream, line)) {
            if (line.empty() || line[0] == 'c') continue;
            if (line[0] == 'p') {
                std::string token, format;
                std::istringstream header(line);
                header >> token >> format >> max_var >> clauses;
                continue;
            }
            std::istringstream lstream(line);
            int lit;
            while (lstream >> lit && lit != 0) {
                max_var = std::max(max_var, std::abs(lit));
            }
            clauses++;
        }

        if (max_var == 0) max_var = 8;
        if (clauses == 0) clauses = 12;

        out_telemetry.variable_count = max_var;
        out_telemetry.original_clause_count = clauses;
        out_telemetry.learned_clause_count = std::max(1, clauses / 3);
        out_telemetry.decisions_count = max_var * 2 + 5;
        out_telemetry.conflicts_count = std::max(2, clauses / 4);
        out_telemetry.propagations_count = max_var * 6 + 18;
        out_telemetry.restarts_count = 2;
        out_telemetry.memory_used_slots = (max_var * 4) + (clauses * 3);
        out_telemetry.memory_wasted_slots = max_var / 2;

        out_telemetry.vsids_activities.resize(max_var);
        for (int i = 0; i < max_var; ++i) {
            out_telemetry.vsids_activities[i] = std::sin((float)i * 0.7f) * 12.0f + 15.0f;
        }

        // Generate plausible trail
        for (int v = 1; v <= max_var; ++v) {
            int level = (v <= 2) ? 0 : (v % 3);
            int val = (v % 2 == 0) ? 1 : -1;
            out_results.trail.push_back({
                level,
                val * v,
                (level == 0) ? "Unit Clause C" + std::to_string(v) : (v % 3 == 0 ? "Decision" : "Propagated via C" + std::to_string(v + 2)),
                (v % 3 == 0)
            });

            out_results.model.push_back({
                v,
                val,
                level,
                (val > 0) ? "Satisfied primary literal" : "Satisfied negated literal"
            });
        }

        auto end = std::chrono::high_resolution_clock::now();
        out_results.execution_time_ms = std::chrono::duration<double, std::milli>(end - start).count();
        out_telemetry.solve_time_ms = out_results.execution_time_ms;
        out_results.status = SolverStatus::SATISFIABLE;

        return true;
    }

    inline bool API_SolveNLP(const char* nl_prose, const SolverParams& params,
                             NLPResults& out_results, TelemetryData& out_telemetry) {
        // [PLUGIN SLOT]: Replace with:
        // return sodium_api_solve_nlp(nl_prose, &params, &out_results, &out_telemetry);

        (void)params;
        auto start = std::chrono::high_resolution_clock::now();

        out_results.extracted_constraints.clear();
        out_results.synthesized_plan.clear();
        out_results.contradiction_explanation.clear();

        if (!nl_prose || strlen(nl_prose) == 0) {
            out_results.status = SolverStatus::ERROR_STATE;
            return false;
        }

        out_results.extracted_constraints.push_back({
            "Primary database must stay replicated to secondary node with under 50ms latency.",
            "ReplicationLatency(Primary, Secondary) <= 50ms => ActiveSync(Primary, Secondary)",
            "(not Latency_Under_50ms or ActiveSync_Enabled)",
            true
        });
        out_results.extracted_constraints.push_back({
            "If node failover is initiated, read-only traffic must be diverted to backup cluster.",
            "FailoverInitiated => RouteReadOnlyTraffic(BackupCluster)",
            "(not Failover_State or Backup_Cluster_Active)",
            true
        });
        out_results.extracted_constraints.push_back({
            "No simultaneous split-brain election is permitted during partition.",
            "forall n1, n2 in Nodes: Leader(n1) and Leader(n2) => n1 = n2",
            "(not Leader_NodeA or not Leader_NodeB)",
            true
        });

        out_results.synthesized_plan.push_back("Action 1: Designate Node-1 as PRIMARY with quorum lease granted (Lease ID: 0x7E3A).");
        out_results.synthesized_plan.push_back("Action 2: Activate synchronous write journal replication to Node-2 (Latency threshold met: 12ms).");
        out_results.synthesized_plan.push_back("Action 3: Route all read-only analytics queries to Node-3 replica pool.");
        out_results.synthesized_plan.push_back("Action 4: Lock failover election token preventing dual-master split-brain invariants.");

        out_telemetry.variable_count = 14;
        out_telemetry.original_clause_count = 28;
        out_telemetry.learned_clause_count = 6;
        out_telemetry.decisions_count = 18;
        out_telemetry.conflicts_count = 3;
        out_telemetry.propagations_count = 42;
        out_telemetry.restarts_count = 1;

        auto end = std::chrono::high_resolution_clock::now();
        out_results.parsing_time_ms = 4.2;
        out_results.reasoning_time_ms = std::chrono::duration<double, std::milli>(end - start).count();
        out_telemetry.solve_time_ms = out_results.parsing_time_ms + out_results.reasoning_time_ms;
        out_results.status = SolverStatus::SATISFIABLE;

        return true;
    }

    inline void API_Interrupt() {
        // [PLUGIN SLOT]: sodium_api_interrupt();
    }

    inline void API_Reset() {
        // [PLUGIN SLOT]: sodium_api_reset();
    }
}

// ============================================================================
// SECTION 2: SIGNATURE PURPLE & WHITE THEME CONFIGURATION
// ============================================================================

namespace SodiumTheme {

    // Primary Color Definitions (Majorly Purple with Pristine White & Violet)
    const ImVec4 BG_OBSIDIAN_PURPLE   = ImVec4(0.063f, 0.027f, 0.114f, 1.00f); // #10071D
    const ImVec4 BG_PANEL_PURPLE      = ImVec4(0.106f, 0.047f, 0.188f, 1.00f); // #1B0C30
    const ImVec4 BG_CONTAINER_PURPLE  = ImVec4(0.145f, 0.067f, 0.251f, 1.00f); // #251140
    const ImVec4 FRAME_BG_PURPLE      = ImVec4(0.188f, 0.086f, 0.329f, 0.90f); // #301654
    const ImVec4 FRAME_HOVER_PURPLE   = ImVec4(0.255f, 0.118f, 0.439f, 1.00f); // #411E70
    const ImVec4 FRAME_ACTIVE_PURPLE  = ImVec4(0.322f, 0.149f, 0.549f, 1.00f); // #52268C

    const ImVec4 ACCENT_ROYAL_PURPLE  = ImVec4(0.486f, 0.227f, 0.929f, 1.00f); // #7C3AED
    const ImVec4 ACCENT_VIBRANT_VIOLET= ImVec4(0.576f, 0.200f, 0.918f, 1.00f); // #9333EA
    const ImVec4 ACCENT_NEON_LILAC    = ImVec4(0.659f, 0.333f, 0.969f, 1.00f); // #A855F7
    const ImVec4 BORDER_GLOW_PURPLE   = ImVec4(0.753f, 0.518f, 0.988f, 0.40f); // #C084FC

    const ImVec4 TEXT_PURE_WHITE      = ImVec4(1.000f, 1.000f, 1.000f, 1.00f); // #FFFFFF
    const ImVec4 TEXT_LAVENDER_WHITE  = ImVec4(0.953f, 0.910f, 1.000f, 0.90f); // #F3E8FF
    const ImVec4 TEXT_MUTED_LILAC     = ImVec4(0.804f, 0.706f, 0.941f, 0.65f); // #CD8FF0

    const ImVec4 STATUS_SAT_GREEN     = ImVec4(0.063f, 0.725f, 0.506f, 1.00f); // #10B981
    const ImVec4 STATUS_UNSAT_ROSE    = ImVec4(0.957f, 0.247f, 0.369f, 1.00f); // #F43F5E
    const ImVec4 STATUS_WARN_AMBER    = ImVec4(0.961f, 0.620f, 0.043f, 1.00f); // #F59E0B

    inline void ApplyPurpleWhiteTheme() {
        ImGuiStyle& style = ImGui::GetStyle();

        // Geometry & Layout Ergonomics
        style.WindowRounding    = 8.0f;
        style.ChildRounding     = 6.0f;
        style.FrameRounding     = 5.0f;
        style.PopupRounding     = 6.0f;
        style.ScrollbarRounding = 6.0f;
        style.GrabRounding      = 4.0f;
        style.TabRounding       = 6.0f;

        style.WindowBorderSize  = 1.0f;
        style.ChildBorderSize   = 1.0f;
        style.PopupBorderSize   = 1.0f;
        style.FrameBorderSize   = 1.0f;
        style.TabBorderSize     = 1.0f;

        style.WindowPadding     = ImVec2(14.0f, 14.0f);
        style.FramePadding      = ImVec2(10.0f, 6.0f);
        style.ItemSpacing       = ImVec2(10.0f, 8.0f);
        style.ItemInnerSpacing  = ImVec2(8.0f, 6.0f);
        style.ScrollbarSize     = 12.0f;

        // Color Map Injection
        ImVec4* colors = style.Colors;

        // Canvas & Window Surfaces
        colors[ImGuiCol_WindowBg]             = BG_OBSIDIAN_PURPLE;
        colors[ImGuiCol_ChildBg]              = BG_PANEL_PURPLE;
        colors[ImGuiCol_PopupBg]              = BG_CONTAINER_PURPLE;
        colors[ImGuiCol_Border]               = BORDER_GLOW_PURPLE;
        colors[ImGuiCol_BorderShadow]         = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

        // Text & Labels
        colors[ImGuiCol_Text]                 = TEXT_PURE_WHITE;
        colors[ImGuiCol_TextDisabled]         = TEXT_MUTED_LILAC;

        // Frames, Input Boxes, and Checkboxes
        colors[ImGuiCol_FrameBg]              = FRAME_BG_PURPLE;
        colors[ImGuiCol_FrameBgHovered]       = FRAME_HOVER_PURPLE;
        colors[ImGuiCol_FrameBgActive]        = FRAME_ACTIVE_PURPLE;

        // Title Bar & Menus
        colors[ImGuiCol_TitleBg]              = BG_PANEL_PURPLE;
        colors[ImGuiCol_TitleBgActive]        = BG_CONTAINER_PURPLE;
        colors[ImGuiCol_TitleBgCollapsed]     = BG_OBSIDIAN_PURPLE;
        colors[ImGuiCol_MenuBarBg]            = BG_PANEL_PURPLE;

        // Buttons & Action Triggers
        colors[ImGuiCol_Button]               = ACCENT_ROYAL_PURPLE;
        colors[ImGuiCol_ButtonHovered]        = ACCENT_VIBRANT_VIOLET;
        colors[ImGuiCol_ButtonActive]         = ACCENT_NEON_LILAC;

        // Tabs & Navigation Toggles
        colors[ImGuiCol_Tab]                  = BG_CONTAINER_PURPLE;
        colors[ImGuiCol_TabHovered]           = ACCENT_VIBRANT_VIOLET;
        colors[ImGuiCol_TabActive]            = ACCENT_ROYAL_PURPLE;
        colors[ImGuiCol_TabUnfocused]         = BG_PANEL_PURPLE;
        colors[ImGuiCol_TabUnfocusedActive]   = FRAME_BG_PURPLE;

        // Scrollbars & Sliders
        colors[ImGuiCol_ScrollbarBg]          = BG_PANEL_PURPLE;
        colors[ImGuiCol_ScrollbarGrab]        = ACCENT_ROYAL_PURPLE;
        colors[ImGuiCol_ScrollbarGrabHovered] = ACCENT_VIBRANT_VIOLET;
        colors[ImGuiCol_ScrollbarGrabActive]  = ACCENT_NEON_LILAC;
        colors[ImGuiCol_SliderGrab]           = TEXT_PURE_WHITE;
        colors[ImGuiCol_SliderGrabActive]     = ACCENT_NEON_LILAC;

        // Headers (Collapsing headers, tree nodes)
        colors[ImGuiCol_Header]               = FRAME_BG_PURPLE;
        colors[ImGuiCol_HeaderHovered]        = ACCENT_ROYAL_PURPLE;
        colors[ImGuiCol_HeaderActive]         = ACCENT_VIBRANT_VIOLET;

        // Tables & Data Grids
        colors[ImGuiCol_TableHeaderBg]        = BG_CONTAINER_PURPLE;
        colors[ImGuiCol_TableBorderStrong]    = BORDER_GLOW_PURPLE;
        colors[ImGuiCol_TableBorderLight]     = ImVec4(0.753f, 0.518f, 0.988f, 0.20f);
        colors[ImGuiCol_TableRowBg]           = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
        colors[ImGuiCol_TableRowBgAlt]        = ImVec4(0.145f, 0.067f, 0.251f, 0.35f);

        // Plots, Histograms, Separators
        colors[ImGuiCol_PlotLines]            = ACCENT_NEON_LILAC;
        colors[ImGuiCol_PlotLinesHovered]     = TEXT_PURE_WHITE;
        colors[ImGuiCol_PlotHistogram]        = ACCENT_VIBRANT_VIOLET;
        colors[ImGuiCol_PlotHistogramHovered] = TEXT_PURE_WHITE;
        colors[ImGuiCol_Separator]            = BORDER_GLOW_PURPLE;
        colors[ImGuiCol_SeparatorHovered]     = ACCENT_NEON_LILAC;
        colors[ImGuiCol_SeparatorActive]      = TEXT_PURE_WHITE;
    }
}

// ============================================================================
// SECTION 3: UI STATE & WORKSPACE CONTROLLER
// ============================================================================

class SodiumUIController {
public:
    enum class WorkspaceMode {
        TRADITIONAL_SAT,
        NATURAL_LANGUAGE_PROCESSING
    };

    WorkspaceMode current_mode = WorkspaceMode::TRADITIONAL_SAT;

    // Solver Parameters & Configuration
    SodiumPlugin::SolverParams solver_params;

    // Buffers for Text Editors
    char cnf_buffer[65536];
    char nlp_buffer[65536];
    char log_filter_buffer[128];

    // Execution & Telemetry Results
    SodiumPlugin::TelemetryData telemetry;
    SodiumPlugin::SolverResults sat_results;
    SodiumPlugin::NLPResults nlp_results;
    SodiumPlugin::SolverStatus overall_status = SodiumPlugin::SolverStatus::IDLE;

    // Console Logging System
    struct LogEntry {
        std::string timestamp;
        std::string level;
        std::string message;
        ImVec4 color;
    };
    std::vector<LogEntry> console_logs;
    bool auto_scroll_logs = true;

    SodiumUIController() {
        memset(cnf_buffer, 0, sizeof(cnf_buffer));
        memset(nlp_buffer, 0, sizeof(nlp_buffer));
        memset(log_filter_buffer, 0, sizeof(log_filter_buffer));

        // Default CNF Example (DIMACS format)
        const char* default_cnf =
            "c ====================================================\n"
            "c Sodium Industrial SAT Benchmark: Distributed Lock Safety\n"
            "c Variables: x1=Node1_Primary, x2=Node2_Primary, x3=SyncQuorum\n"
            "c ====================================================\n"
            "p cnf 5 7\n"
            " 1  2  0\n"
            "-1 -2  0\n"
            " 1  3  0\n"
            "-1  4  5  0\n"
            "-3  4  0\n"
            " 2 -5  0\n"
            "-4 -5  0\n";
        strncpy(cnf_buffer, default_cnf, sizeof(cnf_buffer) - 1);

        // Default NLP Example (Plain English specifications)
        const char* default_nlp ="Enter your necessary command.";
        strncpy(nlp_buffer, default_nlp, sizeof(nlp_buffer) - 1);

        AddLog("INIT", "Sodium Automated Reasoning Engine UI Initialized.", SodiumTheme::ACCENT_NEON_LILAC);
        AddLog("READY", "CDCL SAT and NLP reasoning modules standing by for input.", SodiumTheme::STATUS_SAT_GREEN);
    }

    void AddLog(const std::string& level, const std::string& msg, ImVec4 color) {
        auto now = std::chrono::system_clock::now();
        std::time_t now_time = std::chrono::system_clock::to_time_t(now);
        std::tm* local_tm = std::localtime(&now_time);

        std::ostringstream ss;
        ss << std::put_time(local_tm, "%H:%M:%S");

        console_logs.push_back({ ss.str(), level, msg, color });
        if (console_logs.size() > 500) {
            console_logs.erase(console_logs.begin(), console_logs.begin() + 50);
        }
    }

    void TriggerSolve() {
        overall_status = SodiumPlugin::SolverStatus::RUNNING;

        if (current_mode == WorkspaceMode::TRADITIONAL_SAT) {
            AddLog("SOLVE", "Triggering CDCL SAT search via Sodium API...", SodiumTheme::ACCENT_ROYAL_PURPLE);
            bool ok = SodiumPlugin::API_SolveSAT(cnf_buffer, solver_params, sat_results, telemetry);
            overall_status = sat_results.status;

            if (ok && overall_status == SodiumPlugin::SolverStatus::SATISFIABLE) {
                AddLog("SAT", "Formula mathematically proved SATISFIABLE in " +
                    std::to_string(sat_results.execution_time_ms) + " ms.", SodiumTheme::STATUS_SAT_GREEN);
            } else if (overall_status == SodiumPlugin::SolverStatus::UNSATISFIABLE) {
                AddLog("UNSAT", "Formula mathematically proved UNSATISFIABLE. 1-UIP derivation complete.", SodiumTheme::STATUS_UNSAT_ROSE);
            } else {
                AddLog("ERROR", "Solver encountered error or syntax irregularity.", SodiumTheme::STATUS_UNSAT_ROSE);
            }
        } else {
            AddLog("NLP", "Compiling natural language requirements into First-Order Predicates & CNF...", SodiumTheme::ACCENT_ROYAL_PURPLE);
            bool ok = SodiumPlugin::API_SolveNLP(nlp_buffer, solver_params, nlp_results, telemetry);
            overall_status = nlp_results.status;

            if (ok && overall_status == SodiumPlugin::SolverStatus::SATISFIABLE) {
                AddLog("NLP_SAT", "Semantic constraints compiled and formal solution synthesized successfully.", SodiumTheme::STATUS_SAT_GREEN);
            } else {
                AddLog("NLP_UNSAT", "Contradiction identified between contractual requirements.", SodiumTheme::STATUS_UNSAT_ROSE);
            }
        }
    }

    void TriggerInterrupt() {
        SodiumPlugin::API_Interrupt();
        overall_status = SodiumPlugin::SolverStatus::INTERRUPTED;
        AddLog("INTERRUPT", "Execution halted by user request.", SodiumTheme::STATUS_WARN_AMBER);
    }

    void TriggerReset() {
        SodiumPlugin::API_Reset();
        overall_status = SodiumPlugin::SolverStatus::IDLE;
        sat_results = SodiumPlugin::SolverResults();
        nlp_results = SodiumPlugin::NLPResults();
        telemetry = SodiumPlugin::TelemetryData();
        AddLog("RESET", "Solver state, trail, and buffers reset to initial state.", SodiumTheme::TEXT_MUTED_LILAC);
    }
};

static std::unique_ptr<SodiumUIController> g_ui_controller = nullptr;

// ============================================================================
// SECTION 4: RENDERING MODULES (TOP BAR, WORKSPACES, TELEMETRY, LOGS)
// ============================================================================

static void RenderTopNavigationBar(SodiumUIController& ctrl) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, SodiumTheme::BG_PANEL_PURPLE);
    ImGui::BeginChild("TopNavigationBar", ImVec2(0, 56), true, ImGuiWindowFlags_NoScrollbar);

    // Title and Logo Branding
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(SodiumTheme::ACCENT_NEON_LILAC, "[Na]");
    ImGui::SameLine();
    ImGui::TextColored(SodiumTheme::TEXT_PURE_WHITE, "SODIUM");
    ImGui::SameLine();
    ImGui::TextColored(SodiumTheme::TEXT_MUTED_LILAC, "| Automated Reasoning Engine");

    ImGui::SameLine(340);

    // DUAL-MODE MASTER SWITCHER TAB
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 20.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(16.0f, 6.0f));

    bool is_sat = (ctrl.current_mode == SodiumUIController::WorkspaceMode::TRADITIONAL_SAT);
    bool is_nlp = (ctrl.current_mode == SodiumUIController::WorkspaceMode::NATURAL_LANGUAGE_PROCESSING);

    if (is_sat) {
        ImGui::PushStyleColor(ImGuiCol_Button, SodiumTheme::ACCENT_ROYAL_PURPLE);
        ImGui::PushStyleColor(ImGuiCol_Text, SodiumTheme::TEXT_PURE_WHITE);
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, SodiumTheme::BG_CONTAINER_PURPLE);
        ImGui::PushStyleColor(ImGuiCol_Text, SodiumTheme::TEXT_LAVENDER_WHITE);
    }

    if (ImGui::Button("Sodium Base")) {
        ctrl.current_mode = SodiumUIController::WorkspaceMode::TRADITIONAL_SAT;
        ctrl.AddLog("MODE", "Switched workspace to Traditional SAT Solving.", SodiumTheme::TEXT_PURE_WHITE);
    }
    ImGui::PopStyleColor(2);

    ImGui::SameLine();

    if (is_nlp) {
        ImGui::PushStyleColor(ImGuiCol_Button, SodiumTheme::ACCENT_ROYAL_PURPLE);
        ImGui::PushStyleColor(ImGuiCol_Text, SodiumTheme::TEXT_PURE_WHITE);
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, SodiumTheme::BG_CONTAINER_PURPLE);
        ImGui::PushStyleColor(ImGuiCol_Text, SodiumTheme::TEXT_LAVENDER_WHITE);
    }

    if (ImGui::Button("Sodium Plus(Beta)")) {
        ctrl.current_mode = SodiumUIController::WorkspaceMode::NATURAL_LANGUAGE_PROCESSING;
        ctrl.AddLog("MODE", "Switched workspace to Sodium Natural Language Processing.", SodiumTheme::ACCENT_NEON_LILAC);
    }
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);

    // Global Action Buttons on the Right Hand Side
    float right_side_offset = ImGui::GetWindowWidth() - 380;
    if (right_side_offset > 720) {
        ImGui::SameLine(right_side_offset);

        // Solve Button
        ImGui::PushStyleColor(ImGuiCol_Button, SodiumTheme::ACCENT_ROYAL_PURPLE);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, SodiumTheme::ACCENT_VIBRANT_VIOLET);
        if (ImGui::Button(" Solve [F5] ", ImVec2(100, 0))) {
            ctrl.TriggerSolve();
        }
        ImGui::PopStyleColor(2);

        ImGui::SameLine();
        if (ImGui::Button(" Halt ", ImVec2(70, 0))) {
            ctrl.TriggerInterrupt();
        }

        ImGui::SameLine();
        if (ImGui::Button(" Reset ", ImVec2(70, 0))) {
            ctrl.TriggerReset();
        }

        ImGui::SameLine();
        // Status Badge Pill
        ImVec4 pill_bg = SodiumTheme::BG_CONTAINER_PURPLE;
        const char* pill_text = "IDLE";
        if (ctrl.overall_status == SodiumPlugin::SolverStatus::RUNNING) {
            pill_bg = SodiumTheme::STATUS_WARN_AMBER;
            pill_text = "SOLVING...";
        } else if (ctrl.overall_status == SodiumPlugin::SolverStatus::SATISFIABLE) {
            pill_bg = SodiumTheme::STATUS_SAT_GREEN;
            pill_text = "SAT";
        } else if (ctrl.overall_status == SodiumPlugin::SolverStatus::UNSATISFIABLE) {
            pill_bg = SodiumTheme::STATUS_UNSAT_ROSE;
            pill_text = "UNSAT";
        }

        ImGui::PushStyleColor(ImGuiCol_Button, pill_bg);
        ImGui::Button(pill_text, ImVec2(75, 0));
        ImGui::PopStyleColor();
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();
}

static void RenderTraditionalSATWorkspace(SodiumUIController& ctrl) {
    float avail_width = ImGui::GetContentRegionAvail().x;
    float left_pane_width = avail_width * 0.50f;
    float right_pane_width = avail_width - left_pane_width - 12.0f;

    // LEFT COLUMN: Problem Workspace & CNF Formula Editor
    ImGui::BeginChild("SAT_LeftPane", ImVec2(left_pane_width, 0), true);
    {
        ImGui::TextColored(SodiumTheme::TEXT_PURE_WHITE, "CNF Problem Input (DIMACS Format)");
        ImGui::SameLine();
        ImGui::TextColored(SodiumTheme::TEXT_MUTED_LILAC, "| 2-Watched Literals BCP Target");

        // Preset Selector Row
        ImGui::TextColored(SodiumTheme::ACCENT_NEON_LILAC, "Load Benchmark Presets:");
        ImGui::SameLine();
        if (ImGui::SmallButton("N-Queens (4)")) {
            const char* nqueens =
                "c 4-Queens Problem in DIMACS format\n"
                "p cnf 16 28\n"
                "1 2 3 4 0\n"
                "5 6 7 8 0\n"
                "9 10 11 12 0\n"
                "13 14 15 16 0\n"
                "-1 -2 0\n-1 -3 0\n-1 -4 0\n-2 -3 0\n-2 -4 0\n-3 -4 0\n"
                "-5 -6 0\n-5 -7 0\n-5 -8 0\n-6 -7 0\n-6 -8 0\n-7 -8 0\n"
                "-1 -5 0\n-1 -9 0\n-1 -13 0\n-5 -9 0\n-5 -13 0\n-9 -13 0\n"
                "-1 -6 0\n-1 -11 0\n-1 -16 0\n-2 -7 0\n-2 -12 0\n-3 -8 0\n";
            strncpy(ctrl.cnf_buffer, nqueens, sizeof(ctrl.cnf_buffer) - 1);
            ctrl.AddLog("LOAD", "Loaded 4-Queens CNF Benchmark.", SodiumTheme::TEXT_PURE_WHITE);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Pigeonhole (3 in 2)")) {
            const char* php =
                "c Pigeonhole Principle (Unsatisfiable)\n"
                "p cnf 6 9\n"
                "1 2 0\n"
                "3 4 0\n"
                "5 6 0\n"
                "-1 -3 0\n"
                "-1 -5 0\n"
                "-3 -5 0\n"
                "-2 -4 0\n"
                "-2 -6 0\n"
                "-4 -6 0\n";
            strncpy(ctrl.cnf_buffer, php, sizeof(ctrl.cnf_buffer) - 1);
            ctrl.AddLog("LOAD", "Loaded Pigeonhole Principle (UNSAT) Benchmark.", SodiumTheme::TEXT_PURE_WHITE);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear")) {
            ctrl.cnf_buffer[0] = '\0';
        }

        // Multi-line editor
        float editor_height = ImGui::GetContentRegionAvail().y - 120.0f;
        ImGui::InputTextMultiline("##CNFEditor", ctrl.cnf_buffer, sizeof(ctrl.cnf_buffer),
                                  ImVec2(-1, editor_height > 150 ? editor_height : 150),
                                  ImGuiInputTextFlags_AllowTabInput);

        ImGui::Separator();

        // Parameter Sliders Ribbon
        ImGui::TextColored(SodiumTheme::TEXT_PURE_WHITE, "CDCL Heuristics Tuning");
        ImGui::SliderFloat("VSIDS Decay", &ctrl.solver_params.vsids_decay, 0.70f, 0.99f, "%.2f");
        ImGui::SliderInt("Restart Base", &ctrl.solver_params.restart_base, 20, 500);
        ImGui::SliderFloat("Compaction Threshold", &ctrl.solver_params.compaction_threshold, 0.10f, 0.50f, "%.2f");
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // RIGHT COLUMN: Telemetry, VSIDS Profile, Trail & Model
    ImGui::BeginChild("SAT_RightPane", ImVec2(right_pane_width, 0), true);
    {
        // Metric Telemetry Cards Grid
        ImGui::TextColored(SodiumTheme::TEXT_PURE_WHITE, "Live Engine Telemetry & Diagnostics");
        ImGui::Separator();

        ImGui::Columns(4, "TelemetryColumns", false);
        ImGui::TextColored(SodiumTheme::TEXT_MUTED_LILAC, "Variables");
        ImGui::TextColored(SodiumTheme::TEXT_PURE_WHITE, "%d", ctrl.telemetry.variable_count);
        ImGui::NextColumn();

        ImGui::TextColored(SodiumTheme::TEXT_MUTED_LILAC, "Clauses");
        ImGui::TextColored(SodiumTheme::TEXT_PURE_WHITE, "%d", ctrl.telemetry.original_clause_count + ctrl.telemetry.learned_clause_count);
        ImGui::NextColumn();

        ImGui::TextColored(SodiumTheme::TEXT_MUTED_LILAC, "Decisions");
        ImGui::TextColored(SodiumTheme::TEXT_PURE_WHITE, "%d", ctrl.telemetry.decisions_count);
        ImGui::NextColumn();

        ImGui::TextColored(SodiumTheme::TEXT_MUTED_LILAC, "Conflicts");
        ImGui::TextColored(SodiumTheme::TEXT_PURE_WHITE, "%d", ctrl.telemetry.conflicts_count);
        ImGui::NextColumn();

        ImGui::TextColored(SodiumTheme::TEXT_MUTED_LILAC, "Learned");
        ImGui::TextColored(SodiumTheme::ACCENT_NEON_LILAC, "%d", ctrl.telemetry.learned_clause_count);
        ImGui::NextColumn();

        ImGui::TextColored(SodiumTheme::TEXT_MUTED_LILAC, "Restarts");
        ImGui::TextColored(SodiumTheme::TEXT_PURE_WHITE, "%d", ctrl.telemetry.restarts_count);
        ImGui::NextColumn();

        ImGui::TextColored(SodiumTheme::TEXT_MUTED_LILAC, "Mem Slots");
        ImGui::TextColored(SodiumTheme::TEXT_PURE_WHITE, "%d", ctrl.telemetry.memory_used_slots);
        ImGui::NextColumn();

        ImGui::TextColored(SodiumTheme::TEXT_MUTED_LILAC, "Runtime");
        ImGui::TextColored(SodiumTheme::STATUS_SAT_GREEN, "%.2f ms", ctrl.telemetry.solve_time_ms);
        ImGui::Columns(1);

        ImGui::Spacing();
        ImGui::Separator();

        // VSIDS Activity Histogram
        if (!ctrl.telemetry.vsids_activities.empty()) {
            ImGui::TextColored(SodiumTheme::TEXT_PURE_WHITE, "VSIDS Variable Activity Distribution");
            ImGui::PlotHistogram("##VSIDSActivities",
                                 ctrl.telemetry.vsids_activities.data(),
                                 (int)ctrl.telemetry.vsids_activities.size(),
                                 0, "Activity Scores (Decayed)", 0.0f, 35.0f, ImVec2(-1, 65));
        }

        ImGui::Spacing();

        // TABS: Trail Inspector vs Satisfying Model
        if (ImGui::BeginTabBar("SATDetailsTabBar")) {
            if (ImGui::BeginTabItem(" Implication Trail (2WL) ")) {
                if (ImGui::BeginTable("TrailTable", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImVec2(0, 160))) {
                    ImGui::TableSetupColumn("Level", ImGuiTableColumnFlags_WidthFixed, 50.0f);
                    ImGui::TableSetupColumn("Literal", ImGuiTableColumnFlags_WidthFixed, 60.0f);
                    ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                    ImGui::TableSetupColumn("Reason / Antecedent Clause", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableHeadersRow();

                    for (const auto& item : ctrl.sat_results.trail) {
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::Text("%d", item.decision_level);
                        ImGui::TableNextColumn();
                        ImGui::TextColored(item.literal > 0 ? SodiumTheme::STATUS_SAT_GREEN : SodiumTheme::STATUS_UNSAT_ROSE,
                                           "%+d", item.literal);
                        ImGui::TableNextColumn();
                        if (item.is_decision) {
                            ImGui::TextColored(SodiumTheme::ACCENT_NEON_LILAC, "Decision");
                        } else {
                            ImGui::TextColored(SodiumTheme::TEXT_MUTED_LILAC, "Propagated");
                        }
                        ImGui::TableNextColumn();
                        ImGui::Text("%s", item.reason_clause.c_str());
                    }
                    ImGui::EndTable();
                }
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem(" Satisfying Assignment Model ")) {
                if (ctrl.sat_results.model.empty()) {
                    ImGui::TextColored(SodiumTheme::TEXT_MUTED_LILAC, "No model computed yet. Press 'Solve [F5]'.");
                } else {
                    if (ImGui::BeginTable("ModelTable", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImVec2(0, 160))) {
                        ImGui::TableSetupColumn("Variable", ImGuiTableColumnFlags_WidthFixed, 75.0f);
                        ImGui::TableSetupColumn("Assigned Truth Value", ImGuiTableColumnFlags_WidthFixed, 150.0f);
                        ImGui::TableSetupColumn("Explanation / Origin", ImGuiTableColumnFlags_WidthStretch);
                        ImGui::TableHeadersRow();

                        for (const auto& v : ctrl.sat_results.model) {
                            ImGui::TableNextRow();
                            ImGui::TableNextColumn();
                            ImGui::Text("x%d", v.variable);
                            ImGui::TableNextColumn();
                            if (v.value > 0) {
                                ImGui::TextColored(SodiumTheme::STATUS_SAT_GREEN, "TRUE  (VAL_TRUE)");
                            } else {
                                ImGui::TextColored(SodiumTheme::STATUS_UNSAT_ROSE, "FALSE (VAL_FALSE)");
                            }
                            ImGui::TableNextColumn();
                            ImGui::Text("%s (Level %d)", v.explanation.c_str(), v.decision_level);
                        }
                        ImGui::EndTable();
                    }
                }
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::EndChild();
}

static void RenderNaturalLanguageProcessingWorkspace(SodiumUIController& ctrl) {
    float avail_width = ImGui::GetContentRegionAvail().x;
    float col_width = (avail_width - 12.0f) * 0.50f;

    // LEFT COLUMN: Prose Directives & Contract Editor
    ImGui::BeginChild("NLP_LeftPane", ImVec2(col_width, 0), true);
    {
        ImGui::TextColored(SodiumTheme::TEXT_PURE_WHITE, "Natural Language Policy Directives & SLA Clauses");
        ImGui::SameLine();
        ImGui::TextColored(SodiumTheme::ACCENT_NEON_LILAC, "| Neuro-Symbolic Ingestion");

        // Example Presets
        ImGui::TextColored(SodiumTheme::TEXT_MUTED_LILAC, "Contract Presets:");
        ImGui::SameLine();
        if (ImGui::SmallButton("Avionics Bus Invariants")) {
            const char* avionics =
                "Rule 1: Flight Controller A and Flight Controller B must never assert actuator command authority concurrently.\n"
                "Rule 2: If primary telemetry sensor jitter exceeds 5ms, the arbitration bus must trigger failover to Controller B.\n"
                "Rule 3: Actuator commands must be issued every 20ms or emergency drift mode is engaged.\n";
            strncpy(ctrl.nlp_buffer, avionics, sizeof(ctrl.nlp_buffer) - 1);
            ctrl.AddLog("NLP_PRESET", "Loaded Avionics Safety Directives.", SodiumTheme::TEXT_PURE_WHITE);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Fintech Multi-Party Escrow")) {
            const char* fintech =
                "Clause A: Funds are released from escrow if and only if both Buyer Approval and Customs Signoff are logged.\n"
                "Clause B: If dispute timeout of 72 hours elapses without resolution, arbitration hold takes precedence.\n"
                "Clause C: A dispute flag precludes immediate settlement authorization.\n";
            strncpy(ctrl.nlp_buffer, fintech, sizeof(ctrl.nlp_buffer) - 1);
            ctrl.AddLog("NLP_PRESET", "Loaded Fintech Escrow Settlement Contract.", SodiumTheme::TEXT_PURE_WHITE);
        }

        // Prose Editor
        float editor_height = ImGui::GetContentRegionAvail().y - 10.0f;
        ImGui::InputTextMultiline("##NLPEditor", ctrl.nlp_buffer, sizeof(ctrl.nlp_buffer),
                                  ImVec2(-1, editor_height > 200 ? editor_height : 200),
                                  ImGuiInputTextFlags_AllowTabInput);
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // RIGHT COLUMN: Semantic AST, Compiled Constraints & Synthesized Plan
    ImGui::BeginChild("NLP_RightPane", ImVec2(col_width, 0), true);
    {
        ImGui::TextColored(SodiumTheme::TEXT_PURE_WHITE, "Semantic Extraction & Synthesized Solution");
        ImGui::Separator();

        // Timers & Compiler Health
        ImGui::TextColored(SodiumTheme::TEXT_MUTED_LILAC, "Parsing & NLP Compilation:");
        ImGui::SameLine();
        ImGui::TextColored(SodiumTheme::TEXT_PURE_WHITE, "%.2f ms", ctrl.nlp_results.parsing_time_ms);
        ImGui::SameLine(240);
        ImGui::TextColored(SodiumTheme::TEXT_MUTED_LILAC, "Formal Mathematical Proof:");
        ImGui::SameLine();
        ImGui::TextColored(SodiumTheme::STATUS_SAT_GREEN, "%.2f ms", ctrl.nlp_results.reasoning_time_ms);

        ImGui::Spacing();

        if (ImGui::BeginTabBar("NLPTabBar")) {
            if (ImGui::BeginTabItem(" Compiled Logic Constraints ")) {
                ImGui::TextColored(SodiumTheme::ACCENT_NEON_LILAC, "English Prose -> First-Order Predicates -> Mathematical CNF Clauses:");
                ImGui::Spacing();

                if (ctrl.nlp_results.extracted_constraints.empty()) {
                    ImGui::TextColored(SodiumTheme::TEXT_MUTED_LILAC, "Awaiting NLP ingestion. Click 'Solve [F5]' to compile prose.");
                } else {
                    for (size_t i = 0; i < ctrl.nlp_results.extracted_constraints.size(); ++i) {
                        const auto& item = ctrl.nlp_results.extracted_constraints[i];
                        std::string header_label = "Constraint #" + std::to_string(i + 1);

                        if (ImGui::CollapsingHeader(header_label.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                            ImGui::BulletText("Human Clause: \"%s\"", item.natural_clause.c_str());
                            ImGui::BulletText("Predicate Logic: %s", item.first_order_logic.c_str());
                            ImGui::BulletText("Compiled CNF:   %s", item.compiled_cnf.c_str());
                        }
                        ImGui::Spacing();
                    }
                }
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem(" Synthesized Actionable Plan ")) {
                ImGui::TextColored(SodiumTheme::STATUS_SAT_GREEN, "Mathematical Solution -> Concrete Plain-English Directives:");
                ImGui::Spacing();

                if (ctrl.nlp_results.synthesized_plan.empty()) {
                    ImGui::TextColored(SodiumTheme::TEXT_MUTED_LILAC, "No plan synthesized yet. Run 'Solve [F5]'.");
                } else {
                    ImGui::PushStyleColor(ImGuiCol_ChildBg, SodiumTheme::BG_CONTAINER_PURPLE);
                    ImGui::BeginChild("PlanBox", ImVec2(-1, 0), true);
                    for (const auto& step : ctrl.nlp_results.synthesized_plan) {
                        ImGui::TextColored(SodiumTheme::TEXT_PURE_WHITE, "• %s", step.c_str());
                        ImGui::Spacing();
                    }
                    ImGui::EndChild();
                    ImGui::PopStyleColor();
                }
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::EndChild();
}

static void RenderConsoleLogDrawer(SodiumUIController& ctrl) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, SodiumTheme::BG_PANEL_PURPLE);
    ImGui::BeginChild("ConsoleLogDrawer", ImVec2(0, 0), true);

    // Header with Filter and Clear Button
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(SodiumTheme::TEXT_PURE_WHITE, "Execution Log & System Event Console");
    ImGui::SameLine();
    ImGui::Checkbox("Auto-scroll", &ctrl.auto_scroll_logs);
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear Logs")) {
        ctrl.console_logs.clear();
    }
    ImGui::SameLine(ImGui::GetWindowWidth() - 250);
    ImGui::SetNextItemWidth(230);
    ImGui::InputTextWithHint("##LogFilter", "Filter messages...", ctrl.log_filter_buffer, sizeof(ctrl.log_filter_buffer));

    ImGui::Separator();

    // Scrollable Log Area
    ImGui::BeginChild("LogScrollArea", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
    for (const auto& log : ctrl.console_logs) {
        if (ctrl.log_filter_buffer[0] != '\0') {
            if (log.message.find(ctrl.log_filter_buffer) == std::string::npos &&
                log.level.find(ctrl.log_filter_buffer) == std::string::npos) {
                continue;
            }
        }

        ImGui::TextColored(SodiumTheme::TEXT_MUTED_LILAC, "[%s]", log.timestamp.c_str());
        ImGui::SameLine();
        ImGui::TextColored(SodiumTheme::ACCENT_NEON_LILAC, "[%-8s]", log.level.c_str());
        ImGui::SameLine();
        ImGui::TextColored(log.color, "%s", log.message.c_str());
    }

    if (ctrl.auto_scroll_logs && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
        ImGui::SetScrollHereY(1.0f);
    }

    ImGui::EndChild();
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

// ============================================================================
// SECTION 5: PUBLIC LIFECYCLE API
// ============================================================================

void SodiumUI_Init() {
    SodiumTheme::ApplyPurpleWhiteTheme();
    g_ui_controller = std::make_unique<SodiumUIController>();
}

void SodiumUI_RenderFrame() {
    if (!g_ui_controller) {
        SodiumUI_Init();
    }

    SodiumUIController& ctrl = *g_ui_controller;

    // Fullscreen Root Window Frame
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGuiWindowFlags window_flags =
        ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 10.0f));

    ImGui::Begin("SodiumRootWindow", nullptr, window_flags);
    ImGui::PopStyleVar(3);

    // 1. Top Persistent Navigation Bar with Mode Switcher
    RenderTopNavigationBar(ctrl);
    ImGui::Spacing();

    // 2. Active Mode Workspace (SAT vs NLP)
    float total_height = ImGui::GetContentRegionAvail().y;
    float workspace_height = total_height - 180.0f; // Reserve 180px for bottom log console

    ImGui::BeginChild("ActiveWorkspaceArea", ImVec2(0, workspace_height > 250 ? workspace_height : 250), false);
    if (ctrl.current_mode == SodiumUIController::WorkspaceMode::TRADITIONAL_SAT) {
        RenderTraditionalSATWorkspace(ctrl);
    } else {
        RenderNaturalLanguageProcessingWorkspace(ctrl);
    }
    ImGui::EndChild();

    ImGui::Spacing();

    // 3. Shared Bottom Console Log Drawer
    RenderConsoleLogDrawer(ctrl);

    ImGui::End();
}

void SodiumUI_Shutdown() {
    g_ui_controller.reset();
}
