// [XCDE] Cheats controls (ported from Monado II, MIT; LINUX-120/129), identical in the launcher's Cheats tab and the F1
// menu. live: the game is running (F1), so changes apply at once and Apply remembers them for the next launch;
// otherwise (launcher) they just get saved to xcde.ini for the next start. Returns true when a value changed; the F1
// caller publishes the settings to the guest hooks (xcde_cheats_publish) so they apply at once.
#pragma once
#include "imgui.h"
#include <algorithm>
#include "../settings.h"
#include "../cheats_options.h"

static inline bool monado_cheat_controls(Settings *s, bool english, bool live) {
    bool changed = false;
    if (live) ImGui::TextWrapped("%s", english
        ? "Changes apply during play. Apply remembers them for the next launch."
        : "Los cambios se aplican durante la partida. Aplicar los recuerda al reiniciar.");
    ImGui::TextWrapped("%s", english ? "Enemy health: 0.1x to 20x. Other multipliers: 0x to 20x. Steps: 0.01x (1%); 1x is unchanged."
        : "Vida enemiga: de 0.1x a 20x. Otros: de 0x a 20x. Pasos: 0.01x (1%); 1x no cambia la cantidad.");
    bool *enabled[] = {&s->cheat_damage_dealt, &s->cheat_damage_received, &s->cheat_exp,
                       &s->cheat_ap, &s->cheat_sp, &s->cheat_gold, &s->cheat_enemy_health};
    int *percent[] = {&s->cheat_damage_dealt_percent, &s->cheat_damage_received_percent, &s->cheat_exp_percent,
                      &s->cheat_ap_percent, &s->cheat_sp_percent, &s->cheat_gold_percent, &s->cheat_enemy_health_percent};
    const char *labels[] = {english ? "Party damage dealt" : "Daño causado por el grupo",
        english ? "Party damage received" : "Daño recibido por el grupo", english ? "EXP gained" : "EXP obtenida",
        english ? "AP gained" : "AP obtenidos", english ? "SP gained" : "SP obtenidos",
        english ? "Gold gained" : "Dinero obtenido", english ? "Enemy health" : "Vida de los enemigos"};
    // Indexes match XCDE_CHEAT_*; Enemy health is shown with Combat.
    const int order[] = {0, 1, 6, 2, 3, 4, 5};
    for (int i : order) {
        if (i == 0) ImGui::SeparatorText(english ? "Combat" : "Combate");
        if (i == 2) ImGui::SeparatorText(english ? "Rewards" : "Recompensas");
        ImGui::PushID(i);
        changed |= ImGui::Checkbox(labels[i], enabled[i]);
        if (i == 6) ImGui::SetItemTooltip("%s", english
            ? "Live changes preserve the current HP percentage. Small health bars keep a safety minimum for scripted phases."
            : "Los cambios conservan el porcentaje de vida actual. Las barras pequeñas mantienen un mínimo de seguridad para las fases con guion.");
        ImGui::BeginDisabled(!*enabled[i]);
        float amount_width = ImGui::CalcTextSize("20.00x").x;
        ImGui::SetNextItemWidth(std::max(40.f, std::min(360.f, ImGui::GetContentRegionAvail().x - amount_width -
                                                                   ImGui::GetStyle().ItemInnerSpacing.x)));
        changed |= ImGui::SliderInt("##multiplier", percent[i], xcde_cheats_minimum_percent(i), 2000, "%d%%", ImGuiSliderFlags_AlwaysClamp);
        ImGui::SetItemTooltip("%s", english ? "Ctrl-click to type a percentage. Ctrl+arrow adjusts 1%."
            : "Ctrl+clic para escribir un porcentaje. Ctrl+flecha ajusta 1%.");
        ImGui::SameLine(); ImGui::Text("%.2fx", *percent[i] / 100.0);
        ImGui::EndDisabled(); ImGui::PopID();
    }
    ImGui::SeparatorText(english ? "Exploration" : "Exploración");
    changed |= ImGui::Checkbox(english ? "No fall damage" : "Sin daño por caídas", &s->cheat_no_fall_damage);
    changed |= ImGui::Checkbox(english ? "High jump (x8)" : "Salto alto (x8)", &s->cheat_high_jump);  // [XCDE] T276
    ImGui::SetItemTooltip("%s", english
        ? "Multiplies jump height by 8 through the game's own jump calculation (the community jump code).\nThe jump impulse, collisions and landing are still the game's."
        : "Multiplica por 8 la altura del salto usando el propio cálculo del juego (el código comunitario).\nEl impulso, las colisiones y el aterrizaje siguen siendo del juego.");
    ImGui::TextWrapped("%s", english ? "EXP, AP, SP and gold already earned remain in your save after disabling cheats."
        : "La EXP, AP, SP y el dinero ganados permanecen en la partida al desactivar los trucos.");
    return changed;
}
