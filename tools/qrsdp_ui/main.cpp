// Minimal QRSDP real-time debugging UI: ImGui + ImPlot + GLFW + OpenGL3.
// Shows price over time, order book ladder, event table, and diagnostics.

#include "producer/qrsdp_producer.h"
#include "io/in_memory_sink.h"
#include "book/multi_level_book.h"
#include "model/simple_imbalance_intensity.h"
#include "model/hlr_params.h"
#include "model/curve_intensity_model.h"
#include "rng/mt19937_rng.h"
#include "sampler/competing_intensity_sampler.h"
#include "sampler/unit_size_attribute_sampler.h"
#include "core/records.h"
#include "core/event_types.h"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "implot.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace {

constexpr size_t kPriceHistoryMax = 200000;
constexpr size_t kEventTableMax = 200;
constexpr size_t kDepthHistoryMax = 10000;
constexpr int kLadderLevels = 10;

struct PricePoint {
    double t = 0.0;
    double mid = 0.0;
    double bid = 0.0;
    double ask = 0.0;
    bool shift_up = false;
    bool shift_down = false;
};

struct EventRow {
    double t_sec = 0.0;
    uint64_t ts_ns = 0;
    uint8_t type = 0;
    uint8_t side = 0;
    int32_t price_ticks = 0;
    uint32_t qty = 0;
    uint64_t order_id = 0;
    bool is_shift_marker = false;
    char shift_label[32] = "";
};

const char* eventTypeName(uint8_t type) {
    switch (static_cast<qrsdp::EventType>(type)) {
        case qrsdp::EventType::ADD_BID:     return "ADD_BID";
        case qrsdp::EventType::ADD_ASK:     return "ADD_ASK";
        case qrsdp::EventType::CANCEL_BID:  return "CANCEL_BID";
        case qrsdp::EventType::CANCEL_ASK:  return "CANCEL_ASK";
        case qrsdp::EventType::EXECUTE_BUY: return "EXEC_BUY";
        case qrsdp::EventType::EXECUTE_SELL: return "EXEC_SELL";
        default: return "?";
    }
}

}  // namespace

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
    GLFWwindow* window = glfwCreateWindow(1400, 900, "QRSDP Debug UI", nullptr, nullptr);
    if (!window) { glfwTerminate(); return 1; }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    // --- QRSDP components (owned here; producer uses refs) ---
    qrsdp::Mt19937Rng rng(12345);
    qrsdp::MultiLevelBook book;
    qrsdp::IIntensityModel* intensityModel = nullptr;  // points to legacy or curve model
    qrsdp::CompetingIntensitySampler eventSampler(rng);
    qrsdp::UnitSizeAttributeSampler* attrSampler = nullptr;
    qrsdp::QrsdpProducer* producer = nullptr;
    qrsdp::InMemorySink sink;

    // Session params (UI-driven) — defaults match CLI/production tuning
    uint64_t ui_seed = 777;
    uint32_t ui_session_seconds = 120;
    int32_t ui_p0_ticks = 10000;
    uint32_t ui_levels_per_side = 5;
    uint32_t ui_tick_size = 100;
    uint32_t ui_initial_depth = 5;
    uint32_t ui_initial_spread_ticks = 2;
    double ui_base_L = 20.0;
    double ui_base_M = 15.0;
    double ui_base_C = 0.5;
    double ui_epsilon_exec = 0.5;
    double ui_spread_sens = 0.4;
    double ui_spread_improve = 0.5;
    double ui_alpha = 0.5;
    int ui_step_N = 10;
    int ui_max_events_per_frame = 100;
    bool ui_running = false;
    bool ui_show_mid = true;
    bool ui_show_bid_ask = true;
    bool ui_show_shift_markers = true;
    int ui_model = 0;  // 0 = Legacy (SimpleImbalance), 1 = HLR2014 (CurveIntensity)
    double ui_theta_reinit = 0.0;
    double ui_reinit_mean = 10.0;
    double ui_hlr_spread_sens = 0.3;
    double ui_hlr_imb_sens = 1.0;
    int ui_curve_preset = 0;  // 0 = starter large-tick, 1 = high vol, 2 = mean-reverting (same for now)
    int ui_nmax = 100;

    // Model instances; reset() recreates based on ui_model
    std::unique_ptr<qrsdp::SimpleImbalanceIntensity> legacyModelPtr;
    std::unique_ptr<qrsdp::CurveIntensityModel> curveModelPtr;
    std::unique_ptr<qrsdp::UnitSizeAttributeSampler> attrSamplerPtr(
        new qrsdp::UnitSizeAttributeSampler(rng, ui_alpha, ui_spread_improve));
    attrSampler = attrSamplerPtr.get();
    {
        qrsdp::IntensityParams p{ui_base_L, ui_base_C, ui_base_M, 1.0, 1.0, ui_epsilon_exec, ui_spread_sens};
        legacyModelPtr = std::make_unique<qrsdp::SimpleImbalanceIntensity>(p);
        intensityModel = legacyModelPtr.get();
    }
    std::unique_ptr<qrsdp::QrsdpProducer> producerPtr(
        new qrsdp::QrsdpProducer(rng, book, *intensityModel, eventSampler, *attrSampler));
    producer = producerPtr.get();

    // History (ring buffers)
    std::deque<PricePoint> price_history;
    std::deque<EventRow> event_rows;
    std::deque<double> depth_bid_best_history;
    std::deque<double> depth_ask_best_history;

    // Diagnostics
    uint64_t event_count = 0;
    uint64_t count_add_bid = 0, count_add_ask = 0, count_cancel_bid = 0, count_cancel_ask = 0;
    uint64_t count_exec_buy = 0, count_exec_sell = 0;
    uint64_t up_shifts = 0, down_shifts = 0;
    double last_shift_t = 0.0;
    int32_t last_shift_bid = 0, last_shift_ask = 0;
    bool invariants_ok = true;
    char invariant_msg[256] = "";

    auto buildSession = [&]() {
        qrsdp::TradingSession s{};
        s.seed = ui_seed;
        s.p0_ticks = ui_p0_ticks;
        s.session_seconds = ui_session_seconds;
        s.levels_per_side = static_cast<uint32_t>(ui_levels_per_side);
        s.tick_size = ui_tick_size;
        s.initial_spread_ticks = ui_initial_spread_ticks;
        s.initial_depth = ui_initial_depth;
        s.intensity_params.base_L = ui_base_L;
        s.intensity_params.base_C = ui_base_C;
        s.intensity_params.base_M = ui_base_M;
        s.intensity_params.imbalance_sensitivity = 1.0;
        s.intensity_params.cancel_sensitivity = 1.0;
        s.intensity_params.epsilon_exec = ui_epsilon_exec;
        s.queue_reactive.theta_reinit = static_cast<double>(ui_theta_reinit);
        s.queue_reactive.reinit_depth_mean = ui_reinit_mean;
        return s;
    };

    auto reset = [&]() {
        event_count = 0;
        count_add_bid = count_add_ask = count_cancel_bid = count_cancel_ask = 0;
        count_exec_buy = count_exec_sell = 0;
        up_shifts = down_shifts = 0;
        last_shift_t = 0.0;
        last_shift_bid = last_shift_ask = 0;
        invariants_ok = true;
        invariant_msg[0] = '\0';
        price_history.clear();
        event_rows.clear();
        depth_bid_best_history.clear();
        depth_ask_best_history.clear();
        sink.clear();
        attrSamplerPtr = std::make_unique<qrsdp::UnitSizeAttributeSampler>(rng, ui_alpha, ui_spread_improve);
        attrSampler = attrSamplerPtr.get();
        if (ui_model == 0) {
            legacyModelPtr = std::make_unique<qrsdp::SimpleImbalanceIntensity>(qrsdp::IntensityParams{
                ui_base_L, ui_base_C, ui_base_M, 1.0, 1.0, ui_epsilon_exec, ui_spread_sens});
            curveModelPtr.reset();
            intensityModel = legacyModelPtr.get();
        } else {
            const int K = static_cast<int>(ui_levels_per_side);
            const int nmax = ui_nmax > 0 ? ui_nmax : 100;
            qrsdp::HLRParams p = qrsdp::makeDefaultHLRParams(K, nmax);
            p.spread_sensitivity = ui_hlr_spread_sens;
            p.imbalance_sensitivity = ui_hlr_imb_sens;
            curveModelPtr = std::make_unique<qrsdp::CurveIntensityModel>(std::move(p));
            legacyModelPtr.reset();
            intensityModel = curveModelPtr.get();
        }
        producerPtr = std::make_unique<qrsdp::QrsdpProducer>(
            rng, book, *intensityModel, eventSampler, *attrSampler);
        producer = producerPtr.get();
        qrsdp::TradingSession session = buildSession();
        producer->startSession(session);
        ui_running = false;
        // Push initial book state
        qrsdp::Level bid = book.bestBid();
        qrsdp::Level ask = book.bestAsk();
        double mid = 0.5 * (bid.price_ticks + ask.price_ticks);
        PricePoint pt;
        pt.t = 0.0;
        pt.mid = mid;
        pt.bid = static_cast<double>(bid.price_ticks);
        pt.ask = static_cast<double>(ask.price_ticks);
        price_history.push_back(pt);
        depth_bid_best_history.push_back(static_cast<double>(bid.depth));
        depth_ask_best_history.push_back(static_cast<double>(ask.depth));
    };

    auto stepOne = [&]() -> bool {
        int32_t prev_bid = book.bestBid().price_ticks;
        int32_t prev_ask = book.bestAsk().price_ticks;
        bool produced = producer->stepOneEvent(sink);
        if (!produced) return false;
        const qrsdp::EventRecord& rec = sink.events().back();
        event_count++;
        if (rec.type == 0) count_add_bid++;
        else if (rec.type == 1) count_add_ask++;
        else if (rec.type == 2) count_cancel_bid++;
        else if (rec.type == 3) count_cancel_ask++;
        else if (rec.type == 4) count_exec_buy++;
        else if (rec.type == 5) count_exec_sell++;
        qrsdp::Level bid = book.bestBid();
        qrsdp::Level ask = book.bestAsk();
        double t = producer->currentTime();
        bool shift_down = (bid.price_ticks != prev_bid);
        bool shift_up = (ask.price_ticks != prev_ask);
        if (shift_down) { down_shifts++; last_shift_t = t; last_shift_bid = bid.price_ticks; last_shift_ask = ask.price_ticks; }
        if (shift_up)   { up_shifts++;   last_shift_t = t; last_shift_bid = bid.price_ticks; last_shift_ask = ask.price_ticks; }
        PricePoint pt;
        pt.t = t;
        pt.mid = 0.5 * (bid.price_ticks + ask.price_ticks);
        pt.bid = static_cast<double>(bid.price_ticks);
        pt.ask = static_cast<double>(ask.price_ticks);
        pt.shift_up = shift_up;
        pt.shift_down = shift_down;
        price_history.push_back(pt);
        if (price_history.size() > kPriceHistoryMax) price_history.pop_front();
        depth_bid_best_history.push_back(static_cast<double>(bid.depth));
        depth_ask_best_history.push_back(static_cast<double>(ask.depth));
        while (depth_bid_best_history.size() > kDepthHistoryMax) depth_bid_best_history.pop_front();
        while (depth_ask_best_history.size() > kDepthHistoryMax) depth_ask_best_history.pop_front();
        EventRow row;
        row.t_sec = t;
        row.ts_ns = rec.ts_ns;
        row.type = rec.type;
        row.side = rec.side;
        row.price_ticks = rec.price_ticks;
        row.qty = rec.qty;
        row.order_id = rec.order_id;
        row.is_shift_marker = false;
        event_rows.push_back(row);
        if (shift_down || shift_up) {
            EventRow marker;
            marker.t_sec = t;
            marker.is_shift_marker = true;
            snprintf(marker.shift_label, sizeof(marker.shift_label), "%s", shift_up && shift_down ? "SHIFT UP+DOWN" : shift_up ? "SHIFT UP" : "SHIFT DOWN");
            event_rows.push_back(marker);
        }
        while (event_rows.size() > kEventTableMax) event_rows.pop_front();
        // Invariants
        if (bid.price_ticks >= ask.price_ticks) {
            invariants_ok = false;
            snprintf(invariant_msg, sizeof(invariant_msg), "bid >= ask (%d >= %d)", bid.price_ticks, ask.price_ticks);
        } else if (bid.depth > 1000000u || ask.depth > 1000000u) {
            invariants_ok = false;
            snprintf(invariant_msg, sizeof(invariant_msg), "suspicious depth");
        } else if (ask.price_ticks - bid.price_ticks < 1) {
            invariants_ok = false;
            snprintf(invariant_msg, sizeof(invariant_msg), "spread < 1");
        }
        return true;
    };

    reset();

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        if (ui_running) {
            int limit = ui_max_events_per_frame;
            while (limit-- > 0 && stepOne()) {}
            if (producer->currentTime() >= static_cast<double>(ui_session_seconds))
                ui_running = false;
        }

        // --- Controls (left) ---
        ImGui::Begin("Controls");
        const char* model_items[] = { "Legacy (SimpleImbalance)", "HLR2014 (CurveIntensity + QueueReactive)" };
        if (ImGui::Combo("Model", &ui_model, model_items, 2)) {
            reset();
        }
        ImGui::InputScalar("Seed", ImGuiDataType_U64, &ui_seed);
        {
            uint32_t session_min = 5, session_max = 3600;
            ImGui::SliderScalar("Session (s)", ImGuiDataType_U32, &ui_session_seconds, &session_min, &session_max, "%u");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Max 3600 s = 1 hour");
        }
        {
            int levels_int = static_cast<int>(ui_levels_per_side);
            ImGui::InputInt("Levels per side", &levels_int);
            if (levels_int < 1) levels_int = 1;
            if (levels_int > 32) levels_int = 32;
            ui_levels_per_side = static_cast<uint32_t>(levels_int);
        }
        ImGui::InputScalar("Tick size", ImGuiDataType_U32, &ui_tick_size);
        ImGui::InputScalar("Initial depth", ImGuiDataType_U32, &ui_initial_depth);
        ImGui::InputScalar("Initial spread ticks", ImGuiDataType_U32, &ui_initial_spread_ticks);
        if (ui_model == 1) {
            ImGui::Separator();
            ImGui::Text("HLR2014 Curve Model");
            ImGui::InputDouble("spread_sens (HLR)", &ui_hlr_spread_sens, 0.05, 0, "%.2f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Spread-dependent feedback: >0 boosts adds and dampens execs when spread > 2");
            ImGui::InputDouble("imbalance_sens (HLR)", &ui_hlr_imb_sens, 0.1, 0, "%.2f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Imbalance feedback: >0 boosts execs on the heavier side for mean-reverting dynamics");
            const double theta_min = 0.0, theta_max = 1.0;
            ImGui::SliderScalar("theta_reinit", ImGuiDataType_Double, &ui_theta_reinit, &theta_min, &theta_max, "%.2f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Prob to reinitialize book after a shift (Poisson depths)");
            ImGui::InputDouble("reinit_depth_mean", &ui_reinit_mean, 1.0, 0, "%.1f");
            const char* preset_items[] = { "Starter large-tick", "High volatility", "Mean-reverting" };
            ImGui::Combo("Curve preset", &ui_curve_preset, preset_items, 3);
            ImGui::InputInt("Nmax (curves)", &ui_nmax); if (ui_nmax < 1) ui_nmax = 1; if (ui_nmax > 500) ui_nmax = 500;
        }
        if (ui_model == 0) {
            ImGui::Separator();
            ImGui::Text("Intensity (SimpleImbalance)");
            ImGui::InputDouble("base_L", &ui_base_L, 1.0, 0, "%.2f");
            ImGui::InputDouble("base_M", &ui_base_M, 1.0, 0, "%.2f");
            ImGui::InputDouble("base_C", &ui_base_C, 0.01, 0, "%.3f");
            ImGui::InputDouble("epsilon_exec", &ui_epsilon_exec, 0.01, 0, "%.3f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Baseline exec rate when I~0. lambda_exec = base_M*(epsilon_exec+...).");
            ImGui::InputDouble("spread_sens", &ui_spread_sens, 0.05, 0, "%.2f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Spread-dependent feedback: >0 boosts adds and dampens execs when spread > 2");
        }
        ImGui::Separator();
        ImGui::Text("Attribute sampler");
        ImGui::InputDouble("alpha (level)", &ui_alpha, 0.1, 0, "%.2f");
        ImGui::InputDouble("spread_improve", &ui_spread_improve, 0.1, 0, "%.2f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Coefficient for spread-improving order insertion. p_improve = min(1, (spread-1)*coeff)");
        ImGui::Separator();
        if (ImGui::Button("Reset")) reset();
        ImGui::SameLine();
        if (ImGui::Button("Step 1")) stepOne();
        ImGui::SameLine();
        ImGui::SetNextItemWidth(60);
        ImGui::InputInt("N", &ui_step_N); if (ui_step_N < 1) ui_step_N = 1; if (ui_step_N > 1000) ui_step_N = 1000;
        if (ImGui::Button("Step N")) { for (int i = 0; i < ui_step_N; i++) if (!stepOne()) break; }
        ImGui::Checkbox("Run", &ui_running);
        ImGui::SliderInt("Max events/frame", &ui_max_events_per_frame, 1, 5000, "%d");
        if (ImGui::Button("Debug Preset")) {
            ui_initial_depth = 2;
            ui_initial_spread_ticks = 2;
            ui_base_M = 30.0;
            ui_base_L = 5.0;
            ui_base_C = 0.1;
            ui_epsilon_exec = 0.2;
            ui_spread_sens = 0.0;
            ui_spread_improve = 0.0;
            ui_alpha = 0.5;
            ui_session_seconds = 30;
            reset();
        }
        ImGui::SameLine();
        if (ImGui::Button("Production Preset")) {
            ui_initial_depth = 5;
            ui_initial_spread_ticks = 2;
            ui_base_L = 20.0;
            ui_base_C = 0.5;
            ui_base_M = 15.0;
            ui_epsilon_exec = 0.5;
            ui_spread_sens = 0.4;
            ui_spread_improve = 0.5;
            ui_hlr_spread_sens = 0.3;
            ui_hlr_imb_sens = 1.0;
            ui_alpha = 0.5;
            ui_session_seconds = 120;
            reset();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Debug: low depth, no spread feedback (easy to trigger shifts).\nProduction: matches CLI defaults (realistic spread and depth behaviour).");
        }
        ImGui::End();

        // --- Diagnostics (top-right) ---
        ImGui::Begin("Top-of-book / Diagnostics");
        qrsdp::Level bid = book.bestBid();
        qrsdp::Level ask = book.bestAsk();
        qrsdp::BookState state;
        state.features = book.features();
        const size_t num_levels = book.numLevels();
        state.bid_depths.clear();
        state.ask_depths.clear();
        for (size_t k = 0; k < num_levels; ++k) {
            state.bid_depths.push_back(book.bidDepthAtLevel(k));
            state.ask_depths.push_back(book.askDepthAtLevel(k));
        }
        const qrsdp::BookFeatures& f = state.features;
        ImGui::Text("t = %.3f s   event_count = %lu", producer->currentTime(), (unsigned long)event_count);
        ImGui::Text("best_bid = %d   best_ask = %d   spread = %d", bid.price_ticks, ask.price_ticks, f.spread_ticks);
        ImGui::Text("best_bid_depth = %u   best_ask_depth = %u", bid.depth, ask.depth);
        ImGui::Text("imbalance I = %.4f", f.imbalance);
        ImGui::Separator();
        // Real-time intensities (exact producer logic via same model)
        {
            qrsdp::Intensities intens = intensityModel->compute(state);
            ImGui::Text("lambda_total (Lambda) = %.4f", intens.total());
            ImGui::Text("add_bid=%.3f add_ask=%.3f cancel_bid=%.3f cancel_ask=%.3f exec_buy=%.3f exec_sell=%.3f",
                        intens.add_bid, intens.add_ask, intens.cancel_bid, intens.cancel_ask, intens.exec_buy, intens.exec_sell);
            double p0_add = 1.0;
            if (ui_levels_per_side > 1) {
                double wsum = 0.0;
                for (uint32_t kk = 0; kk < ui_levels_per_side; ++kk)
                    wsum += std::exp(-ui_alpha * static_cast<double>(kk));
                if (wsum > 0.0) p0_add = 1.0 / wsum;
            }
            const double eff_add_bid = intens.add_bid * p0_add;
            const double eff_add_ask = intens.add_ask * p0_add;
            const double eff_cancel_bid = ui_base_C * static_cast<double>(f.q_bid_best);
            const double eff_cancel_ask = ui_base_C * static_cast<double>(f.q_ask_best);
            const double net_bid_drift = intens.exec_sell + eff_cancel_bid - eff_add_bid;
            const double net_ask_drift = intens.exec_buy + eff_cancel_ask - eff_add_ask;
            ImGui::Text("drift at best: bid=%.4f ask=%.4f  (P0=%.3f)", net_bid_drift, net_ask_drift, p0_add);
            const double exec_total = intens.exec_buy + intens.exec_sell;
            if (exec_total < 0.01 && ui_model == 0) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.5f, 0, 1));
                ImGui::Text("No execute intensity: set base_M > 0 and epsilon_exec > 0, then Reset");
                ImGui::PopStyleColor();
            } else if (net_bid_drift < 0.0 && net_ask_drift < 0.0 && ui_model == 0) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 0, 1));
                ImGui::Text("Depth accumulating regime -- shifts unlikely");
                ImGui::PopStyleColor();
            }
        }
        ImGui::Separator();
        ImGui::Text("Event counts: ADD_BID=%lu ADD_ASK=%lu CANCEL_BID=%lu CANCEL_ASK=%lu EXEC_BUY=%lu EXEC_SELL=%lu",
                    (unsigned long)count_add_bid, (unsigned long)count_add_ask,
                    (unsigned long)count_cancel_bid, (unsigned long)count_cancel_ask,
                    (unsigned long)count_exec_buy, (unsigned long)count_exec_sell);
        ImGui::Text("Shifts: up=%lu down=%lu total=%lu (producer shift_count=%lu)",
                    (unsigned long)up_shifts, (unsigned long)down_shifts, (unsigned long)(up_shifts + down_shifts),
                    (unsigned long)producer->shiftCountThisSession());
        ImGui::Text("Last shift: t=%.3f bid=%d ask=%d", last_shift_t, last_shift_bid, last_shift_ask);
        if (!invariants_ok && invariant_msg[0]) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.2f, 0.2f, 1));
            ImGui::Text("INVARIANT: %s", invariant_msg);
            ImGui::PopStyleColor();
        } else {
            ImGui::TextColored(ImVec4(0.2f, 1, 0.2f, 1), "Invariants OK");
        }
        ImGui::End();

        // --- Price over time ---
        ImGui::Begin("Price over time");
        ImGui::Checkbox("Mid", &ui_show_mid);
        ImGui::SameLine();
        ImGui::Checkbox("Bid/Ask", &ui_show_bid_ask);
        ImGui::SameLine();
        ImGui::Checkbox("Shift markers", &ui_show_shift_markers);
        if (ImPlot::BeginPlot("Price", ImVec2(-1, 280))) {
            if (!price_history.empty()) {
                const size_t n = price_history.size();
                std::vector<double> tx(n), mid_v(n), bid_v(n), ask_v(n);
                std::vector<double> shift_t, shift_y;
                double x_min = price_history[0].t, x_max = price_history[0].t;
                double y_min = price_history[0].mid, y_max = price_history[0].mid;
                for (size_t i = 0; i < n; i++) {
                    tx[i] = price_history[i].t;
                    mid_v[i] = price_history[i].mid;
                    bid_v[i] = price_history[i].bid;
                    ask_v[i] = price_history[i].ask;
                    x_min = std::min(x_min, price_history[i].t);
                    x_max = std::max(x_max, price_history[i].t);
                    y_min = std::min(y_min, std::min(price_history[i].mid, std::min(price_history[i].bid, price_history[i].ask)));
                    y_max = std::max(y_max, std::max(price_history[i].mid, std::max(price_history[i].bid, price_history[i].ask)));
                    if (ui_show_shift_markers && (price_history[i].shift_up || price_history[i].shift_down)) {
                        shift_t.push_back(price_history[i].t);
                        shift_y.push_back(price_history[i].mid);
                        y_min = std::min(y_min, price_history[i].mid);
                        y_max = std::max(y_max, price_history[i].mid);
                    }
                }
                double y_pad = (y_max - y_min) * 0.05;
                if (y_pad < 0.5) y_pad = 0.5;
                if (x_max <= x_min) x_max = x_min + 1.0;
                ImPlot::SetupAxesLimits(x_min, x_max, y_min - y_pad, y_max + y_pad, ImPlotCond_Always);
                if (ui_show_mid) ImPlot::PlotLine("Mid", tx.data(), mid_v.data(), (int)n);
                if (ui_show_bid_ask) {
                    ImPlot::PlotLine("Bid", tx.data(), bid_v.data(), (int)n);
                    ImPlot::PlotLine("Ask", tx.data(), ask_v.data(), (int)n);
                }
                if (ui_show_shift_markers && !shift_t.empty())
                    ImPlot::PlotScatter("Shift", shift_t.data(), shift_y.data(), (int)shift_t.size());
            }
            ImPlot::EndPlot();
        }
        ImGui::End();

        // --- Depth at best over time ---
        ImGui::Begin("Depth at best");
        if (ImPlot::BeginPlot("Depth", ImVec2(-1, 180))) {
            if (!depth_bid_best_history.empty()) {
                const size_t n = depth_bid_best_history.size();
                std::vector<double> tx(n), bid_dep(n), ask_dep(n);
                double d_min = depth_bid_best_history[0], d_max = depth_bid_best_history[0];
                for (size_t i = 0; i < n; i++) {
                    tx[i] = static_cast<double>(i);
                    bid_dep[i] = depth_bid_best_history[i];
                    ask_dep[i] = depth_ask_best_history[i];
                    d_min = std::min(d_min, std::min(depth_bid_best_history[i], depth_ask_best_history[i]));
                    d_max = std::max(d_max, std::max(depth_bid_best_history[i], depth_ask_best_history[i]));
                }
                double d_pad = (d_max - d_min) * 0.05;
                if (d_pad < 0.5) d_pad = 0.5;
                ImPlot::SetupAxesLimits(0.0, static_cast<double>(n) - 0.5, d_min - d_pad, d_max + d_pad, ImPlotCond_Always);
                ImPlot::PlotLine("Bid best", tx.data(), bid_dep.data(), (int)n);
                ImPlot::PlotLine("Ask best", tx.data(), ask_dep.data(), (int)n);
            }
            ImPlot::EndPlot();
        }
        ImGui::End();

        // --- Order book ladder ---
        ImGui::Begin("Order book ladder");
        if (ImGui::BeginTable("Ladder", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg, ImVec2(-1, 0), 0.0f)) {
            ImGui::TableSetupColumn("Bid price", ImGuiTableColumnFlags_WidthFixed, 100);
            ImGui::TableSetupColumn("Bid depth", ImGuiTableColumnFlags_WidthFixed, 80);
            ImGui::TableSetupColumn("Ask price", ImGuiTableColumnFlags_WidthFixed, 100);
            ImGui::TableSetupColumn("Ask depth", ImGuiTableColumnFlags_WidthFixed, 80);
            ImGui::TableHeadersRow();
            int32_t best_bid = book.bestBid().price_ticks;
            int32_t best_ask = book.bestAsk().price_ticks;
            int n = static_cast<int>(std::min(book.numLevels(), static_cast<size_t>(kLadderLevels)));
            for (int k = 0; k < n; k++) {
                ImGui::TableNextRow();
                int32_t bp = book.bidPriceAtLevel(static_cast<size_t>(k));
                uint32_t bd = book.bidDepthAtLevel(static_cast<size_t>(k));
                int32_t ap = book.askPriceAtLevel(static_cast<size_t>(k));
                uint32_t ad = book.askDepthAtLevel(static_cast<size_t>(k));
                bool best_bid_row = (bp == best_bid);
                bool best_ask_row = (ap == best_ask);
                if (best_bid_row) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(80, 120, 80, 80));
                if (best_ask_row) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(120, 80, 80, 80));
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%d", bp);
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%u", bd);
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%d", ap);
                ImGui::TableSetColumnIndex(3);
                ImGui::Text("%u", ad);
            }
            ImGui::EndTable();
        }
        ImGui::End();

        // --- Recent events ---
        ImGui::Begin("Recent events");
        if (ImGui::BeginTable("Events", 7, ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY, ImVec2(-1, -1), 0.0f)) {
            ImGui::TableSetupColumn("t", ImGuiTableColumnFlags_WidthFixed, 70);
            ImGui::TableSetupColumn("type", ImGuiTableColumnFlags_WidthFixed, 90);
            ImGui::TableSetupColumn("side", ImGuiTableColumnFlags_WidthFixed, 40);
            ImGui::TableSetupColumn("price", ImGuiTableColumnFlags_WidthFixed, 70);
            ImGui::TableSetupColumn("qty", ImGuiTableColumnFlags_WidthFixed, 40);
            ImGui::TableSetupColumn("order_id", ImGuiTableColumnFlags_WidthFixed, 80);
            ImGui::TableSetupColumn("marker", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();
            for (size_t i = 0; i < event_rows.size(); i++) {
                const EventRow& r = event_rows[i];
                ImGui::TableNextRow();
                if (r.is_shift_marker) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(100, 100, 200, 150));
                ImGui::TableSetColumnIndex(0); ImGui::Text("%.3f", r.t_sec);
                ImGui::TableSetColumnIndex(1); ImGui::Text("%s", r.is_shift_marker ? "" : eventTypeName(r.type));
                ImGui::TableSetColumnIndex(2); ImGui::Text("%u", r.side);
                ImGui::TableSetColumnIndex(3); ImGui::Text("%d", r.price_ticks);
                ImGui::TableSetColumnIndex(4); ImGui::Text("%u", r.qty);
                ImGui::TableSetColumnIndex(5); ImGui::Text("%lu", (unsigned long)r.order_id);
                ImGui::TableSetColumnIndex(6); ImGui::Text("%s", r.shift_label);
            }
            ImGui::EndTable();
        }
        ImGui::End();

        ImGui::Render();
        int w, h;
        glfwGetFramebufferSize(window, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.1f, 0.1f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
