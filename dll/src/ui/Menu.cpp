#include <framework/stdafx.h>
#include "Menu.h"
#include "Styles.h"
#include <features/Cheats.h>
#include <imgui.h>
#include <stdio.h>

namespace UI {
    static void HelpMarker(const char* desc) {
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
            ImGui::TextUnformatted(desc);
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
    }

    void DrawTeleportMenu()
    {
        static bool style_setup = false;
        if (!style_setup) {
            SetupModernStyle();
            style_setup = true;
        }

        if (!Features::g_teleport.menu_open)
            return;

        ImGui::SetNextWindowSize(ImVec2(450, 600), ImGuiCond_FirstUseEver);
        
        if (ImGui::Begin("MIO Memories in Orbit - Transmit", &Features::g_teleport.menu_open, ImGuiWindowFlags_NoCollapse)) {
            
            // --- 状态面板 ---
            if (ImGui::CollapsingHeader("系统状态", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Columns(2, "status_cols", false);
                
                ImGui::Text("坐标状态:"); 
                ImGui::NextColumn();
                if (Features::g_teleport.last_read_ok)
                    ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), "正常 (%.1f, %.1f)", Features::g_teleport.current_x, Features::g_teleport.current_y);
                else
                    ImGui::TextColored(ImVec4(0.8f, 0.2f, 0.2f, 1.0f), "失败");
                
                ImGui::NextColumn();
                ImGui::Text("能量数值:");
                ImGui::NextColumn();
                ImGui::Text("%.2f", Features::g_energy.current_value);
                
                ImGui::Columns(1);
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // --- 核心修改器 ---
            if (ImGui::CollapsingHeader("核心修改器", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (ImGui::Checkbox("去除能量消耗", &Features::g_energy.lock_enabled)) {
                    // Feedback or sound could go here
                }
                
                ImGui::Spacing();
                
                ImGui::Checkbox("飞行模式 (WASD)", &Features::g_fly.enabled);
                ImGui::SameLine(); HelpMarker("使用 WASD 键在空中移动，Shift 键加速");
                
                if (Features::g_fly.enabled) {
                    ImGui::Indent();
                    ImGui::SliderFloat("移动步长", &Features::g_fly.step, 0.1f, 10.0f, "%.1f");
                    ImGui::SliderFloat("加速倍率", &Features::g_fly.speed_multiplier, 1.0f, 10.0f, "%.1f");
                    ImGui::Unindent();
                }
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // --- 传送系统 ---
            if (ImGui::CollapsingHeader("传送管理", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::InputTextWithHint("##note", "输入坐标备注...", Features::g_teleport.pending_note, sizeof(Features::g_teleport.pending_note));
                
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.26f, 0.59f, 0.98f, 0.40f));
                if (ImGui::Button("保存当前坐标", ImVec2(ImGui::GetContentRegionAvail().x, 0)))
                    Features::AddTeleportEntry();
                ImGui::PopStyleColor();

                ImGui::Spacing();

                if (ImGui::BeginListBox("##locations", ImVec2(-FLT_MIN, 180))) {
                    for (int i = 0; i < static_cast<int>(Features::g_teleport.entries.size()); ++i) {
                        const auto& entry = Features::g_teleport.entries[i];
                        char label[256];
                        if (!entry.note.empty())
                            snprintf(label, sizeof(label), "[%d] %s (%.1f, %.1f)", i + 1, entry.note.c_str(), entry.x, entry.y);
                        else
                            snprintf(label, sizeof(label), "[%d] 坐标: %.1f, %.1f", i + 1, entry.x, entry.y);

                        const bool selected = (Features::g_teleport.selected_index == i);
                        if (ImGui::Selectable(label, selected))
                            Features::g_teleport.selected_index = i;

                        if (selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndListBox();
                }

                ImGui::Spacing();

                float btn_width = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2.0f;
                if (ImGui::Button("传送到选中", ImVec2(btn_width, 0)))
                    Features::TeleportToSelected();
                
                ImGui::SameLine();
                if (ImGui::Button("删除选中", ImVec2(btn_width, 0)))
                    Features::DeleteSelectedEntry();
            }

            // --- 底部信息 ---
            ImGui::SetCursorPosY(ImGui::GetWindowHeight() - 40);
            ImGui::Separator();
            ImGui::TextDisabled("快捷键: F6 快速传送 | F7 显示/隐藏 | F9 卸载插件");
        }
        ImGui::End();
    }
}
