#include <foobar2000/sdk/foobar2000.h>

// I-define ang mga mode para sa manu-manong pag-sync
enum SyncMode {
    SYNC_MODE_NONE = 0,
    SYNC_MODE_MATCH_NEXT,   // Button 1: Ang kasalukuyang kanta ang mag-aadjust sa susunod
    SYNC_MODE_MATCH_CURRENT // Button 2: Ang susunod na kanta ang mapupuwersang sumunod sa kasalukuyan
};

// Global variable para malaman ng buong plugin kung anong mode ang aktibo
static SyncMode g_active_sync_mode = SYNC_MODE_NONE;

class dsp_bpmsync_v2 : public dsp_impl_base {
private:
    double m_current_track_bpm = 0.0;
    double m_next_track_bpm = 0.0;
    double m_current_speed_ratio = 1.0;
    double m_transition_duration = 10.0; // Huling 10 segundo ng kanta magsisimula ang pag-sync (para sa Button 1)

    // Helper function para basahin ang BPM tag (%bpm%) mula sa kanta
    double get_track_bpm(metadb_handle_ptr p_track) {
        if (p_track.is_empty()) return 0.0;
        file_info_impl info;
        if (p_track->get_info(info)) {
            const char* bpm_str = info.meta_get("bpm", 0);
            if (bpm_str != nullptr) return atof(bpm_str);
        }
        return 0.0;
    }

public:
    dsp_bpmsync_v2() {
        m_current_track_bpm = 0.0;
        m_next_track_bpm = 0.0;
        m_current_speed_ratio = 1.0;
    }

    static void g_get_name(pfc::string_base & p_out) { 
        p_out = "BPM Manual Controller Sync"; 
    }
    
    static GUID g_get_guid() {
        // Natatanging GUID para sa DSP Component na ito
        static const GUID guid = { 0x6b3d51f2, 0x9c4a, 0x4f81, { 0xad, 0x7e, 0x8c, 0x93, 0xa2, 0xb1, 0x55, 0x66 } };
        return guid;
    }

    // Tinatawag ng foobar tuwing lilipat ang kanta
    void on_track_change(metadb_handle_ptr p_next_track) {
        static_api_ptr_t<playback_control> pc;
        metadb_handle_ptr p_current_track;
        
        if (pc->get_now_playing(p_current_track)) {
            m_current_track_bpm = get_track_bpm(p_current_track);
        }
        m_next_track_bpm = get_track_bpm(p_next_track);

        // Kung lumipat na sa susunod na kanta at HINDI naka-Button 2 (MATCH_CURRENT), i-reset ang speed sa normal
        if (g_active_sync_mode != SYNC_MODE_MATCH_CURRENT) {
            m_current_speed_ratio = 1.0;
        }
    }

    // Pangunahing audio processing ng foobar
    void process_core(audio_chunk * p_chunk, dsp_chunk_list * p_chunk_list, abort_callback & p_abort) {
        static_api_ptr_t<playback_control> pc;
        double total_time = 0, current_time = 0;

        if (pc->get_now_playing_time(current_time) && pc->get_now_playing_length(total_time)) {
            double time_remaining = total_time - current_time;

            if (m_current_track_bpm > 0 && m_next_track_bpm > 0) {
                
                // === LOGIC NG BUTTON 1 (Current adjusts to Next) ===
                if (g_active_sync_mode == SYNC_MODE_MATCH_NEXT && time_remaining <= m_transition_duration) {
                    double target_ratio = m_next_track_bpm / m_current_track_bpm;
                    double progress = (m_transition_duration - time_remaining) / m_transition_duration;
                    m_current_speed_ratio = 1.0 + (target_ratio - 1.0) * progress; // Unti-unting binabago ang bilis
                }
                
                // === LOGIC NG BUTTON 2 (Next adjusts to Current) ===
                else if (g_active_sync_mode == SYNC_MODE_MATCH_CURRENT) {
                    // Pinupuwersa ang kasalukuyang tumutugtog na kanta (na naging 'next' track na ngayon) na gayahin ang lumang tempo
                    m_current_speed_ratio = m_current_track_bpm / m_next_track_bpm;
                }
                
                else {
                    m_current_speed_ratio = 1.0;
                }
            }
        }

        // Isagawa ang dynamic resampling speed change
        if (m_current_speed_ratio != 1.0) {
            unsigned int original_sample_rate = p_chunk->get_srate();
            p_chunk->set_srate(static_cast<unsigned int>(original_sample_rate * m_current_speed_ratio));
        }
    }

    void flush() { m_current_speed_ratio = 1.0; }
    double get_latency() { return 0; }
    bool need_track_change_notice() { return true; }
};

static dsp_factory_nopreview_t<dsp_bpmsync_v2> g_dsp_bpmsync_factory;


// ============================================================================
// LOGIC PARA SA MGA BUTTONS (Mainmenu Commands)
// ============================================================================

class mainmenu_commands_bpmsync : public mainmenu_commands {
public:
    enum {
        cmd_sync_to_next = 0,
        cmd_sync_to_current,
        cmd_total
    };

    t_uint32 get_command_count() override { return cmd_total; }

    // Mga pangalan ng commands na lilitaw sa foobar configuration
    void get_name(t_uint32 p_index, pfc::string_base & p_out) override {
        switch (p_index) {
            case cmd_sync_to_next: p_out = "BPM Sync: Adjust Current Song to Next Song (Button 1)"; break;
            case cmd_sync_to_current: p_out = "BPM Sync: Adjust Next Song to Current Song (Button 2)"; break;
        }
    }

    void get_description(t_uint32 p_index, pfc::string_base & p_out) override {
        switch (p_index) {
            case cmd_sync_to_next: p_out = "Unti-unting binabago ang tempo ng kasalukuyang kanta para tumugma sa susunod."; break;
            case cmd_sync_to_current: p_out = "Pinupuwersa ang susunod na kanta na sumunod sa tempo ng kasalukuyang kanta."; break;
        }
    }

    GUID get_parent() override {
        return mainmenu_groups::playback; // Ilalagay ito sa ilalim ng "Playback" menu ng foobar
    }

    GUID get_command(t_uint32 p_index) override {
        // Mga natatanging GUID para sa keyboard shortcuts mo
        static const GUID guid_btn1 = { 0x1f2e3d4c, 0x5b6a, 0x7c8d, { 0x9e, 0x0f, 0x1a, 0x2b, 0x3c, 0x4d, 0x5e, 0x6f } };
        static const GUID guid_btn2 = { 0x2f3e4d5c, 0x6b7a, 0x8c9d, { 0x0e, 0x1f, 0x2a, 0x3b, 0x4c, 0x5d, 0x6e, 0x7f } };
        return (p_index == cmd_sync_to_next) ? guid_btn1 : guid_btn2;
    }

    // Ang aktwal na mangyayari kapag pinindot ang Button 1 o Button 2
    void execute(t_uint32 p_index, service_ptr_t<service_base> p_callback) override {
        switch (p_index) {
            case cmd_sync_to_next:
                if (g_active_sync_mode == SYNC_MODE_MATCH_NEXT) {
                    g_active_sync_mode = SYNC_MODE_NONE; // Patayin ang mode kapag pinindot ulit
                    fb2k::inMainThread([] { popup_message::g_show("BPM Sync Disabled", "Info"); });
                } else {
                    g_active_sync_mode = SYNC_MODE_MATCH_NEXT;
                    fb2k::inMainThread([] { popup_message::g_show("Mode Activated: Current song matching Next song!", "BPM Sync"); });
                }
                break;

            case cmd_sync_to_current:
                if (g_active_sync_mode == SYNC_MODE_MATCH_CURRENT) {
                    g_active_sync_mode = SYNC_MODE_NONE; // Patayin ang mode kapag pinindot ulit
                    fb2k::inMainThread([] { popup_message::g_show("BPM Sync Disabled", "Info"); });
                } else {
                    g_active_sync_mode = SYNC_MODE_MATCH_CURRENT;
                    fb2k::inMainThread([] { popup_message::g_show("Mode Activated: Next song will match Current tempo!", "BPM Sync"); });
                }
                break;
        }
    }
};

static mainmenu_commands_factory_t<mainmenu_commands_bpmsync> g_mainmenu_commands_bpmsync_factory;

// Pagpapakilala ng bersyon ng component sa foobar2000 settings
DECLARE_COMPONENT_VERSION("BPM Manual Controller Sync", "1.1", "Gawa sa tulong ng AI - May manual triggers para sa flexible song transitions.");
VALIDATE_COMPONENT_FILENAME("foo_dsp_bpmsync.dll");
    
