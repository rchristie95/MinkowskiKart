//
//  Minkowski Kart - Relativistic VFX Manager
//  Handles visual effects for all relativistic powerups
//

#ifndef HEADER_RELATIVISTIC_VFX_HPP
#define HEADER_RELATIVISTIC_VFX_HPP

#include "utils/no_copy.hpp"
#include "utils/vec3.hpp"
#include <vector>

#ifndef SERVER_ONLY
class ParticleEmitter;
#endif

namespace irr
{
    namespace scene { class ISceneNode; class IMeshSceneNode; class IBillboardSceneNode; }
    namespace video { class SMaterial; }
}
using namespace irr;

class AbstractKart;
class BlackboardOverlay;

// Per-effect state for a single kart or projectile
struct WarpBubbleVFX
{
    float              rim_intensity;      // Einstein ring brightness
    float              ripple_phase;       // concentric ripple animation
    float              collapse_timer;     // collapse animation on block
    bool               active;
#ifndef SERVER_ONLY
    scene::IMeshSceneNode *sphere_node;
    ParticleEmitter   *shimmer_emitter;
#endif
    WarpBubbleVFX() : rim_intensity(0), ripple_phase(0),
                      collapse_timer(-1), active(false)
#ifndef SERVER_ONLY
                      , sphere_node(nullptr), shimmer_emitter(nullptr)
#endif
    {}
};

struct GeodesicMissileVFX
{
    float              trail_curvature;    // how bent the trail is
    float              warp_ahead;         // spacetime warp in front
    std::vector<Vec3>  trail_points;       // curved trail history
    float              flash_timer;        // impact flash
#ifndef SERVER_ONLY
    ParticleEmitter   *core_emitter;
    ParticleEmitter   *trail_emitter;
#endif
    GeodesicMissileVFX() : trail_curvature(0), warp_ahead(0),
                           flash_timer(-1)
#ifndef SERVER_ONLY
                           , core_emitter(nullptr), trail_emitter(nullptr)
#endif
    {}
};

struct BlackHoleVFX
{
    float              lensing_radius;     // gravitational lensing range
    float              accretion_phase;    // accretion disk rotation
    float              impulse_radius;     // gravitational pull range
    bool               returning;          // on return arc
#ifndef SERVER_ONLY
    scene::IBillboardSceneNode *disk_node;
    ParticleEmitter   *accretion_emitter;
#endif
    BlackHoleVFX() : lensing_radius(2.0f), accretion_phase(0),
                     impulse_radius(4.0f), returning(false)
#ifndef SERVER_ONLY
                     , disk_node(nullptr), accretion_emitter(nullptr)
#endif
    {}
};

struct TimeDilationVFX
{
    float              redshift_intensity; // how strong the red haze is
    float              smear_factor;       // motion trail stretching
    float              drag_sound_pitch;   // for audio deepening
#ifndef SERVER_ONLY
    ParticleEmitter   *halo_emitter;
#endif
    TimeDilationVFX() : redshift_intensity(0), smear_factor(0),
                        drag_sound_pitch(1.0f)
#ifndef SERVER_ONLY
                        , halo_emitter(nullptr)
#endif
    {}
};

struct MassSpikeVFX
{
    float              compression_factor; // how much chassis is squished
    float              ground_ripple;      // compressed-space ripples
    float              strain_sparks;      // spark rate under load
#ifndef SERVER_ONLY
    ParticleEmitter   *downward_emitter;
    ParticleEmitter   *spark_emitter;
#endif
    MassSpikeVFX() : compression_factor(0), ground_ripple(0),
                     strain_sparks(0)
#ifndef SERVER_ONLY
                     , downward_emitter(nullptr), spark_emitter(nullptr)
#endif
    {}
};

struct FrameShiftVFX
{
    float              wave_progress;      // 0-1 sweep across track
    float              wave_radius;        // current wave radius from origin
    Vec3               origin;             // where the shift started
    float              chromatic_split;    // red/blue separation
#ifndef SERVER_ONLY
    ParticleEmitter   *grid_emitter;
#endif
    FrameShiftVFX() : wave_progress(0), wave_radius(0),
                      chromatic_split(0)
#ifndef SERVER_ONLY
                      , grid_emitter(nullptr)
#endif
    {}
};

struct NeutronStarVFX
{
    float              core_brightness;    // how bright the dense core is
    float              halo_tightness;     // how tight the halo clings
    float              impact_flash;       // white flash on hit
#ifndef SERVER_ONLY
    ParticleEmitter   *core_emitter;
    ParticleEmitter   *halo_emitter;
#endif
    NeutronStarVFX() : core_brightness(1.0f), halo_tightness(0.3f),
                       impact_flash(-1)
#ifndef SERVER_ONLY
                       , core_emitter(nullptr), halo_emitter(nullptr)
#endif
    {}
};

struct CosmicStringVFX
{
    float              filament_brightness;// how bright the string glows
    float              stress_phase;       // energy running along string
    float              lensing_artifact;   // tiny lensing flickers
    bool               is_blackboard;      // backward-fired blackboard mode
    float              blackboard_wobble;  // wobble animation
    float              blackboard_opacity; // fade in/out
#ifndef SERVER_ONLY
    ParticleEmitter   *filament_emitter;
#endif
    CosmicStringVFX() : filament_brightness(1.0f), stress_phase(0),
                        lensing_artifact(0), is_blackboard(false),
                        blackboard_wobble(0), blackboard_opacity(0)
#ifndef SERVER_ONLY
                        , filament_emitter(nullptr)
#endif
    {}
};

struct TidalArmVFX
{
    float              arc_progress;       // 0-1 swing animation
    float              distortion_width;   // how wide the force sheet is
    float              spaghettification;  // stretch on contact
#ifndef SERVER_ONLY
    ParticleEmitter   *arc_emitter;
#endif
    TidalArmVFX() : arc_progress(0), distortion_width(0.5f),
                    spaghettification(0)
#ifndef SERVER_ONLY
                    , arc_emitter(nullptr)
#endif
    {}
};


/**
 * Manages all relativistic visual effects for Minkowski Kart powerups.
 * Handles particle emitters, shader uniforms, and per-frame VFX updates.
 */
class RelativisticVFXManager : public NoCopy
{
private:
    // Active effect instances (indexed by kart ID or projectile ID)
    std::vector<WarpBubbleVFX>     m_warp_bubbles;
    std::vector<TimeDilationVFX>   m_time_dilations;
    std::vector<MassSpikeVFX>      m_mass_spikes;
    std::vector<TidalArmVFX>       m_tidal_arms;

    // Blackboard overlays (per-kart, active when hit by Cosmic String backward)
    std::vector<BlackboardOverlay*> m_blackboards;

    // Singleton active effects for projectiles/global
    std::vector<GeodesicMissileVFX> m_geodesic_missiles;
    std::vector<BlackHoleVFX>       m_black_holes;
    std::vector<NeutronStarVFX>     m_neutron_stars;
    std::vector<CosmicStringVFX>    m_cosmic_strings;
    FrameShiftVFX                   m_frame_shift;

    float m_global_time;

    void updateWarpBubble(WarpBubbleVFX &vfx, float dt,
                          AbstractKart *kart);
    void updateTimeDilation(TimeDilationVFX &vfx, float dt,
                            AbstractKart *kart);
    void updateMassSpike(MassSpikeVFX &vfx, float dt,
                         AbstractKart *kart);
    void updateFrameShift(float dt);

public:
    RelativisticVFXManager();
    ~RelativisticVFXManager();

    void init(unsigned int num_karts);
    void reset();
    void update(float dt);
    void updateGraphics(float dt);

    // Per-kart attachment effects
    void activateWarpBubble(unsigned int kart_id);
    void deactivateWarpBubble(unsigned int kart_id);
    void warpBubbleHit(unsigned int kart_id);

    void activateTimeDilation(unsigned int kart_id);
    void deactivateTimeDilation(unsigned int kart_id);

    void activateMassSpike(unsigned int kart_id);
    void deactivateMassSpike(unsigned int kart_id);

    void activateTidalArm(unsigned int kart_id);
    void deactivateTidalArm(unsigned int kart_id);

    // Frame shift (global effect)
    void triggerFrameShift(const Vec3 &origin);

    // Blackboard overlay (Cosmic String backward-fire gag)
    void triggerBlackboard(unsigned int kart_id, float duration_seconds);
    void renderBlackboards();

    // Query for rendering
    const WarpBubbleVFX *getWarpBubble(unsigned int kart_id) const;
    const TimeDilationVFX *getTimeDilation(unsigned int kart_id) const;
    const MassSpikeVFX *getMassSpike(unsigned int kart_id) const;
    const FrameShiftVFX &getFrameShift() const { return m_frame_shift; }

    float getGlobalTime() const { return m_global_time; }

    static RelativisticVFXManager *get();
    static void create();
    static void destroy();
};

extern RelativisticVFXManager *relativistic_vfx_manager;

#endif // HEADER_RELATIVISTIC_VFX_HPP
